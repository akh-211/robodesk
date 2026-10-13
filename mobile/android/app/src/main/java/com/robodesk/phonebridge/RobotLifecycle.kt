package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

/**
 * Robot-side lifecycle events (I5) as plain logic so they run in host tests: OTA waiting (GAP-D), "why is the link not usable"
 * notices (A10) and the two guards that keep reconnect/replay handling from looping.
 */

/**
 * Tracks one firmware update after `ota-start` was accepted (or after a reconnect found an update already running).
 * BLE drops during the update; the result is read from the first snapshot after the reconnect. No live percentage exists.
 */
class OtaTracker(private val now: () -> Long, private val interpret: (P.StatusSnapshot) -> OtaResult, private val waitMs: Long = WAIT_MS) {
    var startedAt = 0L
        private set
    var result: OtaResult? = null
        private set
    private var wasOffline = false
    private var connected = true

    val tracking: Boolean get() = startedAt != 0L
    /** True while the update screen owns the app: no write buttons, chip says "Update berjalan". */
    val active: Boolean get() = tracking && result == null

    fun start() { startedAt = now().coerceAtLeast(1L); result = null; wasOffline = false }
    fun dismiss() { startedAt = 0L; result = null; wasOffline = false }

    fun onLink(ready: Boolean) {
        if (!ready && connected && tracking) wasOffline = true
        connected = ready
    }

    /** A snapshot arrived. A terminal state counts only after a reconnect (or 30 s), so a stale DONE/FAILED from an older run is ignored. */
    fun onStatus(s: P.StatusSnapshot) {
        val r = interpret(s)
        if (!tracking) {
            if (r.kind == OtaResult.Kind.RUNNING) { start(); wasOffline = true } // adopted: ota-start answer was lost or the app restarted
            return
        }
        if (result != null) return
        val settled = r.kind == OtaResult.Kind.DONE || r.kind == OtaResult.Kind.UP_TO_DATE || r.kind == OtaResult.Kind.FAILED
        if (settled && (wasOffline || now() - startedAt > SETTLE_MS)) result = r
    }

    /** Call about once per second. Ends the wait after [waitMs] with a "check the robot" result. */
    fun tick() {
        if (active && now() - startedAt > waitMs)
            result = OtaResult(OtaResult.Kind.FAILED, if (connected) "Update belum selesai setelah 10 menit · periksa robot" else "Belum tersambung setelah 10 menit · periksa robot")
    }

    companion object {
        const val WAIT_MS = 10 * 60 * 1000L
        const val SETTLE_MS = 30_000L
    }
}

/** What the robot's identity characteristic (93de0007) says. It is readable only by the *approved* connection: before approval it is empty. */
object IdentityGate {
    sealed class Parsed {
        /** Empty value: bond exists but the robot has not approved this phone yet (not an error; read again). */
        object AwaitingApproval : Parsed()
        object Invalid : Parsed()
        class Ok(val robotId: ByteArray, val flags: Long) : Parsed()
    }
    enum class Verdict { KEY_MISSING, NO_CREDENTIAL, PROCEED }

    fun parse(value: ByteArray): Parsed {
        if (value.isEmpty()) return Parsed.AwaitingApproval
        if (value.size != 26 || value[0].toInt() != 2 || value[1].toInt() != 1) return Parsed.Invalid
        val flags = (value[2].toLong() and 0xff) or ((value[3].toLong() and 0xff) shl 8) or ((value[4].toLong() and 0xff) shl 16) or ((value[5].toLong() and 0xff) shl 24)
        return Parsed.Ok(value.copyOfRange(6, 22), flags)
    }

    /** flags bit1 = active key, bit2 = enrollment window (60 s after approval). [haveKey] = stored key for this robot id. */
    fun decide(flags: Long, haveKey: Boolean): Verdict = when {
        (flags and 2L) != 0L && !haveKey -> Verdict.KEY_MISSING
        (flags and 2L) == 0L && (flags and 4L) == 0L -> Verdict.NO_CREDENTIAL
        else -> Verdict.PROCEED
    }
}

/**
 * Onboarding "pairing search". The robot only accepts a new bond from a connection made WHILE its 60 s window is open
 * (anything else is disconnected at once), so the phone retries a fresh connection quickly instead of backing off to 30 s,
 * and those rejected attempts are not counted as handshake failures.
 */
class PairingSearch(private val windowMs: Long = WINDOW_MS) {
    private var until = 0L
    fun begin(now: Long) { until = now + windowMs }
    fun end() { until = 0L }
    fun active(now: Long) = until != 0L && now < until

    companion object {
        const val WINDOW_MS = 120_000L
        const val RETRY_MS = 4_000L
        /** From bond to approval: window 60 s + margin. */
        const val APPROVAL_MS = 75_000L
        /** A bond in progress (user typing the passkey) must not be killed by the 20 s operation watchdog. */
        const val OPERATION_MS = 75_000L
    }
}

/** Counts sessions that died before READY while handshaking (robot ignores us: iPhone mode, revoked trust, factory reset on the robot). */
class HandshakeGuard(private val limit: Int = LIMIT) {
    var failures = 0
        private set
    val stuck: Boolean get() = failures >= limit
    fun onHandshakeFailed() { if (failures < 1000) failures++ }
    fun onReady() { failures = 0 }

    companion object { const val LIMIT = 3 }
}

/**
 * The robot refused our envelopes (replayed counter, revoked session). One refusal re-syncs with a fresh session; repeated
 * refusals without any accepted answer in between stop the automatic reconnect instead of looping.
 */
class SessionRejectGuard(private val limit: Int = LIMIT) {
    enum class Action { RESYNC, GIVE_UP }

    var consecutive = 0
        private set

    fun onRejected(): Action { consecutive++; return if (consecutive >= limit) Action.GIVE_UP else Action.RESYNC }
    fun onAccepted() { consecutive = 0 }
    fun reset() { consecutive = 0 }

    companion object { const val LIMIT = 3 }
}

/** One line shown under the appbar when the app cannot do its normal job; null when everything is usable. */
class UiNotice(val kind: Kind, val text: String, val bad: Boolean = true, val action: String? = null) {
    enum class Kind { NO_ROBOT, PERMISSION, BLUETOOTH_OFF, UPDATING, HANDSHAKE_STUCK, SECURITY, IPHONE_ROBOT, REPAIR, NOT_PAIRED, RECONNECTING, S3_LINK }
}

/** Manufacturer data (company FFFF) in the robot's scan response: "RD" + flag, flag 1 while its pairing window is open. */
object PairingBeacon {
    const val COMPANY_ID = 0xFFFF
    /** null = no marker (older firmware or another device). */
    fun parse(payload: ByteArray?): Boolean? =
        if (payload == null || payload.size < 3 || payload[0] != 0x52.toByte() || payload[1] != 0x44.toByte()) null else payload[2].toInt() == 1
}

object LinkNotices {
    const val REPAIR_ACTION = "Pasangkan ulang"
    const val PAIR_ACTION = "Mulai pairing"
    const val IPHONE_TEXT = "Robot disetel untuk iPhone · lakukan reset pabrik dari robot (sentuh/serial) lalu pasangkan ulang"

    /** First matching condition wins; ordered from "nothing to connect to" down to "robot-internal link". */
    fun compute(link: LinkState, hasRobot: Boolean, bluetoothOn: Boolean, nearbyGranted: Boolean, otaActive: Boolean,
                status: P.StatusSnapshot?, iphoneRobot: Boolean = false, robotSeen: Boolean = false, robotPairingOpen: Boolean = false,
                paired: Boolean = true): UiNotice? = when {
        !hasRobot -> UiNotice(UiNotice.Kind.NO_ROBOT, "Belum ada robot dipilih · jalankan onboarding")
        !nearbyGranted -> UiNotice(UiNotice.Kind.PERMISSION, "Izin Perangkat sekitar ditolak · izinkan di Setelan Android")
        !bluetoothOn -> UiNotice(UiNotice.Kind.BLUETOOTH_OFF, "Bluetooth mati · nyalakan untuk menyambung")
        !paired -> UiNotice(UiNotice.Kind.NOT_PAIRED, "Robot dipilih tetapi belum dipasangkan · app tidak menyambung sendiri sebelum Anda memulai pairing", action = PAIR_ACTION)
        otaActive && !link.ready -> UiNotice(UiNotice.Kind.UPDATING, "Robot sedang memperbarui · menyambung ulang otomatis", bad = false)
        iphoneRobot -> UiNotice(UiNotice.Kind.IPHONE_ROBOT, IPHONE_TEXT)
        // The robot waits for a new phone (its pairing window is open): after a reset the old trust is gone on its side.
        !link.ready && robotPairingOpen -> UiNotice(UiNotice.Kind.REPAIR, "Robot membuka pairing baru (kemungkinan di-reset) · pasangkan ulang agar HP ini terhubung lagi", action = REPAIR_ACTION)
        link.handshakeFailures >= HandshakeGuard.LIMIT && !link.ready ->
            UiNotice(UiNotice.Kind.HANDSHAKE_STUCK, "Robot tidak menyelesaikan sesi aman · mungkin disetel untuk iPhone atau tidak lagi mempercayai HP ini (misalnya di-reset) · pasangkan ulang", action = REPAIR_ACTION)
        link.phase == LinkPhase.ERROR -> UiNotice(UiNotice.Kind.SECURITY, "Kesalahan keamanan sesi · pasangkan ulang", action = REPAIR_ACTION)
        // Robot is advertising but the phone's saved bond keeps failing: the bond no longer matches the robot's keys.
        !link.ready && link.staleBond && robotSeen -> UiNotice(UiNotice.Kind.REPAIR, "Robot terlihat tetapi bond Bluetooth HP tidak cocok lagi · pasangkan ulang", action = REPAIR_ACTION)
        !link.ready -> UiNotice(UiNotice.Kind.RECONNECTING,
            if (link.attempt > 0) "Sesi BLE berakhir · menyambung ulang (percobaan ${link.attempt})…" else "Sesi BLE berakhir · menyambung ulang…")
        status != null && !status.uartConnected -> UiNotice(UiNotice.Kind.S3_LINK, "Menyambung ulang robot (S3)…", bad = false)
        else -> null
    }
}
