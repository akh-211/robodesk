package com.robodesk.phonebridge

/** Fine-grained phase kept for the existing notification UI. */
enum class BridgeConnectionPhase { IDLE, SELECTING, PAIRING, CONNECTING, AUTHENTICATING, POLICY, READY, OFFLINE, NEEDS_PERMISSION, INCOMPATIBLE, SECURITY_ERROR, REVOKING, REVOKED }

/** Phases of the single robot connection (ANDROID_APP_TASKS.md A1). Pure Kotlin so it runs in host tests. */
enum class LinkPhase { OFFLINE, SCANNING, CONNECTING, BONDING, HANDSHAKE, READY, RECONNECTING, ERROR }

/** [detail] is the Indonesian stage/failure line shown during onboarding (never robot-supplied). [attempt] is the reconnect attempt number (RECONNECTING only); [cause] is a short UI-safe reason (ERROR/RECONNECTING). */
data class LinkState(val phase: LinkPhase = LinkPhase.OFFLINE, val attempt: Int = 0, val cause: String? = null, val handshakeFailures: Int = 0, val detail: String? = null, val staleBond: Boolean = false, val pairStep: Int = 0) {
    val ready: Boolean get() = phase == LinkPhase.READY
}

/** Minimal observable value holder (stand-in for StateFlow so the logic compiles without coroutines on the host). */
class Observable<T>(initial: T) {
    private val listeners = java.util.concurrent.CopyOnWriteArrayList<(T) -> Unit>()
    @Volatile var value: T = initial
        private set

    /** Calls [listener] immediately with the current value; returns a handle that unsubscribes. */
    fun observe(listener: (T) -> Unit): () -> Unit {
        listeners += listener
        listener(value)
        return { listeners -= listener }
    }

    fun set(next: T) {
        if (next == value) return
        value = next
        listeners.forEach { it(next) }
    }
}

/** Process-wide connection state; written by RobotLinkService, read by UI. */
object RobotLink {
    val state = Observable(LinkState())
}

/** Last connection steps (scan, connect mode, GATT status codes) shown on the pairing screen; never keys or payloads. */
object LinkDiag {
    private val lines = ArrayDeque<String>()
    val text = Observable("")

    @Synchronized fun log(line: String) {
        val stamp = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.ROOT).format(java.util.Date())
        lines.addLast("$stamp $line")
        while (lines.size > 14) lines.removeFirst()
        text.set(lines.joinToString("\n"))
    }
}

/** Reconnect delay: 1 s doubling up to 30 s; reset on a successful READY. */
class ReconnectBackoff(private val firstMs: Long = 1_000L, private val maxMs: Long = 30_000L) {
    var attempt = 0
        private set

    fun next(): Long {
        val delay = (firstMs shl attempt.coerceAtMost(20)).coerceAtMost(maxMs)
        attempt++
        return delay
    }

    fun reset() { attempt = 0 }
}

/** Maps the legacy fine-grained bridge phase (still used by notification UI) to the A1 link phase. */
object LinkPhaseMapper {
    fun map(phase: BridgeConnectionPhase, reconnectAttempt: Int, cause: String?, handshakeFailures: Int = 0, detail: String? = null, staleBond: Boolean = false, pairStep: Int = 0): LinkState = mapPhase(phase, reconnectAttempt, cause).copy(handshakeFailures = handshakeFailures, detail = detail, staleBond = staleBond, pairStep = pairStep)

    private fun mapPhase(phase: BridgeConnectionPhase, reconnectAttempt: Int, cause: String?): LinkState = when (phase) {
        BridgeConnectionPhase.IDLE, BridgeConnectionPhase.SELECTING, BridgeConnectionPhase.REVOKED, BridgeConnectionPhase.REVOKING -> LinkState(LinkPhase.OFFLINE)
        BridgeConnectionPhase.PAIRING -> LinkState(LinkPhase.BONDING)
        BridgeConnectionPhase.CONNECTING -> LinkState(LinkPhase.CONNECTING)
        BridgeConnectionPhase.AUTHENTICATING, BridgeConnectionPhase.POLICY -> LinkState(LinkPhase.HANDSHAKE)
        BridgeConnectionPhase.READY -> LinkState(LinkPhase.READY)
        BridgeConnectionPhase.OFFLINE ->
            if (reconnectAttempt > 0) LinkState(LinkPhase.RECONNECTING, reconnectAttempt, cause) else LinkState(LinkPhase.OFFLINE)
        BridgeConnectionPhase.NEEDS_PERMISSION -> LinkState(LinkPhase.ERROR, 0, cause ?: "Izin Perangkat sekitar diperlukan")
        BridgeConnectionPhase.INCOMPATIBLE -> LinkState(LinkPhase.ERROR, 0, cause ?: "Firmware robot tidak kompatibel")
        BridgeConnectionPhase.SECURITY_ERROR -> LinkState(LinkPhase.ERROR, 0, cause ?: "Kesalahan keamanan sesi")
    }
}
