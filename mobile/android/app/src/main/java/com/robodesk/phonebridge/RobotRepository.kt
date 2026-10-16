package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

/** Result of a repository call: a value, or an Indonesian UI message plus whether the robot state is now unknown. */
sealed class Res<out T> {
    class Ok<T>(val value: T) : Res<T>()
    class Err(val message: String, val uncertain: Boolean = false, val status: P.Status? = null) : Res<Nothing>()
}

typealias Done<T> = (Res<T>) -> Unit

/** Maps transport/robot failures (A10) to UI text. No request content is ever included. */
object UiErrors {
    fun message(f: SettingsFailure, hint: String? = null): String = when (f.reason) {
        SettingsFailure.Reason.TIMEOUT -> "Tidak ada respons dari robot"
        SettingsFailure.Reason.NOT_CONNECTED -> "Robot belum terhubung · menyambung ulang…"
        SettingsFailure.Reason.UNCERTAIN -> "Hasil tidak pasti · baca ulang dari robot sebelum mengulang"
        SettingsFailure.Reason.INVALID_REQUEST -> hint ?: "Input tidak valid"
        SettingsFailure.Reason.STATUS -> statusMessage(f.status, f.statusCode)
        else -> f.message
    }

    fun statusMessage(s: P.Status?, code: Int): String = when (s) {
        null -> "Status robot tidak dikenal ($code)"
        P.Status.BUSY -> "Robot sibuk, coba lagi sebentar"
        P.Status.ROBOT_UNAVAILABLE -> "Robot (S3) tidak menjawab"
        P.Status.SEQUENCE_ERROR, P.Status.CRC_MISMATCH -> "Transfer terganggu, coba lagi"
        P.Status.INVALID_SETTINGS -> "Robot menolak setelan (nilai di luar batas)"
        P.Status.STORAGE_FAILED -> "Robot gagal menyimpan"
        P.Status.UPDATE_BUSY -> "Update firmware sedang berjalan"
        P.Status.CONFIRM_REQUIRED -> "Konfirmasi diperlukan"
        P.Status.FORBIDDEN -> "Perubahan ini tidak diizinkan lewat BLE"
        P.Status.TOO_LARGE -> "Dokumen terlalu besar"
        P.Status.CONTENT_REJECTED -> "Ditolak filter privasi robot (kredensial / kesehatan / deret angka panjang). Filter bersifat konservatif, bukan jaminan."
        P.Status.FULL -> "Penuh (maks ${P.MAX_FACTS} fakta / ${P.MAX_REMINDERS} pengingat)"
        P.Status.TIME_NOT_SET -> "Jam robot belum sinkron · sambungkan Wi-Fi robot dulu"
        P.Status.NETWORK_UNAVAILABLE -> "Robot belum tersambung ke Wi-Fi"
        P.Status.DISABLED -> "Memori AI dimatikan di setelan robot"
        P.Status.REJECTED -> "Robot menolak: sedang sibuk atau belum dalam keadaan yang tepat"
        else -> s.label
    }
}

/** Local side of "forget robot": the live link (authenticated revoke) and the local credential wipe. */
interface ForgetPort {
    /** Starts the authenticated revoke on the robot; false when the robot cannot be reached right now. */
    fun requestRemoteForget(): Boolean
    /** Removes bond data, credentials and caches on this phone. */
    fun clearLocal(): Boolean
}

class ForgetOutcome(val robotRevoking: Boolean, val localCleared: Boolean) {
    val message: String
        get() = when {
            !localCleared -> "Tidak semua data lokal bisa diverifikasi terhapus · periksa pengaturan aplikasi"
            robotRevoking -> "Robot mencabut kepercayaan lalu data HP dihapus"
            else -> "Data HP dihapus, tetapi robot di luar jangkauan masih mempercayai HP ini sampai dipasangkan ulang atau reset pabrik"
        }
}

/** PD-2.4 live mood line for Home, e.g. "Ngantuk · sudah malam". Ids mirror PhoneSettingsProtocol.h (MoodIds/MindCause/sleep). */
object MoodText {
    private val MOODS = listOf("", "Tenang", "Ceria", "Penasaran", "Sayang", "Gelisah", "Murung", "Ngantuk", "Senang", "Kaget", "Ngambek")
    private val CAUSES = listOf("", "ada kamu", "baru dielus", "habis ngobrol", "baru bangun", "diangkat", "ditaruh lagi",
        "berhasil", "gagal", "lama sendirian", "ditenangkan", "disentuh terus")
    private val SLEEP = listOf("", "", "Ngantuk", "Tidur", "Tidur nyenyak", "Baru bangun")

    /** [hour] = phone local hour 0..23, used only to explain sleepiness. Null when the robot reports no mood. */
    fun describe(moodId: Int, cause: Int, sleepState: Int, hour: Int): String? {
        val sleep = SLEEP.getOrElse(sleepState) { "" }
        val mood = sleep.ifEmpty { MOODS.getOrElse(moodId) { "" } }
        if (mood.isEmpty()) return null
        val night = hour >= 21 || hour < 6
        val reason = CAUSES.getOrElse(cause) { "" }.ifEmpty { if ((moodId == 7 || sleepState in 2..4) && night) "sudah malam" else "" }
        return if (reason.isEmpty()) mood else "$mood · $reason"
    }
}

/** PD-3.4 opt-in local mood diary: one entry per hour (mood id + sleep state), never a transcript, at most 7 days. */
object MoodDiary {
    const val MAX_HOURS = 24 * 7
    data class Entry(val hour: Long, val moodId: Int, val sleepState: Int)

    /** Adds the status sample taken at [epochSec]; a later sample in the same hour replaces the earlier one. */
    fun record(entries: List<Entry>, epochSec: Long, moodId: Int, sleepState: Int): List<Entry> {
        if (epochSec <= 0 || (moodId !in 1..10 && sleepState !in 1..5)) return entries
        val hour = epochSec / 3600
        val kept = entries.filter { it.hour != hour && it.hour > hour - MAX_HOURS }
        return (kept + Entry(hour, moodId.coerceIn(0, 10), sleepState.coerceIn(0, 5))).sortedBy { it.hour }
    }

    fun encode(entries: List<Entry>) = entries.joinToString(";") { "${it.hour}:${it.moodId}:${it.sleepState}" }

    fun decode(text: String?): List<Entry> = text.orEmpty().split(';').mapNotNull { part ->
        val f = part.split(':')
        val h = f.getOrNull(0)?.toLongOrNull(); val m = f.getOrNull(1)?.toIntOrNull(); val s = f.getOrNull(2)?.toIntOrNull()
        if (f.size != 3 || h == null || m == null || s == null || h <= 0 || m !in 0..10 || s !in 0..5) null else Entry(h, m, s)
    }.sortedBy { it.hour }.takeLast(MAX_HOURS)

    /** (local day index, "Ceria 5 jam, Ngantuk 3 jam"), newest day first. [zoneOffsetSec] = phone UTC offset. */
    fun days(entries: List<Entry>, zoneOffsetSec: Long): List<Pair<Long, String>> =
        entries.groupBy { Math.floorDiv(it.hour * 3600 + zoneOffsetSec, 86400L) }.entries.sortedByDescending { it.key }.map { (day, list) ->
            day to list.groupingBy { MoodText.describe(it.moodId, 0, it.sleepState, 12).orEmpty() }.eachCount()
                .filterKeys { it.isNotEmpty() }.entries.sortedByDescending { it.value }.take(3).joinToString(", ") { "${it.key} ${it.value} jam" }
        }
}

/** PD-4.16 user-owned Gemini key (owner decision PD-4.0): paste clean-up and friendly checks before sending. */
object GeminiKeyHelp {
    const val KEY_PAGE = "https://aistudio.google.com/apikey"
    const val STEPS = "1. Buka halaman kunci Google AI Studio dan masuk dengan akun Google Anda.\n" +
        "2. Pilih Create API key, lalu salin kuncinya.\n" +
        "3. Tempel di sini dan kirim. Kunci disimpan di robot, bukan di server kami; kuota dan biaya mengikuti akun Google Anda."

    /** Removes whitespace/newlines that come with copy-paste. */
    fun normalize(raw: String): String = raw.filterNot { it.isWhitespace() }

    /** Null = looks fine; otherwise a short Indonesian hint. Blocking problems start with "!". */
    fun check(raw: String): String? {
        val key = normalize(raw)
        return when {
            key.isEmpty() -> "!Tempel kunci dulu"
            key.length > P.MAX_GEMINI_KEY || !key.all { it in 'A'..'Z' || it in 'a'..'z' || it in '0'..'9' || it == '-' || it == '_' || it == '.' } ->
                "!Kunci hanya berisi huruf, angka, - _ . (maks ${P.MAX_GEMINI_KEY})"
            !key.startsWith("AIza") -> "Kunci Gemini biasanya diawali AIza. Pastikan yang disalin kunci API, bukan nama proyek."
            key.length != 39 -> "Panjang kunci Gemini biasanya 39 karakter (ini ${key.length}). Periksa apakah tersalin utuh."
            else -> null
        }
    }
}

/** PD-4.8 prominent disclosure shown before Android's notification-access settings. */
const val NOTIFICATION_ACCESS_DISCLOSURE = "Aplikasi membaca notifikasi HP hanya dari aplikasi yang Anda pilih, lalu mengirim judul dan cuplikan " +
    "singkatnya ke robot lewat Bluetooth terenkripsi agar robot bisa menampilkan atau membacakannya. Kode OTP, kata sandi, dan data sensitif " +
    "disaring di HP dan tidak dikirim. Tidak ada notifikasi yang dikirim ke server mana pun. Akses bisa dicabut kapan saja di setelan Android."

/** PD-4.14 privacy indicators: one wording for mic and cloud state everywhere in the app. */
object PrivacyState {
    const val CONSENT_TEXT = "Saat percakapan aktif, suara dan teks yang Anda kirim diteruskan langsung dari robot ke Google Gemini memakai kunci Anda " +
        "dan diatur oleh ketentuan Google untuk akun Anda. Kami tidak menyimpan atau menerima audio Anda. Mikrofon bisa dimatikan kapan saja, " +
        "dan persetujuan ini bisa dicabut di Setelan › Privasi."
    fun mic(muted: Boolean) = if (muted) "Mikrofon mati" else "Mikrofon aktif"
    /** null consent = settings not loaded yet. */
    fun cloud(keyInstalled: Boolean?, consent: Boolean?): String = when {
        keyInstalled == false -> "Tanpa AI cloud (belum ada kunci)"
        consent == false -> "AI cloud mati (belum disetujui)"
        keyInstalled == true && consent == true -> "AI cloud aktif (Gemini)"
        else -> "-"
    }
}

/** PD-4.3 "birth" onboarding: the name the owner gives the robot (settings robotName, max 31). */
object RobotNaming {
    const val DEFAULT = "AILI"
    /** Trimmed name, or null when empty, too long or containing control characters. */
    fun clean(raw: String): String? {
        val name = raw.trim()
        return if (name.isEmpty() || name.length > 31 || name.any { it.isISOControl() }) null else name
    }
}

/** PD-4.2 Kenangan: companion age and milestones from the robot's read-only settings keys. */
object Milestones {
    val DAYS = listOf(7, 30, 100, 365)
    fun daysTogether(firstEpoch: Long, nowEpoch: Long): Int =
        if (firstEpoch <= 0 || nowEpoch < firstEpoch) 0 else ((nowEpoch - firstEpoch) / 86400L).toInt()
    /** The next milestone day still ahead, or null after a year. */
    fun next(days: Int): Int? = DAYS.firstOrNull { it > days }
    /** Milestone days the robot already celebrated (bit i = DAYS[i]). */
    fun celebrated(mask: Int): List<Int> = DAYS.filterIndexed { i, _ -> mask and (1 shl i) != 0 }
}

/** Status numbers prepared for the Home/Diagnostics screens. */
class StatusView(val s: P.StatusSnapshot) {
    fun moodText(hour: Int): String? = MoodText.describe(s.moodId, s.moodCause, s.sleepState, hour)
    val wifiConnected get() = s.wifiState and 1 != 0
    val geminiKey get() = s.configFlags and 1 != 0
    val wifiSlotConfigured get() = s.configFlags and 14 != 0
    fun slotConfigured(slot: Int) = s.configFlags and (1 shl slot) != 0 // slot 1..3 = bits 1..3
    val pairingWindowOpen get() = s.bleFlags and 4 != 0
    val c3Version get() = P.formatFirmwareVersion(s.c3Version)
    val s3Version get() = if (s.s3Valid) P.formatFirmwareVersion(s.s3Version) else "-"
    val linkText get() = if (s.uartConnected) "Tersambung · ${s.linkAgeMs} ms" else "Terputus"
    val wifiText get() = if (!wifiConnected) "Tidak tersambung" else (if (s.wifiActiveSlot in 1..3) "Slot ${s.wifiActiveSlot} · " else "") + "${s.wifiRssi} dBm"
    val uptimeText get() = uptime(s.c3UptimeMs)
    val heapText get() = "${s.c3HeapFree / 1024} / ${if (s.s3Valid) (s.s3HeapFree / 1024).toString() else "-"} KiB"
    val pomodoroText: String
        get() = when (s.pomodoroState) {
            0 -> "berhenti"
            4 -> "dijeda"
            else -> (if (s.pomodoroState == 1) "fokus " else "istirahat ") + "%02d:%02d".format(s.pomodoroRemainingSec / 60, s.pomodoroRemainingSec % 60)
        }

    companion object {
        fun uptime(ms: Long): String {
            val m = ms / 60000
            return if (m >= 60) "${m / 60} j ${m % 60} m" else "$m m"
        }
    }
}

/** Which live-control buttons make sense for the current snapshot (null snapshot = nothing known, controls stay usable). */
class LiveAvailability(s: P.StatusSnapshot?) {
    val pomodoroStart = s == null || s.pomodoroState == 0
    val pomodoroPause = s == null || s.pomodoroState in 1..3
    val pomodoroResume = s == null || s.pomodoroState == 4
    val pomodoroStop = s == null || s.pomodoroState != 0
    val activityRunning = s != null && s.activityState == 1
    val activityPaused = s != null && s.activityState == 2
    val activityAny = activityRunning || activityPaused
    val gameAccept = s != null && s.gameState == 1
    val gameCancel = s != null && s.gameState in 1..2
    val irLearning = s != null && s.irLearnState == 1
    val integrationBusy = s != null && s.integrationBusy
}

/** OTA outcome after reconnect (GAP-D). */
class OtaResult(val kind: Kind, val message: String) {
    enum class Kind { RUNNING, DONE, UP_TO_DATE, FAILED, IDLE }
}

/**
 * One function per row of the feature matrix (ANDROID_ROBOT_INTEGRATION.md section 1, rows 1..21 plus live controls,
 * integrations, IR, sound test, experiences). Everything goes through [BridgeSettingsClient] and so through the one scheduler.
 */
class RobotRepository(private val client: BridgeSettingsClient, private val nowSec: () -> Long = { System.currentTimeMillis() / 1000 }) {
    private fun <T, R> map(done: Done<R>, ok: (T) -> Res<R>): SettingsDone<T> = { o ->
        when (o) {
            is SettingsOutcome.Ok -> done(ok(o.value))
            is SettingsOutcome.Fail -> done(Res.Err(UiErrors.message(o.failure), o.failure.reason == SettingsFailure.Reason.UNCERTAIN, o.failure.status))
        }
    }

    private fun <T> passthrough(done: Done<T>): SettingsDone<T> = map<T, T>(done) { Res.Ok(it) }

    private fun err(f: SettingsFailure) = Res.Err(UiErrors.message(f), f.reason == SettingsFailure.Reason.UNCERTAIN, f.status)

    private fun invalid(message: String): Res.Err = Res.Err(message)

    private fun command(request: P.Request?, bad: String, done: Done<Unit>) {
        if (request == null) return done(invalid(bad))
        client.call(request, map<P.Response, Unit>(done) { Res.Ok(Unit) })
    }

    // Rows 2-6, 13, 17: status
    fun readStatus(done: Done<StatusView>) = client.readStatus(map<P.StatusSnapshot, StatusView>(done) { Res.Ok(StatusView(it)) })

    // Rows 1, 7, 8, 10: settings (name, mic/sound switches, all groups, navigation)
    fun loadSettings(done: Done<RobotSettings>) = client.readSettings(map<ByteArray, RobotSettings>(done) { bytes ->
        RobotSettings.parse(bytes)?.let { Res.Ok(it) } ?: Res.Err("Dokumen setelan dari robot tidak valid")
    })

    fun robotName(done: Done<String>) = loadSettings { r -> done(when (r) { is Res.Ok -> Res.Ok(r.value.string("robotName")); is Res.Err -> r }) }

    /** True when the robot reports it is registered for iPhone: the app cannot change that (I0). */
    fun isIphoneRobot(s: RobotSettings) = s.int("phonePlatform") == 1

    class SaveResult(val reread: RobotSettings, val differing: List<String>)

    /**
     * Validate -> write -> read back -> compare. A lost answer is UNCERTAIN: the caller keeps its local edits and re-reads.
     * [differing] lists keys whose stored value is not what was sent (the robot may normalise a value).
     */
    fun saveSettings(next: RobotSettings, done: Done<SaveResult>) {
        val errors = next.validate()
        if (errors.isNotEmpty()) return done(invalid(errors.values.first()))
        val sent = next.toBytes()
        client.writeSettings(sent) { outcome ->
            if (outcome is SettingsOutcome.Fail) return@writeSettings done(err(outcome.failure))
            loadSettings { r ->
                when (r) {
                    is Res.Err -> done(Res.Err("Tersimpan, tetapi pembacaan ulang gagal: ${r.message}", uncertain = true))
                    is Res.Ok -> done(Res.Ok(SaveResult(r.value, next.sameAs(r.value, RobotSettings.CONTROLLED_KEYS.filter { next.has(it) }))))
                }
            }
        }
    }

    // Row 9: allowlists (inside the settings document)
    fun allowlists(s: RobotSettings) = Allowlist.from(s)

    // Row 11: Wi-Fi
    fun sendWifi(slot: Int, ssid: String, password: String, keepPassword: Boolean, done: Done<Unit>) {
        if (ssid.isEmpty()) { return done(invalid("SSID wajib diisi")) }
        command(P.secretWifi(slot, ssid, password, keepPassword), "SSID maks 32 byte, sandi maks 64 byte, tanpa karakter kontrol", done)
    }
    fun clearWifi(slot: Int, done: Done<Unit>) = command(P.secretWifi(slot, "", "", false), "Slot tidak valid", done)

    // Row 12: Gemini key
    fun sendGeminiKey(raw: String, done: Done<Unit>) {
        val key = GeminiKeyHelp.normalize(raw)
        if (key.isEmpty()) return done(invalid("Kunci kosong"))
        command(P.secretGemini(key), "Kunci hanya boleh A-Z a-z 0-9 - _ . (maks 96)", done)
    }
    fun clearGeminiKey(done: Done<Unit>) = command(P.secretGemini(""), "Kunci tidak valid", done)

    // Chat with the robot (kinds 25/26)
    private fun chatErr(r: Res.Err): Res.Err = when (r.status) {
        P.Status.ROBOT_UNAVAILABLE -> Res.Err("Robot belum terhubung ke Gemini · pastikan Wi-Fi robot dan kunci Gemini aktif (perintah seperti /status tetap bisa)", r.uncertain, r.status)
        P.Status.BUSY -> Res.Err("Robot masih mengerjakan pesan sebelumnya", r.uncertain, r.status)
        else -> r
    }
    fun chatSend(text: String, done: Done<Long>) {
        if (P.chatLine(text) == null) return done(invalid("Tulis pesan 1-${P.MAX_CHAT_TEXT} byte"))
        client.chatSend(text, map<Long, Long>({ r -> done(if (r is Res.Err) chatErr(r) else r) }) { Res.Ok(it) })
    }
    fun chatPoll(turn: Long, offset: Int, done: Done<P.ChatPoll>) = client.chatPoll(turn, offset, passthrough(done))

    // Row 14: forget (GAP-C): authenticated revoke first, local wipe regardless
    fun forgetRobot(port: ForgetPort): ForgetOutcome {
        val remote = port.requestRemoteForget()
        val local = port.clearLocal()
        return ForgetOutcome(remote, local)
    }

    // Row 15: memory
    class MemoryView(val revision: Long, val facts: List<P.Fact>, val fromCache: Boolean)

    fun loadMemory(cachedRevision: Long?, cached: List<P.Fact>, done: Done<MemoryView>) =
        client.listMemory(cachedRevision, map<BridgeSettingsClient.MemoryList, MemoryView>(done) {
            if (it.unchanged) Res.Ok(MemoryView(cachedRevision ?: it.revision, cached, true)) else Res.Ok(MemoryView(it.revision, it.facts, false))
        })

    fun addMemory(category: Int, key: String, value: String, done: Done<Pair<Long, Long>>) {
        if (P.memoryAdd(category, key, value) == null) return done(invalid("Kunci 1-39 byte, nilai 1-159 byte, tanpa karakter kontrol"))
        client.addMemory(category, key, value, passthrough(done))
    }
    fun deleteMemory(id: Long, done: Done<Long>) = client.deleteMemory(id, passthrough(done))
    fun clearMemory(done: Done<Long>) = client.clearMemory(passthrough(done))

    // Row 16: reminders. kind: 1 once, 2 daily, 3 timer. For ONCE, [value] is the epoch second and must be in the future.
    fun loadReminders(done: Done<P.ReminderDocument>) = client.listReminders(passthrough(done))

    fun addReminder(schedule: Int, minute: Int, value: Long, text: String, current: Int, done: Done<Pair<Long, Long>>) {
        if (current >= P.MAX_REMINDERS) return done(invalid("Penuh (maks ${P.MAX_REMINDERS})"))
        if (schedule == 1 && value <= nowSec()) return done(invalid("Waktu pengingat harus di masa depan"))
        if (P.reminderAdd(schedule, minute, value, text) == null) return done(invalid("Teks 1-111 byte dan waktu harus valid"))
        client.addReminder(schedule, minute, value, text, passthrough(done))
    }
    fun deleteReminder(id: Long, done: Done<Long>) = client.deleteReminder(id, passthrough(done))

    // Row 19-21: OTA, reboot, factory reset
    fun startOta(done: Done<Unit>) = command(P.otaStart(), "", done)
    /** Rebooting the C3 drops BLE before (or right after) the answer: a lost answer is the expected outcome, not an error (I5). */
    fun reboot(s3: Boolean, c3: Boolean, done: Done<Unit>) = command(P.reboot((if (c3) 1 else 0) or (if (s3) 2 else 0)), "Pilih minimal satu target") { r ->
        done(if (c3 && r is Res.Err && r.uncertain) Res.Ok(Unit) else r)
    }
    fun factoryReset(typed: String, done: Done<Unit>) {
        if (typed != "WIPE") return done(invalid("Ketik WIPE untuk melanjutkan"))
        command(P.factoryReset(), "", done)
    }

    /** Interprets a snapshot read after reconnect. */
    fun otaResult(s: P.StatusSnapshot): OtaResult {
        val state = P.OtaState.fromCode(s.otaState)
        return when {
            state == null || state == P.OtaState.IDLE -> OtaResult(OtaResult.Kind.IDLE, "Tidak ada update berjalan")
            state.active -> OtaResult(OtaResult.Kind.RUNNING, state.label)
            state == P.OtaState.DONE -> OtaResult(OtaResult.Kind.DONE, "Selesai · terpasang ${P.formatFirmwareVersion(s.c3Version)}")
            state == P.OtaState.UP_TO_DATE -> OtaResult(OtaResult.Kind.UP_TO_DATE, "Sudah terbaru")
            else -> OtaResult(OtaResult.Kind.FAILED, "Gagal: ${P.otaErrorLabel(s.otaError)}")
        }
    }

    // Row 18: version check by the phone (informational)
    fun latestVersion(fetch: () -> String?, installedC3: Long): Res<Pair<String, Boolean>> {
        val text = fetch() ?: return Res.Err("Tidak bisa menghubungi GitHub · coba lagi saat HP online")
        val v = UpdateManifest.parseVersion(text) ?: return Res.Err("Manifest rilis tidak valid")
        return Res.Ok(v to UpdateManifest.isNewer(v, installedC3))
    }

    // Live controls (Beranda) and activities
    fun expression(id: Int, done: Done<Unit>) = command(P.expression(id), "Ekspresi tidak dikenal", done)
    fun talk(done: Done<Unit>) = command(P.talk(), "", done)
    fun pause(done: Done<Unit>) = command(P.pauseInitiative(1), "", done)
    fun resume(done: Done<Unit>) = command(P.pauseInitiative(0), "", done)
    fun pomodoro(sub: Int, done: Done<Unit>) = command(P.pomodoro(sub), "Perintah Pomodoro tidak valid", done)
    fun activity(sub: Int, activityId: Int = 0, done: Done<Unit>) = command(P.activity(sub, activityId), "Aktivitas tidak valid", done)
    fun game(sub: Int, done: Done<Unit>) = command(P.game(sub), "", done)
    fun learning(sub: Int, done: Done<Unit>) = command(P.learning(sub), "", done)
    fun soundTest(cue: Int, done: Done<Unit>) = command(P.soundTest(cue), "Suara tidak dikenal", done)
    fun experiencesClear(done: Done<Unit>) = command(P.experiencesClear(), "", done)
    fun rest(sleep: Boolean, done: Done<Unit>) = command(P.rest(sleep), "", done)
    fun remotePet(done: Done<Unit>) = command(P.remotePet(), "", done)

    // IR
    fun irSend(hex: String, done: Done<Unit>) {
        val code = if (hex.length == 8) hex.toLongOrNull(16) else null
        command(code?.let { P.irSend(it) }, "Kode NEC harus 8 digit hex", done)
    }
    fun irLearn(name: String, done: Done<Unit>) = command(P.irLearn(name), "Nama 1-15: huruf, angka, spasi, - _", done)
    fun irReplay(name: String, done: Done<Unit>) = command(P.irReplay(name), "Pilih profil tersimpan", done)
    fun irProfiles(done: Done<List<String>>) = client.listIrProfiles(passthrough(done))

    // Integrations
    fun loadIntegrations(done: Done<P.IntegrationsConfig>) = client.readIntegrations(passthrough(done))
    fun integrationsStatus(done: Done<P.IntegrationsStatus>) = client.readIntegrationsStatus(passthrough(done))
    fun refreshIntegrations(done: Done<Unit>) = client.refreshIntegrations(passthrough(done))
    fun clearIntegrations(done: Done<Unit>) = client.clearIntegrations(passthrough(done))

    /** Saves config, then token / calendar only when typed (blank = keep). [clearCalendar] sends an empty URL. */
    fun saveIntegrations(weather: Boolean, lat: Float, lon: Float, base: String, entities: List<String>,
                         token: String, calendar: String, clearCalendar: Boolean, done: Done<Unit>) {
        if (P.integrationsSetConfig(weather, lat, lon, base, entities)?.also { it.wipe() } == null) return done(invalid("Alamat, entitas, lintang atau bujur tidak valid"))
        if (token.isNotEmpty() && P.integrationsSetToken(token)?.also { it.wipe() } == null) return done(invalid("Token tidak valid (ASCII tanpa spasi, maks 191)"))
        if (calendar.isNotEmpty() && P.integrationsSetCalendar(calendar)?.also { it.wipe() } == null) return done(invalid("URL kalender tidak valid (maks 255)"))
        client.setIntegrationsConfig(weather, lat, lon, base, entities) { o ->
            if (o is SettingsOutcome.Fail) return@setIntegrationsConfig done(err(o.failure))
            if (token.isEmpty()) return@setIntegrationsConfig calendarStep(calendar, clearCalendar, done)
            client.setIntegrationsToken(token) { t ->
                if (t is SettingsOutcome.Fail) done(err(t.failure)) else calendarStep(calendar, clearCalendar, done)
            }
        }
    }

    private fun calendarStep(calendar: String, clear: Boolean, done: Done<Unit>) {
        val url = if (clear) "" else calendar
        if (!clear && calendar.isEmpty()) return done(Res.Ok(Unit))
        client.setIntegrationsCalendar(url, passthrough(done))
    }
}
