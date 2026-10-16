package com.robodesk.phonebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import java.util.zip.CRC32

/**
 * Pure codec for docs/mobile/BLE_SETTINGS_PROTOCOL.md (kinds 8..26, response 0x7d).
 * No Android dependencies. Request payloads can hold secrets: they are never logged
 * (Request.toString hides them) and are wiped with Request.wipe().
 */
object BridgeSettingsProtocol {
    const val KIND_SETTINGS_GET = 8
    const val KIND_SETTINGS_SET = 9
    const val KIND_SECRET_SET = 10
    const val KIND_STATUS_SNAPSHOT = 11
    const val KIND_COMMAND = 12
    const val KIND_MEMORY_LIST = 13
    const val KIND_MEMORY_ADD = 14
    const val KIND_MEMORY_DELETE = 15
    const val KIND_MEMORY_CLEAR = 16
    const val KIND_REMINDER_LIST = 17
    const val KIND_REMINDER_ADD = 18
    const val KIND_REMINDER_DELETE = 19
    const val KIND_IR_PROFILE_LIST = 20
    const val KIND_INTEGRATIONS_GET = 21
    const val KIND_INTEGRATIONS_SET = 22
    const val KIND_INTEGRATIONS_ACTION = 23
    const val KIND_INTEGRATIONS_STATUS = 24
    const val KIND_CHAT_SEND = 25
    const val KIND_CHAT_POLL = 26
    const val KIND_RESPONSE = 0x7d

    const val PAGE_DATA = 480
    const val PAGE_HEADER = 8
    const val MAX_DOCUMENT = 4096
    const val MAX_PAGES = 9
    const val RESPONSE_HEADER = 6
    const val MAX_PAYLOAD = BridgeAuthenticatedEnvelope.MAX_PAYLOAD
    const val SNAPSHOT_SIZE_V1 = 58
    const val SNAPSHOT_SIZE_V2 = 65
    const val SNAPSHOT_SIZE = 76
    const val SNAPSHOT_SIZE_V4 = 79
    const val IR_NAME_MAX = 15
    const val ACTIVITY_ID_MAX = 13

    /** Command op 4 ids and op 11 cue ids = index in these lists (BLE_SETTINGS_PROTOCOL.md section 8.2). */
    val EXPRESSIONS = listOf("happy", "curious", "shy", "love", "sad", "surprised", "thinking", "agree", "disagree", "wink", "laugh", "sleepy",
        "annoyed", "dizzy", "excited", "calm", "bored", "proud", "anxious", "relieved", "mischievous", "lonely")
    val SOUND_CUES = listOf("wake", "happy", "curious", "affection", "thinking", "surprise", "sad", "sleepy", "error", "success", "pickup", "shake")
    const val DEFAULT_TIMEOUT_MS = 5_000L
    const val SLOW_TIMEOUT_MS = 10_000L
    const val MAX_FACTS = 96
    const val MAX_REMINDERS = 8
    const val MAX_FACT_KEY = 39
    const val MAX_FACT_VALUE = 159
    const val MAX_REMINDER_TEXT = 111
    const val MAX_GEMINI_KEY = 96
    const val INT_BASE_MAX = 127
    const val INT_ENTITY_MAX = 47
    const val INT_ENTITIES = 3
    const val INT_TOKEN_MAX = 191
    const val INT_CALENDAR_MAX = 255

    enum class Status(val code: Int, val label: String) {
        OK(0, "Berhasil"),
        BUSY(1, "Robot sedang sibuk, coba lagi"),
        BAD_REQUEST(2, "Permintaan tidak valid"),
        NOT_AUTHORIZED(3, "Sesi berakhir"),
        ROBOT_UNAVAILABLE(4, "Robot (S3) tidak menjawab"),
        SEQUENCE_ERROR(5, "Urutan halaman salah, ulangi dari awal"),
        CRC_MISMATCH(6, "CRC dokumen tidak cocok"),
        INVALID_SETTINGS(7, "Setelan tidak valid"),
        STORAGE_FAILED(8, "Robot gagal menyimpan"),
        NOT_IMPLEMENTED(9, "Update via BLE belum tersedia"),
        CONFIRM_REQUIRED(10, "Konfirmasi diperlukan"),
        UPDATE_BUSY(11, "Update firmware sedang berjalan"),
        UNSUPPORTED(12, "Tidak didukung oleh firmware robot"),
        PAGE_OUT_OF_RANGE(13, "Halaman di luar jangkauan"),
        FORBIDDEN(14, "Perubahan ini tidak diizinkan lewat BLE"),
        TOO_LARGE(15, "Dokumen terlalu besar"),
        NO_MEMORY(16, "Memori robot tidak cukup"),
        CONTENT_REJECTED(17, "Teks ditolak filter privasi robot"),
        FULL(18, "Penyimpanan robot penuh"),
        NOT_FOUND(19, "Item tidak ditemukan"),
        DISABLED(20, "Memori AI dimatikan di setelan robot"),
        TIME_NOT_SET(21, "Jam robot belum disinkronkan"),
        NETWORK_UNAVAILABLE(22, "Robot belum tersambung ke Wi-Fi"),
        REJECTED(23, "Robot menolak: sedang sibuk atau belum dalam keadaan yang tepat");

        companion object {
            fun fromCode(code: Int): Status? = entries.firstOrNull { it.code == code }
        }
    }

    /** One BLE request. Secret requests wipe their buffer after the exchange. */
    class Request(
        val kind: Int,
        payload: ByteArray,
        private val appendSession: Boolean = false,
        val timeoutMs: Long = DEFAULT_TIMEOUT_MS
    ) {
        private val data = payload

        /** Overrides the default scheduling lane (see [RequestPriority]); null = by kind. */
        var priorityHint: Int? = null

        /** Final payload; confirm tokens get the little-endian session id appended. Null if it would exceed 500 bytes. */
        fun payloadFor(sessionId: Long): ByteArray? {
            val extra = if (appendSession) 4 else 0
            if (data.size + extra > MAX_PAYLOAD || (appendSession && sessionId !in 1..UINT32_MAX)) return null
            val out = data.copyOf(data.size + extra)
            if (appendSession) putU32(out, data.size, sessionId)
            return out
        }

        fun wipe() = data.fill(0)

        override fun toString() = "Request(kind=$kind)"
    }

    class Response(val requestId: Long, val kind: Int, val statusCode: Int, val body: ByteArray) {
        val status: Status? get() = Status.fromCode(statusCode)
    }

    class Page(val index: Int, val count: Int, val total: Int, val crc: Long, val data: ByteArray)

    // ---------------------------------------------------------------- requests

    fun settingsGet(page: Int): Request? =
        if (page in 0 until MAX_PAGES) Request(KIND_SETTINGS_GET, byteArrayOf(page.toByte())) else null

    /** Splits a complete document into SettingsSet page requests (send strictly in order, each after the previous response). */
    fun settingsSetPages(document: ByteArray): List<Request>? {
        if (document.isEmpty() || document.size > MAX_DOCUMENT) return null
        val count = pageCount(document.size)
        val crc = crc32(document)
        return (0 until count).map { index ->
            val start = index * PAGE_DATA
            val end = minOf(document.size, start + PAGE_DATA)
            val out = ByteArray(PAGE_HEADER + end - start)
            out[0] = index.toByte()
            out[1] = count.toByte()
            putU16(out, 2, document.size)
            putU32(out, 4, crc)
            document.copyInto(out, PAGE_HEADER, start, end)
            Request(KIND_SETTINGS_SET, out, timeoutMs = if (index == count - 1) SLOW_TIMEOUT_MS else DEFAULT_TIMEOUT_MS)
        }
    }

    /** Wi-Fi slot 0..2. Empty ssid clears the slot; keepPassword needs an ssid and an empty password. */
    fun secretWifi(slot: Int, ssid: String, password: String, keepPassword: Boolean): Request? {
        val s = ssid.toByteArray(Charsets.UTF_8)
        val p = password.toByteArray(Charsets.UTF_8)
        if (slot !in 0..2 || s.size > 32 || p.size > 64 || !printable(s) || !printable(p)) return null
        if (s.isEmpty() && (p.isNotEmpty() || keepPassword)) return null
        if (keepPassword && p.isNotEmpty()) return null
        val out = ByteArray(5 + s.size + p.size)
        out[0] = 1
        out[1] = slot.toByte()
        out[2] = if (keepPassword) 1 else 0
        out[3] = s.size.toByte()
        s.copyInto(out, 4)
        out[4 + s.size] = p.size.toByte()
        p.copyInto(out, 5 + s.size)
        s.fill(0)
        p.fill(0)
        return Request(KIND_SECRET_SET, out)
    }

    /** Empty key clears it. Alphabet A-Z a-z 0-9 - _ . */
    fun secretGemini(key: String): Request? {
        if (key.length > MAX_GEMINI_KEY || !key.all { it in 'A'..'Z' || it in 'a'..'z' || it in '0'..'9' || it == '-' || it == '_' || it == '.' }) return null
        val k = key.toByteArray(Charsets.US_ASCII)
        val out = ByteArray(2 + k.size)
        out[0] = 2
        out[1] = k.size.toByte()
        k.copyInto(out, 2)
        k.fill(0)
        return Request(KIND_SECRET_SET, out)
    }

    fun statusSnapshot() = Request(KIND_STATUS_SNAPSHOT, byteArrayOf(0))

    /** target bitmask: bit0 gateway C3, bit1 robot S3. */
    fun reboot(target: Int): Request? =
        if (target in 1..3) Request(KIND_COMMAND, byteArrayOf(1, target.toByte())) else null

    /** The `WIPE` token carries the current session id, appended when the request is sent. */
    fun factoryReset() = Request(KIND_COMMAND, byteArrayOf(2, 'W'.code.toByte(), 'I'.code.toByte(), 'P'.code.toByte(), 'E'.code.toByte()), appendSession = true)

    fun otaStart() = Request(KIND_COMMAND, byteArrayOf(3))

    // Live controls (command ops 4..13, section 8.2). Ok = accepted by the robot; results come from StatusSnapshot schema 3.
    private fun live(vararg bytes: Int) = Request(KIND_COMMAND, ByteArray(bytes.size) { bytes[it].toByte() })

    fun expression(id: Int): Request? = if (id in EXPRESSIONS.indices) live(4, id) else null
    fun talk() = live(5)

    /** mode 0 resume, 1 pause until resumed, 2 pause 15 min, 3 pause 1 h. */
    fun pauseInitiative(mode: Int): Request? = if (mode in 0..3) live(6, mode) else null

    /** sub 1 start, 2 pause, 3 resume, 4 stop. */
    fun pomodoro(sub: Int): Request? = if (sub in 1..4) live(7, sub) else null

    /** sub 1 start (needs activityId 1..13), 2 pause, 3 resume, 4 cancel, 5 skip, 6 favorite, 7 history-clear. */
    fun activity(sub: Int, activityId: Int = 0): Request? = when {
        sub == 1 -> if (activityId in 1..ACTIVITY_ID_MAX) live(8, 1, activityId) else null
        sub in 2..7 && activityId == 0 -> live(8, sub)
        else -> null
    }

    /** sub 1 accept, 2 cancel. */
    fun game(sub: Int): Request? = if (sub in 1..2) live(9, sub) else null

    /** sub 0 disable, 1 enable, 2 reset learned data. */
    fun learning(sub: Int): Request? = if (sub in 0..2) live(10, sub) else null

    fun soundTest(cue: Int): Request? = if (cue in SOUND_CUES.indices) live(11, cue) else null

    fun irSend(code: Long): Request? =
        if (code in 0..UINT32_MAX) Request(KIND_COMMAND, ByteArray(6).also { it[0] = 12; it[1] = 1; putU32(it, 2, code) }) else null

    /** Name: 1..15 ASCII letters, digits, `_`, `-` or space (same rule as the robot). learn = asynchronous 10 s window. */
    fun irLearn(name: String): Request? = irNamed(2, name)
    fun irReplay(name: String): Request? = irNamed(3, name)

    fun irNameValid(name: String): Boolean =
        name.length in 1..IR_NAME_MAX && name.all { it in 'a'..'z' || it in 'A'..'Z' || it in '0'..'9' || it == '_' || it == '-' || it == ' ' }

    private fun irNamed(sub: Int, name: String): Request? {
        if (!irNameValid(name)) return null
        val bytes = name.toByteArray(Charsets.US_ASCII)
        return Request(KIND_COMMAND, byteArrayOf(12, sub.toByte(), bytes.size.toByte()) + bytes)
    }

    fun experiencesClear() = live(13)

    /** PD-2.5 Tidurkan (true) / Bangunkan (false); PD-3.3 remote pet. The robot rate-limits both (Rejected). */
    fun rest(sleep: Boolean) = live(14, if (sleep) 1 else 2)
    fun remotePet() = live(15)

    fun memoryList(afterId: Long, page: Int): Request? {
        if (afterId !in 0..UINT32_MAX || page !in 0 until MAX_PAGES) return null
        val out = ByteArray(5)
        putU32(out, 0, afterId)
        out[4] = page.toByte()
        return Request(KIND_MEMORY_LIST, out)
    }

    fun memoryAdd(category: Int, key: String, value: String): Request? {
        val k = textBytes(key, MAX_FACT_KEY) ?: return null
        val v = textBytes(value, MAX_FACT_VALUE) ?: return null
        if (category !in 0..5) return null
        val out = ByteArray(3 + k.size + v.size)
        out[0] = category.toByte()
        out[1] = k.size.toByte()
        out[2] = v.size.toByte()
        k.copyInto(out, 3)
        v.copyInto(out, 3 + k.size)
        return Request(KIND_MEMORY_ADD, out, timeoutMs = SLOW_TIMEOUT_MS)
    }

    fun memoryDelete(id: Long): Request? = idRequest(KIND_MEMORY_DELETE, id)

    // ------------------------------------------------------------------ chat (kinds 25/26)

    const val MAX_CHAT_TEXT = 280
    const val MAX_CHAT_REPLY = 640
    private const val CHAT_POLL_HEADER = 11

    /** One line of text: any run of whitespace/control characters becomes a single space. Null when nothing is left or it exceeds [MAX_CHAT_TEXT] bytes. */
    fun chatLine(text: String): String? {
        val line = text.map { if (Character.isISOControl(it) || it.isWhitespace() || it == ' ' || it == '　') ' ' else it }.joinToString("").replace(Regex(" +"), " ").trim()
        return if (line.isEmpty() || line.toByteArray(Charsets.UTF_8).size > MAX_CHAT_TEXT) null else line
    }

    /** `textLength u16, text`. The reply body is the turn id (see [parseChatTurn]). Never retried: a lost answer means "uncertain". */
    fun chatSend(text: String): Request? {
        val line = chatLine(text) ?: return null
        val bytes = textBytes(line, MAX_CHAT_TEXT) ?: return null
        val out = ByteArray(2 + bytes.size)
        putU16(out, 0, bytes.size)
        bytes.copyInto(out, 2)
        return Request(KIND_CHAT_SEND, out)
    }

    /** `turn u32 (0 = latest), offset u16`. */
    fun chatPoll(turn: Long, offset: Int): Request? {
        if (turn !in 0..UINT32_MAX || offset !in 0..MAX_CHAT_REPLY) return null
        val out = ByteArray(6)
        putU32(out, 0, turn)
        putU16(out, 4, offset)
        return Request(KIND_CHAT_POLL, out)
    }

    fun parseChatTurn(body: ByteArray): Long? = if (body.size == 4 && getU32(body, 0) != 0L) getU32(body, 0) else null

    /** state: 0 idle/unknown, 1 queued, 2 thinking, 3 replying, 4 done, 5 failed. fail: 0 none, 1 send, 2 timeout, 3 offline, 4 interrupted. */
    class ChatPoll(val turn: Long, val state: Int, val fail: Int, val total: Int, val offset: Int, val chunk: ByteArray)

    fun parseChatPoll(body: ByteArray): ChatPoll? {
        if (body.size < CHAT_POLL_HEADER) return null
        val chunkLength = body[10].toInt() and 0xff
        val total = getU16(body, 6)
        val offset = getU16(body, 8)
        val state = body[4].toInt() and 0xff
        val fail = body[5].toInt() and 0xff
        if (body.size != CHAT_POLL_HEADER + chunkLength || total > MAX_CHAT_REPLY || offset + chunkLength > total || state > 5 || fail > 4) return null
        return ChatPoll(getU32(body, 0), state, fail, total, offset, body.copyOfRange(CHAT_POLL_HEADER, body.size))
    }

    /** `MCLR` + session id (appended when sent). */
    fun memoryClear() = Request(KIND_MEMORY_CLEAR, "MCLR".toByteArray(Charsets.US_ASCII), appendSession = true, timeoutMs = SLOW_TIMEOUT_MS)

    fun reminderList(page: Int): Request? =
        if (page in 0 until MAX_PAGES) Request(KIND_REMINDER_LIST, byteArrayOf(page.toByte())) else null

    /** schedule 1 once (value = UTC epoch seconds), 2 daily (minute 0..1439), 3 timer (value = 1..86400 s). */
    fun reminderAdd(schedule: Int, minute: Int, value: Long, text: String): Request? {
        val t = textBytes(text, MAX_REMINDER_TEXT) ?: return null
        val valid = when (schedule) {
            1 -> minute == 0 && value in 1..UINT32_MAX
            2 -> minute in 0..1439 && value == 0L
            3 -> minute == 0 && value in 1..86_400
            else -> false
        }
        if (!valid) return null
        val out = ByteArray(8 + t.size)
        out[0] = schedule.toByte()
        putU16(out, 1, minute)
        putU32(out, 3, value)
        out[7] = t.size.toByte()
        t.copyInto(out, 8)
        return Request(KIND_REMINDER_ADD, out, timeoutMs = SLOW_TIMEOUT_MS)
    }

    fun reminderDelete(id: Long): Request? = idRequest(KIND_REMINDER_DELETE, id)

    // ------------------------------------------------- IR profiles and integrations (kinds 20..24)

    fun irProfileList() = Request(KIND_IR_PROFILE_LIST, byteArrayOf(0))
    fun integrationsGet() = Request(KIND_INTEGRATIONS_GET, byteArrayOf(0))
    fun integrationsStatus() = Request(KIND_INTEGRATIONS_STATUS, byteArrayOf(0))

    /** Asynchronous on the robot: Ok = queued. */
    fun integrationsRefresh() = Request(KIND_INTEGRATIONS_ACTION, byteArrayOf(1))

    /** Clears every integration setting and cached value; Busy while a provider request runs. */
    fun integrationsClear() = Request(KIND_INTEGRATIONS_ACTION, byteArrayOf(2))

    /** Type 1: keeps the saved token and calendar URL. Entities are `domain.object`; the robot re-validates (https, syntax). */
    fun integrationsSetConfig(weather: Boolean, latitude: Float, longitude: Float, haBase: String, entities: List<String>): Request? {
        val base = haBase.toByteArray(Charsets.US_ASCII)
        val ents = entities.map { it.toByteArray(Charsets.US_ASCII) }
        if (!(latitude in -90f..90f) || !(longitude in -180f..180f) || haBase.length != base.size || base.size > INT_BASE_MAX || !visibleAscii(base)) return null
        if (ents.size > INT_ENTITIES || ents.any { it.isEmpty() || it.size > INT_ENTITY_MAX || !visibleAscii(it) } || entities.any { it.length != it.toByteArray().size }) return null
        val out = ByteArray(12 + base.size + ents.sumOf { 1 + it.size })
        out[0] = 1
        out[1] = if (weather) 1 else 0
        ByteBuffer.wrap(out, 2, 8).order(ByteOrder.LITTLE_ENDIAN).putFloat(latitude).putFloat(longitude)
        out[10] = base.size.toByte()
        base.copyInto(out, 11)
        var at = 11 + base.size
        out[at++] = ents.size.toByte()
        for (e in ents) { out[at++] = e.size.toByte(); e.copyInto(out, at); at += e.size }
        return Request(KIND_INTEGRATIONS_SET, out, timeoutMs = SLOW_TIMEOUT_MS)
    }

    /** Write-only. Empty clears. Visible ASCII, no spaces, at most 191 bytes. */
    fun integrationsSetToken(token: String): Request? = integrationsSecret(2, token, INT_TOKEN_MAX)

    /** Write-only (a private feed URL is a secret). Empty clears. https is checked by the robot. */
    fun integrationsSetCalendar(url: String): Request? = integrationsSecret(3, url, INT_CALENDAR_MAX)

    private fun integrationsSecret(type: Int, text: String, max: Int): Request? {
        val bytes = text.toByteArray(Charsets.UTF_8)
        if (bytes.size != text.length || bytes.size > max || !visibleAscii(bytes)) { bytes.fill(0); return null }
        val out = ByteArray(2 + bytes.size)
        out[0] = type.toByte()
        out[1] = bytes.size.toByte()
        bytes.copyInto(out, 2)
        bytes.fill(0)
        return Request(KIND_INTEGRATIONS_SET, out, timeoutMs = SLOW_TIMEOUT_MS)
    }

    private fun visibleAscii(bytes: ByteArray) = bytes.all { val b = it.toInt() and 0xff; b in 0x21..0x7e }

    // --------------------------------------------------------------- responses

    /** Decoded 0x7d payload, or null when malformed (bad length or request kind outside 8..26). */
    fun parseResponse(payload: ByteArray): Response? {
        if (payload.size < RESPONSE_HEADER || payload.size > MAX_PAYLOAD) return null
        val kind = payload[4].toInt() and 0xff
        if (kind !in KIND_SETTINGS_GET..KIND_CHAT_POLL) return null
        return Response(getU32(payload, 0), kind, payload[5].toInt() and 0xff, payload.copyOfRange(RESPONSE_HEADER, payload.size))
    }

    /** Page header + data with exact length checks, or null. */
    fun parsePage(body: ByteArray): Page? {
        if (body.size < PAGE_HEADER) return null
        val index = body[0].toInt() and 0xff
        val count = body[1].toInt() and 0xff
        val total = getU16(body, 2)
        if (total !in 1..MAX_DOCUMENT || count != pageCount(total) || index >= count) return null
        val expected = minOf(PAGE_DATA, total - index * PAGE_DATA)
        if (body.size - PAGE_HEADER != expected) return null
        return Page(index, count, total, getU32(body, 4), body.copyOfRange(PAGE_HEADER, body.size))
    }

    /** Reassembles a paged document; pages must arrive in order starting at 0. */
    class PageAssembler {
        enum class Step { NEXT, DONE, RESTART, SEQUENCE_ERROR, CRC_MISMATCH }

        private var buffer: ByteArray? = null
        private var count = 0
        private var crc = 0L
        private var next = 0

        val nextPage: Int get() = next
        var document: ByteArray? = null
            private set

        fun accept(page: Page): Step {
            val current = buffer
            if (page.index == 0) {
                reset()
                buffer = ByteArray(page.total)
                count = page.count
                crc = page.crc
            } else if (current == null) {
                return Step.SEQUENCE_ERROR
            } else if (page.total != current.size || page.count != count || page.crc != crc) {
                reset() // document changed mid-read: caller restarts at page 0
                return Step.RESTART
            } else if (page.index != next) {
                reset()
                return Step.SEQUENCE_ERROR
            }
            val target = buffer ?: return Step.SEQUENCE_ERROR
            page.data.copyInto(target, page.index * PAGE_DATA)
            next = page.index + 1
            if (next < count) return Step.NEXT
            val ok = crc32(target) == crc
            if (!ok) { reset(); return Step.CRC_MISMATCH }
            document = target
            buffer = null
            return Step.DONE
        }

        fun reset() {
            buffer?.fill(0)
            buffer = null
            document = null
            count = 0
            crc = 0L
            next = 0
        }
    }

    /** SettingsSet response body: page echo and applied flag. */
    fun parseSetBody(body: ByteArray): Pair<Int, Boolean>? =
        if (body.size == 2) (body[0].toInt() and 0xff) to (body[1].toInt() == 1) else null

    fun parseRevisionBody(body: ByteArray): Long? = if (body.size == 4) getU32(body, 0) else null

    fun parseIdRevisionBody(body: ByteArray): Pair<Long, Long>? =
        if (body.size == 8) getU32(body, 0) to getU32(body, 4) else null

    /** Firmware version u32 = nomor rilis monoton bertanda tangan (bukan semver); 0 = tidak diketahui. */
    fun formatFirmwareVersion(v: Long): String = if (v <= 0L || v > 0xFFFFFFFFL) "?" else "v$v"

    // --------------------------------------------------------------- documents

    class StatusSnapshot(
        val schema: Int, val c3Version: Long, val s3Version: Long, val c3UptimeMs: Long, val s3UptimeMs: Long,
        val c3HeapFree: Long, val c3HeapMin: Long, val c3LargestBlock: Long, val s3HeapFree: Long, val s3HeapMin: Long,
        val linkFlags: Int, val linkAgeMs: Long, val linkRetries: Long, val linkCrcErrors: Long, val linkDisconnects: Long,
        val wifiState: Int, val wifiRssi: Int, val bleFlags: Int, val configFlags: Int,
        val otaState: Int = 0, val otaError: Int = 0, val otaPercent: Int = 0, val otaTargetVersion: Long = 0,
        // Schema 3 (0 when the robot reports an older schema or the S3 is unreachable).
        val wifiActiveSlot: Int = 0, val pomodoroState: Int = 0, val pomodoroRemainingSec: Int = 0,
        val activityId: Int = 0, val activityState: Int = 0, val gameState: Int = 0, val learningEnabled: Boolean = false,
        val irProfileCount: Int = 0, val irLearnState: Int = 0, val integrationBusy: Boolean = false,
        // Schema 4 (PD-2.4): mood id (MoodText), CharacterMind cause, sleep state (0 = unknown/older robot).
        val moodId: Int = 0, val moodCause: Int = 0, val sleepState: Int = 0
    ) {
        val uartConnected get() = linkFlags and 1 != 0
        val s3Valid get() = linkFlags and 2 != 0
    }

    /** StatusSnapshot schema 2 OTA state (spec section 7.1). */
    enum class OtaState(val code: Int, val label: String) {
        IDLE(0, "Tidak ada update berjalan"),
        CHECKING(1, "Memeriksa rilis bertanda tangan"),
        DOWNLOADING_S3(2, "Menyiapkan unduhan robot (S3)"),
        FLASHING_S3(3, "Mengunduh dan menulis firmware robot (S3)"),
        WAITING_S3_BOOT(4, "Menunggu robot (S3) menyala dengan versi baru"),
        DOWNLOADING_C3(5, "Mengunduh firmware gateway (C3)"),
        DONE(6, "Update selesai"),
        FAILED(7, "Update gagal"),
        UP_TO_DATE(8, "Firmware sudah versi terbaru");

        val active get() = code in 1..5

        companion object {
            fun fromCode(code: Int): OtaState? = entries.firstOrNull { it.code == code }
        }
    }

    fun otaErrorLabel(code: Int): String = when (code) {
        0 -> "-"
        1 -> "Wi-Fi tidak tersambung"
        2 -> "Jam belum sinkron"
        3 -> "Robot sedang sibuk"
        4 -> "Gagal mengunduh dari GitHub"
        5 -> "Manifest rilis tidak valid"
        6 -> "Tanda tangan tidak valid"
        7 -> "Ukuran atau hash image salah"
        8 -> "Robot (S3) menolak image"
        9 -> "Versi rilis S3 dan C3 tidak sama"
        10 -> "Robot (S3) tidak menyala dengan versi baru"
        11 -> "Gagal memasang firmware gateway (C3)"
        12 -> "Gateway (C3) tidak berpindah ke versi baru"
        13 -> "Robot (S3) tidak terhubung"
        255 -> "Kesalahan lain di robot"
        else -> "Kode $code"
    }

    fun parseStatus(document: ByteArray): StatusSnapshot? {
        if (document.size < SNAPSHOT_SIZE_V1) return null
        val schema = document[0].toInt() and 0xff
        // Schema 1 is exactly 58 bytes; schema 2 >= 65 (OTA fields); schema >= 3 >= 76 (live status); later schemas may grow.
        if (schema == 0 || (schema == 1 && document.size != SNAPSHOT_SIZE_V1) || (schema == 2 && document.size < SNAPSHOT_SIZE_V2) ||
            (schema >= 3 && document.size < SNAPSHOT_SIZE) || (schema >= 4 && document.size < SNAPSHOT_SIZE_V4)) return null
        val ota = schema >= 2
        val live = schema >= 3
        fun u8(offset: Int) = if (live) document[offset].toInt() and 0xff else 0
        return StatusSnapshot(
            schema, getU32(document, 1), getU32(document, 5), getU32(document, 9), getU32(document, 13),
            getU32(document, 17), getU32(document, 21), getU32(document, 25), getU32(document, 29), getU32(document, 33),
            document[37].toInt() and 0xff, getU32(document, 38), getU32(document, 42), getU32(document, 46), getU32(document, 50),
            document[54].toInt() and 0xff, document[55].toInt(), document[56].toInt() and 0xff, document[57].toInt() and 0xff,
            if (ota) document[58].toInt() and 0xff else 0, if (ota) document[59].toInt() and 0xff else 0,
            if (ota) minOf(100, document[60].toInt() and 0xff) else 0, if (ota) getU32(document, 61) else 0L,
            u8(65), u8(66), if (live) getU16(document, 67) else 0, u8(69), u8(70), u8(71), u8(72) != 0, u8(73), u8(74), u8(75) != 0,
            if (schema >= 4) u8(76) else 0, if (schema >= 4) u8(77) else 0, if (schema >= 4) u8(78) else 0
        )
    }

    class Fact(
        val id: Long, val updatedEpoch: Long, val day: Int, val category: Int, val flags: Int,
        val key: String, val value: String
    ) {
        val pinned get() = flags and 1 != 0
        val source get() = (flags shr 1) and 3
    }

    /** One MemoryList window. `raw` is the concatenated entry bytes used for the revision CRC. */
    class MemoryWindow(val revision: Long, val total: Int, val more: Boolean, val facts: List<Fact>, val raw: ByteArray)

    fun parseMemoryWindow(document: ByteArray): MemoryWindow? {
        if (document.size < 8 || document[0].toInt() != 1) return null
        val total = document[5].toInt() and 0xff
        val entries = document[6].toInt() and 0xff
        val more = document[7].toInt()
        if (total > MAX_FACTS || entries > total || (more != 0 && more != 1)) return null
        val facts = ArrayList<Fact>(entries)
        var offset = 8
        var lastId = 0L
        repeat(entries) {
            if (offset + 14 > document.size) return null
            val id = getU32(document, offset)
            val category = document[offset + 10].toInt() and 0xff
            val keyLength = document[offset + 12].toInt() and 0xff
            val valueLength = document[offset + 13].toInt() and 0xff
            if (id == 0L || id <= lastId || category > 5 || keyLength !in 1..MAX_FACT_KEY || valueLength !in 1..MAX_FACT_VALUE ||
                offset + 14 + keyLength + valueLength > document.size) return null
            lastId = id
            facts += Fact(
                id, getU32(document, offset + 4), getU16(document, offset + 8), category, document[offset + 11].toInt() and 0xff,
                plainText(document, offset + 14, keyLength), plainText(document, offset + 14 + keyLength, valueLength)
            )
            offset += 14 + keyLength + valueLength
        }
        if (offset != document.size) return null
        return MemoryWindow(getU32(document, 1), total, more == 1, facts, document.copyOfRange(8, offset))
    }

    /** Local-cache form of a fact list: the MemoryList document layout in one window (more = 0). Null if a text no longer fits. */
    fun encodeMemoryDocument(revision: Long, facts: List<Fact>): ByteArray? {
        if (facts.size > MAX_FACTS) return null
        val out = java.io.ByteArrayOutputStream()
        val head = ByteArray(8)
        head[0] = 1
        putU32(head, 1, revision)
        head[5] = facts.size.toByte()
        head[6] = facts.size.toByte()
        out.write(head)
        for (fact in facts) {
            val k = textBytes(fact.key, MAX_FACT_KEY) ?: return null
            val v = textBytes(fact.value, MAX_FACT_VALUE) ?: return null
            val entry = ByteArray(14)
            putU32(entry, 0, fact.id)
            putU32(entry, 4, fact.updatedEpoch)
            putU16(entry, 8, fact.day)
            entry[10] = fact.category.toByte()
            entry[11] = fact.flags.toByte()
            entry[12] = k.size.toByte()
            entry[13] = v.size.toByte()
            out.write(entry)
            out.write(k)
            out.write(v)
        }
        return out.toByteArray()
    }

    class Reminder(val id: Long, val schedule: Int, val minute: Int, val due: Long, val text: String)

    /** `stableRaw` has timer countdowns zeroed, matching the revision CRC definition. */
    class ReminderDocument(val revision: Long, val reminders: List<Reminder>, val stableRaw: ByteArray)

    fun parseReminderDocument(document: ByteArray): ReminderDocument? {
        if (document.size < 6 || document[0].toInt() != 1) return null
        val count = document[5].toInt() and 0xff
        if (count > MAX_REMINDERS) return null
        val list = ArrayList<Reminder>(count)
        val stable = document.copyOfRange(6, document.size)
        var offset = 6
        var lastId = 0L
        repeat(count) {
            if (offset + 12 > document.size) return null
            val id = getU32(document, offset)
            val schedule = document[offset + 4].toInt() and 0xff
            val minute = getU16(document, offset + 5)
            val textLength = document[offset + 11].toInt() and 0xff
            if (id == 0L || id <= lastId || schedule !in 1..3 || minute > 1439 || textLength !in 1..MAX_REMINDER_TEXT ||
                offset + 12 + textLength > document.size) return null
            lastId = id
            list += Reminder(id, schedule, minute, getU32(document, offset + 7), plainText(document, offset + 12, textLength))
            if (schedule == 3) stable.fill(0, offset - 6 + 7, offset - 6 + 11)
            offset += 12 + textLength
        }
        if (offset != document.size) return null
        return ReminderDocument(getU32(document, 1), list, stable)
    }

    // -------------------------------------- IR profile / integrations response bodies

    /** Kind 20 body: count (0..4), then len + name per profile. Names are untrusted display text. */
    fun parseIrProfiles(body: ByteArray): List<String>? {
        if (body.isEmpty()) return null
        val count = body[0].toInt() and 0xff
        if (count > 4) return null
        var at = 1
        val names = ArrayList<String>()
        repeat(count) {
            if (at >= body.size) return null
            val len = body[at++].toInt() and 0xff
            if (len !in 1..IR_NAME_MAX || at + len > body.size) return null
            val raw = body.copyOfRange(at, at + len)
            if (!irNameValid(String(raw, Charsets.US_ASCII))) return null
            names += String(raw, Charsets.US_ASCII)
            at += len
        }
        return if (at == body.size) names else null
    }

    /** Kind 21 document. The token and calendar URL are never returned, only whether they are set. */
    class IntegrationsConfig(
        val weatherEnabled: Boolean, val tokenConfigured: Boolean, val calendarConfigured: Boolean,
        val latitude: Float, val longitude: Float, val haBase: String, val entities: List<String>
    ) {
        override fun toString() = "IntegrationsConfig(entities=${entities.size})"
    }

    fun parseIntegrationsConfig(document: ByteArray): IntegrationsConfig? {
        if (document.size < 12 || document[0].toInt() != 1) return null
        val flags = document[1].toInt() and 0xff
        if (flags and 7.inv() != 0) return null
        val buffer = ByteBuffer.wrap(document, 2, 8).order(ByteOrder.LITTLE_ENDIAN)
        val lat = buffer.float
        val lon = buffer.float
        val baseLen = document[10].toInt() and 0xff
        if (baseLen > INT_BASE_MAX || 11 + baseLen >= document.size) return null
        val base = String(document, 11, baseLen, Charsets.US_ASCII)
        var at = 11 + baseLen
        val count = document[at++].toInt() and 0xff
        if (count > INT_ENTITIES) return null
        val entities = ArrayList<String>()
        repeat(count) {
            if (at >= document.size) return null
            val len = document[at++].toInt() and 0xff
            if (len !in 1..INT_ENTITY_MAX || at + len > document.size) return null
            entities += String(document, at, len, Charsets.US_ASCII)
            at += len
        }
        if (at != document.size || !(lat in -90f..90f) || !(lon in -180f..180f)) return null
        return IntegrationsConfig(flags and 1 != 0, flags and 2 != 0, flags and 4 != 0, lat, lon, base, entities)
    }

    class IntegrationEntity(val slot: Int, val valid: Boolean, val ageMs: Long, val state: String)

    /** Error codes are the robot's: 0 ok, 1 client_init, 2 response_too_large, 3 tls_or_network, 4 authorization_rejected, 5 provider_rejected, 6 http_status, 7 invalid_response, 8 waiting_for_clock. */
    class IntegrationsStatus(
        val busy: Boolean, val weatherConfigured: Boolean, val weatherValid: Boolean, val homeAssistantConfigured: Boolean,
        val calendarConfigured: Boolean, val calendarValid: Boolean,
        val weatherError: Int, val homeAssistantError: Int, val calendarError: Int,
        val weatherAgeMs: Long, val weather: String, val entities: List<IntegrationEntity>, val calendarAgeMs: Long, val calendar: String
    )

    fun parseIntegrationsStatus(document: ByteArray): IntegrationsStatus? {
        if (document.size < 5 + 5 + 1 + 5 || document[0].toInt() != 1) return null
        val flags = document[1].toInt() and 0xff
        if (flags and 63.inv() != 0) return null
        var at = 5
        fun u8(): Int = document[at++].toInt() and 0xff
        fun u32(): Long { if (at + 4 > document.size) return -1; val v = getU32(document, at); at += 4; return v }
        fun text(max: Int): String? {
            if (at >= document.size) return null
            val len = u8()
            if (len > max || at + len > document.size) return null
            return plainText(document, at, len).also { at += len }
        }
        val weatherAge = u32().takeIf { it >= 0 } ?: return null
        val weather = text(95) ?: return null
        if (at >= document.size) return null
        val count = u8()
        if (count > INT_ENTITIES) return null
        val entities = ArrayList<IntegrationEntity>()
        repeat(count) {
            if (at + 2 > document.size) return null
            val slot = u8()
            val valid = u8()
            val age = u32().takeIf { it >= 0 } ?: return null
            val state = text(24) ?: return null
            if (slot >= INT_ENTITIES || valid > 1) return null
            entities += IntegrationEntity(slot, valid == 1, age, state)
        }
        val calendarAge = u32().takeIf { it >= 0 } ?: return null
        val calendar = text(219) ?: return null
        if (at != document.size) return null
        return IntegrationsStatus(
            flags and 1 != 0, flags and 2 != 0, flags and 4 != 0, flags and 8 != 0, flags and 16 != 0, flags and 32 != 0,
            document[2].toInt() and 0xff, document[3].toInt() and 0xff, document[4].toInt() and 0xff,
            weatherAge, weather, entities, calendarAge, calendar
        )
    }

    // ----------------------------------------------------------------- helpers

    fun pageCount(total: Int) = (total + PAGE_DATA - 1) / PAGE_DATA

    fun crc32(bytes: ByteArray): Long = CRC32().also { it.update(bytes) }.value

    /** Robot-supplied text is untrusted: control characters and line separators become U+FFFD, nothing is interpreted. */
    fun plainText(bytes: ByteArray, offset: Int, length: Int): String {
        val text = String(bytes, offset, length, Charsets.UTF_8)
        val out = StringBuilder(text.length)
        for (ch in text) out.append(if (Character.isISOControl(ch) || ch == ' ' || ch == ' ') '�' else ch)
        return out.toString()
    }

    /** Strict UTF-8, no C0/C1/DEL/U+2028/U+2029, at least one non-space byte, 1..maxBytes bytes. */
    fun textBytes(text: String, maxBytes: Int): ByteArray? {
        val bytes = text.toByteArray(Charsets.UTF_8)
        if (bytes.isEmpty() || bytes.size > maxBytes || bytes.all { it == 0x20.toByte() }) return null
        val decoded = try {
            Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
        } catch (_: CharacterCodingException) { return null }
        if (decoded.any { Character.isISOControl(it) || it == ' ' || it == ' ' }) return null
        return bytes
    }

    private fun printable(bytes: ByteArray) = bytes.all { val b = it.toInt() and 0xff; b >= 0x20 && b != 0x7f }

    private fun idRequest(kind: Int, id: Long): Request? {
        if (id !in 1..UINT32_MAX) return null
        val out = ByteArray(4)
        putU32(out, 0, id)
        return Request(kind, out, timeoutMs = SLOW_TIMEOUT_MS)
    }

    private const val UINT32_MAX = 0xffff_ffffL

    private fun putU16(bytes: ByteArray, offset: Int, value: Int) {
        bytes[offset] = value.toByte()
        bytes[offset + 1] = (value ushr 8).toByte()
    }

    private fun putU32(bytes: ByteArray, offset: Int, value: Long) {
        ByteBuffer.wrap(bytes, offset, 4).order(ByteOrder.LITTLE_ENDIAN).putInt(value.toInt())
    }

    private fun getU16(bytes: ByteArray, offset: Int) =
        (bytes[offset].toInt() and 0xff) or ((bytes[offset + 1].toInt() and 0xff) shl 8)

    private fun getU32(bytes: ByteArray, offset: Int): Long =
        ByteBuffer.wrap(bytes, offset, 4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and UINT32_MAX
}
