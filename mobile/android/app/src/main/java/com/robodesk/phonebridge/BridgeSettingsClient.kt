package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

/** Why a settings operation did not produce a result. Messages are Indonesian UI text; no secret is ever included. */
class SettingsFailure(val reason: Reason, val status: P.Status? = null, val statusCode: Int = -1) {
    enum class Reason { NOT_CONNECTED, QUEUE_FULL, TIMEOUT, STATUS, MALFORMED, CRC, SEQUENCE, INVALID_REQUEST, TOO_MANY_RESTARTS, UNCERTAIN, REJECTED }

    val message: String
        get() = when (reason) {
            Reason.NOT_CONNECTED -> "Robot belum terhubung"
            Reason.QUEUE_FULL -> "Antrean permintaan penuh, coba lagi"
            Reason.TIMEOUT -> "Robot tidak menjawab (timeout)"
            Reason.STATUS -> status?.label ?: "Status robot tidak dikenal ($statusCode)"
            Reason.MALFORMED -> "Respons robot tidak valid"
            Reason.CRC -> "CRC dokumen tidak cocok"
            Reason.SEQUENCE -> "Urutan halaman respons salah"
            Reason.INVALID_REQUEST -> "Input tidak valid"
            Reason.TOO_MANY_RESTARTS -> "Dokumen terus berubah saat dibaca, coba lagi"
            Reason.REJECTED -> "Robot menolak sesi ini (replay/cabut) · menyambung ulang dengan sesi baru"
            Reason.UNCERTAIN -> "Hasil tidak pasti (koneksi terputus atau tak ada jawaban); baca ulang dari robot sebelum mengulang"
        }
}

sealed class SettingsOutcome<out T> {
    class Ok<T>(val value: T) : SettingsOutcome<T>()
    class Fail(val failure: SettingsFailure) : SettingsOutcome<Nothing>()
}

typealias SettingsDone<T> = (SettingsOutcome<T>) -> Unit

fun interface SettingsTransport {
    /** Queues [request]; [callback] is invoked exactly once (possibly synchronously when not connected). */
    fun send(request: P.Request, callback: SettingsDone<P.Response>)
}

/**
 * Bridge between the activity and RoboNotificationListener, which owns the single authenticated GATT session.
 * The service installs [submit] while it runs; calls are made from the main thread.
 */
object PhoneSettingsBridge : SettingsTransport {
    @Volatile var submit: ((P.Request, SettingsDone<P.Response>) -> Unit)? = null

    override fun send(request: P.Request, callback: SettingsDone<P.Response>) {
        val target = submit
        if (target == null) {
            request.wipe()
            callback(SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.NOT_CONNECTED)))
        } else target(request, callback)
    }
}

/** High-level operations (paging, restart rule, CRC checks) on top of a [SettingsTransport]. */
class BridgeSettingsClient(private val transport: SettingsTransport) {
    class Paged(val data: ByteArray, val complete: Boolean)
    class MemoryList(val revision: Long, val facts: List<P.Fact>, val unchanged: Boolean)

    private fun fail(reason: SettingsFailure.Reason) = SettingsOutcome.Fail(SettingsFailure(reason))

    /** Sends one request; a non-Ok robot status becomes a STATUS failure. */
    fun call(request: P.Request, done: SettingsDone<P.Response>) {
        transport.send(request) { outcome ->
            if (outcome is SettingsOutcome.Ok && outcome.value.statusCode != 0) {
                done(SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.STATUS, outcome.value.status, outcome.value.statusCode)))
            } else done(outcome)
        }
    }

    /** Reads a paged document page by page; restarts at page 0 (max 3 times) when the header changes mid-read. */
    fun readPaged(makeRequest: (Int) -> P.Request?, stopAfterFirst: ((ByteArray) -> Boolean)? = null, done: SettingsDone<Paged>) {
        val assembler = P.PageAssembler()
        var restarts = 0
        fun finishWith(outcome: SettingsOutcome<Paged>) { assembler.reset(); done(outcome) }
        fun step(page: Int) {
            val request = makeRequest(page) ?: return finishWith(fail(SettingsFailure.Reason.INVALID_REQUEST))
            call(request) { outcome ->
                if (outcome is SettingsOutcome.Fail) return@call finishWith(outcome)
                val parsed = P.parsePage((outcome as SettingsOutcome.Ok).value.body) ?: return@call finishWith(fail(SettingsFailure.Reason.MALFORMED))
                when (assembler.accept(parsed)) {
                    P.PageAssembler.Step.NEXT -> if (parsed.index == 0 && stopAfterFirst?.invoke(parsed.data) == true)
                        finishWith(SettingsOutcome.Ok(Paged(parsed.data, false))) else step(assembler.nextPage)
                    P.PageAssembler.Step.DONE -> {
                        val document = assembler.document ?: return@call finishWith(fail(SettingsFailure.Reason.MALFORMED))
                        done(SettingsOutcome.Ok(Paged(document, true)))
                    }
                    P.PageAssembler.Step.RESTART -> if (++restarts > MAX_RESTARTS) finishWith(fail(SettingsFailure.Reason.TOO_MANY_RESTARTS)) else step(0)
                    P.PageAssembler.Step.SEQUENCE_ERROR -> finishWith(fail(SettingsFailure.Reason.SEQUENCE))
                    P.PageAssembler.Step.CRC_MISMATCH -> finishWith(fail(SettingsFailure.Reason.CRC))
                }
            }
        }
        step(0)
    }

    fun readSettings(done: SettingsDone<ByteArray>) =
        readPaged({ P.settingsGet(it) }, null) { done(it.document()) }

    /**
     * Sends a complete schema-1 document; Ok(true) once the robot reports it applied and saved. A SequenceError or
     * CrcMismatch from the robot restarts the upload at page 0 (at most [MAX_SET_RESTARTS] times). An UNCERTAIN
     * outcome is passed through: the caller must re-read before assuming either result.
     */
    fun writeSettings(document: ByteArray, done: SettingsDone<Boolean>) {
        var restarts = 0
        fun attempt() {
            val pages = P.settingsSetPages(document) ?: return done(fail(SettingsFailure.Reason.INVALID_REQUEST))
            fun step(index: Int) {
                if (index > 0) pages[index].priorityHint = RequestPriority.CONTINUATION
                call(pages[index]) { outcome ->
                    if (outcome is SettingsOutcome.Fail) {
                        val status = outcome.failure.status
                        if ((status == P.Status.SEQUENCE_ERROR || status == P.Status.CRC_MISMATCH) && restarts < MAX_SET_RESTARTS) {
                            restarts++
                            for (i in index + 1..pages.lastIndex) pages[i].wipe()
                            return@call attempt()
                        }
                        for (i in index + 1..pages.lastIndex) pages[i].wipe()
                        return@call done(outcome)
                    }
                    val body = P.parseSetBody((outcome as SettingsOutcome.Ok).value.body)
                    val last = index == pages.lastIndex
                    when {
                        body == null || body.first != index || body.second != last -> done(fail(SettingsFailure.Reason.MALFORMED))
                        last -> done(SettingsOutcome.Ok(true))
                        else -> step(index + 1)
                    }
                }
            }
            step(0)
        }
        attempt()
    }

    fun readStatus(done: SettingsDone<P.StatusSnapshot>) =
        readPaged({ if (it == 0) P.statusSnapshot() else null }, null) { outcome ->
            done(outcome.document().map { P.parseStatus(it) })
        }

    /**
     * Reads the fact list. With [cachedRevision] the first window's revision is compared and the read stops after
     * one round trip when it is unchanged.
     */
    fun listMemory(cachedRevision: Long?, done: SettingsDone<MemoryList>) {
        var facts = ArrayList<P.Fact>()
        var raw = java.io.ByteArrayOutputStream()
        var revision = 0L
        var afterId = 0L
        var windows = 0
        var restarts = 0
        fun restart(): Boolean { facts = ArrayList(); raw = java.io.ByteArrayOutputStream(); afterId = 0L; windows = 0; return ++restarts <= MAX_RESTARTS }
        fun window() {
            val stop: ((ByteArray) -> Boolean)? = if (afterId == 0L && cachedRevision != null) { first ->
                first.size >= 8 && first[0].toInt() == 1 && le32(first, 1) == cachedRevision
            } else null
            readPaged({ P.memoryList(afterId, it) }, stop) { outcome ->
                if (outcome is SettingsOutcome.Fail) return@readPaged done(outcome)
                val paged = (outcome as SettingsOutcome.Ok).value
                if (!paged.complete) return@readPaged done(SettingsOutcome.Ok(MemoryList(cachedRevision ?: 0L, emptyList(), true)))
                val w = P.parseMemoryWindow(paged.data) ?: return@readPaged done(fail(SettingsFailure.Reason.MALFORMED))
                if (windows == 0 && cachedRevision != null && w.revision == cachedRevision) {
                    return@readPaged done(SettingsOutcome.Ok(MemoryList(cachedRevision, emptyList(), true)))
                }
                if (windows == 0) revision = w.revision
                else if (w.revision != revision) {
                    if (!restart()) return@readPaged done(fail(SettingsFailure.Reason.TOO_MANY_RESTARTS))
                    return@readPaged window()
                }
                facts.addAll(w.facts)
                raw.write(w.raw)
                windows++
                if (w.more) {
                    if (w.facts.isEmpty() || windows >= MAX_WINDOWS) return@readPaged done(fail(SettingsFailure.Reason.MALFORMED))
                    afterId = w.facts.last().id
                    return@readPaged window()
                }
                if (facts.size != w.total || P.crc32(raw.toByteArray()) != revision) return@readPaged done(fail(SettingsFailure.Reason.CRC))
                done(SettingsOutcome.Ok(MemoryList(revision, facts, false)))
            }
        }
        window()
    }

    fun listReminders(done: SettingsDone<P.ReminderDocument>) =
        readPaged({ P.reminderList(it) }, null) { outcome ->
            done(outcome.document().map {
                P.parseReminderDocument(it)?.takeIf { doc -> P.crc32(doc.stableRaw) == doc.revision }
            })
        }

    /** Ok(id to revision). */
    fun addMemory(category: Int, key: String, value: String, done: SettingsDone<Pair<Long, Long>>) =
        mutate(P.memoryAdd(category, key, value), { P.parseIdRevisionBody(it) }, done)

    fun deleteMemory(id: Long, done: SettingsDone<Long>) = mutate(P.memoryDelete(id), { P.parseRevisionBody(it) }, done)
    fun clearMemory(done: SettingsDone<Long>) = mutate(P.memoryClear(), { P.parseRevisionBody(it) }, done)

    fun addReminder(schedule: Int, minute: Int, value: Long, text: String, done: SettingsDone<Pair<Long, Long>>) =
        mutate(P.reminderAdd(schedule, minute, value, text), { P.parseIdRevisionBody(it) }, done)

    fun deleteReminder(id: Long, done: SettingsDone<Long>) = mutate(P.reminderDelete(id), { P.parseRevisionBody(it) }, done)

    /** Kinds 25/26: send one chat line (answer = turn id) and read the robot's reply in chunks. */
    fun chatSend(text: String, done: SettingsDone<Long>) = mutate(P.chatSend(text), { P.parseChatTurn(it) }, done)
    fun chatPoll(turn: Long, offset: Int, done: SettingsDone<P.ChatPoll>) = mutate(P.chatPoll(turn, offset), { P.parseChatPoll(it) }, done)

    /** Kind 20: stored IR profile names (up to 4). */
    fun listIrProfiles(done: SettingsDone<List<String>>) = mutate(P.irProfileList(), { P.parseIrProfiles(it) }, done)

    fun readIntegrations(done: SettingsDone<P.IntegrationsConfig>) =
        readPaged({ if (it == 0) P.integrationsGet() else null }, null) { done(it.document().map { doc -> P.parseIntegrationsConfig(doc) }) }

    fun readIntegrationsStatus(done: SettingsDone<P.IntegrationsStatus>) =
        readPaged({ if (it == 0) P.integrationsStatus() else null }, null) { done(it.document().map { doc -> P.parseIntegrationsStatus(doc) }) }

    /** Writes of the integration config; body is empty on Ok. The token and calendar URL travel only in these requests. */
    fun setIntegrationsConfig(weather: Boolean, latitude: Float, longitude: Float, haBase: String, entities: List<String>, done: SettingsDone<Unit>) =
        mutate(P.integrationsSetConfig(weather, latitude, longitude, haBase, entities), { if (it.isEmpty()) Unit else null }, done)

    fun setIntegrationsToken(token: String, done: SettingsDone<Unit>) =
        mutate(P.integrationsSetToken(token), { if (it.isEmpty()) Unit else null }, done)

    fun setIntegrationsCalendar(url: String, done: SettingsDone<Unit>) =
        mutate(P.integrationsSetCalendar(url), { if (it.isEmpty()) Unit else null }, done)

    fun refreshIntegrations(done: SettingsDone<Unit>) = mutate(P.integrationsRefresh(), { if (it.isEmpty()) Unit else null }, done)
    fun clearIntegrations(done: SettingsDone<Unit>) = mutate(P.integrationsClear(), { if (it.isEmpty()) Unit else null }, done)

    private fun <T> mutate(request: P.Request?, parse: (ByteArray) -> T?, done: SettingsDone<T>) {
        if (request == null) return done(fail(SettingsFailure.Reason.INVALID_REQUEST))
        call(request) { outcome ->
            if (outcome is SettingsOutcome.Fail) return@call done(outcome)
            val value = parse((outcome as SettingsOutcome.Ok).value.body)
            done(if (value == null) fail(SettingsFailure.Reason.MALFORMED) else SettingsOutcome.Ok(value))
        }
    }

    private fun SettingsOutcome<Paged>.document(): SettingsOutcome<ByteArray> = when (this) {
        is SettingsOutcome.Fail -> this
        is SettingsOutcome.Ok -> if (value.complete) SettingsOutcome.Ok(value.data) else fail(SettingsFailure.Reason.MALFORMED)
    }

    private fun <A, B : Any> SettingsOutcome<A>.map(transform: (A) -> B?): SettingsOutcome<B> = when (this) {
        is SettingsOutcome.Fail -> this
        is SettingsOutcome.Ok -> transform(value)?.let { SettingsOutcome.Ok(it) } ?: fail(SettingsFailure.Reason.MALFORMED)
    }

    private fun le32(bytes: ByteArray, offset: Int): Long =
        (bytes[offset].toLong() and 0xff) or ((bytes[offset + 1].toLong() and 0xff) shl 8) or
            ((bytes[offset + 2].toLong() and 0xff) shl 16) or ((bytes[offset + 3].toLong() and 0xff) shl 24)

    private companion object {
        const val MAX_RESTARTS = 3
        const val MAX_WINDOWS = 8
        const val MAX_SET_RESTARTS = 2
    }
}
