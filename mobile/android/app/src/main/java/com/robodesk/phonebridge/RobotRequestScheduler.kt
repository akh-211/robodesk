package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

/** Lower number = served first. CONTINUATION keeps the pages of one SettingsSet back to back. */
object RequestPriority {
    const val CONTINUATION = 0
    const val USER = 1
    const val SAVE = 2
    const val POLL = 3
    const val PULL = 4

    /** Default lane per kind: user actions > settings save > status polling > memory pull. Reads that a screen waits for count as user actions. */
    fun forKind(kind: Int): Int = when (kind) {
        P.KIND_SETTINGS_SET -> SAVE
        P.KIND_STATUS_SNAPSHOT -> POLL
        P.KIND_MEMORY_LIST -> PULL
        else -> USER
    }

    /** True when the robot may have changed state; such a request is never retried and a lost answer is "uncertain". */
    fun mutating(kind: Int): Boolean = when (kind) {
        P.KIND_SETTINGS_SET, P.KIND_SECRET_SET, P.KIND_COMMAND, P.KIND_MEMORY_ADD, P.KIND_MEMORY_DELETE, P.KIND_MEMORY_CLEAR,
        P.KIND_REMINDER_ADD, P.KIND_REMINDER_DELETE, P.KIND_INTEGRATIONS_SET, P.KIND_INTEGRATIONS_ACTION, P.KIND_CHAT_SEND -> true
        else -> false
    }
}

/**
 * Global request scheduler: exactly one request in flight (protocol rule), priority lanes, timeout, and retries only with a
 * new envelope counter (the caller's `send` builds a fresh envelope each time it is invoked).
 *
 * Retry rules: read requests are re-sent once after a timeout; any request is re-sent after `Busy` with a short backoff
 * (the robot did not process it); a mutating request whose answer was lost (timeout or disconnect) finishes as
 * [SettingsFailure.Reason.UNCERTAIN] and is never re-sent. Responses are bound to the session they were sent in.
 */
class RobotRequestScheduler(private val capacity: Int = 16) {
    private class Entry(val request: P.Request, val callback: SettingsDone<P.Response>, val priority: Int, val seq: Long) {
        var readyAt = 0L
        var timeoutAttempts = 1
        var busyRetries = 0
        var sessionId = 0L
    }

    private val waiting = ArrayList<Entry>()
    private var current: Entry? = null
    private var counter = 0L
    private var deadlineMs = 0L
    private var seq = 0L
    private var session = 0L

    /** Set once an outcome was lost; the UI must re-read the affected data after reconnect, then call [acknowledgeReread]. */
    var needsReread = false
        private set

    val inFlight: Boolean get() = current != null
    val hasWaiting: Boolean get() = waiting.isNotEmpty()
    val hasMutatingActive: Boolean
        get() = current?.let { RequestPriority.mutating(it.request.kind) } == true || waiting.any { RequestPriority.mutating(it.request.kind) }

    fun acknowledgeReread() { needsReread = false }

    fun hasWaiting(kind: Int) = waiting.any { it.request.kind == kind } || current?.request?.kind == kind

    /** False when the queue (waiting + in flight) is full; the caller then fails the request itself. */
    fun enqueue(request: P.Request, callback: SettingsDone<P.Response>): Boolean {
        if (waiting.size + (if (current != null) 1 else 0) >= capacity) return false
        waiting += Entry(request, callback, request.priorityHint ?: RequestPriority.forKind(request.kind), seq++)
        return true
    }

    /** Starts the best ready request via [send] (returns the new envelope counter, or null if it could not be built). */
    fun startNext(nowMs: Long, send: (P.Request) -> Long?): P.Request? {
        while (current == null) {
            val entry = waiting.filter { it.readyAt <= nowMs }.minWithOrNull(compareBy({ it.priority }, { it.seq })) ?: return null
            waiting.remove(entry)
            val sent = send(entry.request)
            if (sent == null) { finish(entry, SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.INVALID_REQUEST))); continue }
            current = entry
            entry.sessionId = session
            counter = sent
            deadlineMs = nowMs + entry.request.timeoutMs
            return entry.request
        }
        return null
    }

    /** Delivers [response] when counter, kind and session all match the in-flight request; anything else is ignored. */
    fun onResponse(response: P.Response, nowMs: Long = 0L, sessionId: Long = session): Boolean {
        val entry = current ?: return false
        if (sessionId != entry.sessionId || response.requestId != counter || response.kind != entry.request.kind) return false
        current = null
        if (response.statusCode == P.Status.BUSY.code && entry.busyRetries < MAX_BUSY_RETRIES) {
            entry.busyRetries++
            entry.readyAt = nowMs + BUSY_BACKOFF_MS * entry.busyRetries
            waiting += entry
            return true
        }
        finish(entry, SettingsOutcome.Ok(response))
        return true
    }

    /**
     * The robot refused the envelope itself (ack status != 0, e.g. replayed counter): it did not process the request, so it is
     * failed as REJECTED at once, never retried and never "uncertain". False when counter or session do not match the in-flight request.
     */
    fun onRejectedAck(requestCounter: Long, sessionId: Long = session): Boolean {
        val entry = current ?: return false
        if (sessionId != entry.sessionId || requestCounter != counter) return false
        current = null
        finish(entry, SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.REJECTED)))
        return true
    }

    /** Handles the in-flight deadline: re-queues a read once, otherwise fails (UNCERTAIN for mutating requests). True if anything changed. */
    fun expire(nowMs: Long): Boolean {
        val entry = current ?: return false
        if (nowMs < deadlineMs) return false
        current = null
        if (!RequestPriority.mutating(entry.request.kind) && entry.timeoutAttempts < MAX_READ_ATTEMPTS) {
            entry.timeoutAttempts++
            entry.readyAt = nowMs
            waiting += entry
            return true
        }
        finish(entry, SettingsOutcome.Fail(SettingsFailure(lostReason(entry))))
        return true
    }

    /** Earliest time something needs attention (deadline or a backoff end), or null when idle. */
    fun nextWakeMs(nowMs: Long): Long? {
        var wake = if (current != null) deadlineMs else Long.MAX_VALUE
        for (e in waiting) if (e.readyAt > nowMs && e.readyAt < wake) wake = e.readyAt
        return if (wake == Long.MAX_VALUE) null else wake
    }

    /** A new authenticated session starts: responses of older sessions are never accepted. */
    fun onSessionStart(sessionId: Long) { session = sessionId }

    /** The link dropped. A sent mutating request becomes UNCERTAIN, everything else NOT_CONNECTED. */
    fun onDisconnected() {
        session = 0L
        val entries = ArrayList<Entry>(waiting.size + 1)
        val sent = current
        current = null
        entries += waiting
        waiting.clear()
        sent?.let { finish(it, SettingsOutcome.Fail(SettingsFailure(lostReason(it)))) }
        entries.forEach { finish(it, SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.NOT_CONNECTED))) }
    }

    fun failAll(failure: SettingsFailure) {
        val entries = ArrayList<Entry>(waiting.size + 1)
        current?.let { entries += it }
        current = null
        entries += waiting
        waiting.clear()
        entries.forEach { finish(it, SettingsOutcome.Fail(failure)) }
    }

    private fun lostReason(entry: Entry): SettingsFailure.Reason =
        if (RequestPriority.mutating(entry.request.kind)) { needsReread = true; SettingsFailure.Reason.UNCERTAIN } else SettingsFailure.Reason.TIMEOUT

    private fun finish(entry: Entry, outcome: SettingsOutcome<P.Response>) {
        entry.request.wipe()
        entry.callback(outcome)
    }

    companion object {
        const val MAX_BUSY_RETRIES = 3
        const val BUSY_BACKOFF_MS = 250L
        const val MAX_READ_ATTEMPTS = 2
    }
}

/**
 * Status polling at the lowest steady priority. Runs only while a screen needs the status, the app is in the foreground and
 * the link is ready; it pauses while any write is queued or in flight so saves never wait behind it.
 */
class StatusPoller(private val client: BridgeSettingsClient, private val scheduler: RobotRequestScheduler, private val intervalMs: Long = 5_000L) {
    val latest = Observable<P.StatusSnapshot?>(null)
    @Volatile var screenNeedsStatus = false
    @Volatile var foreground = false
    @Volatile var connected = false
    private var pollOutstanding = false
    private var nextAt = 0L

    val active: Boolean get() = screenNeedsStatus && foreground && connected

    /** Returns true when a poll was submitted. */
    fun tick(nowMs: Long): Boolean {
        if (!active || pollOutstanding || nowMs < nextAt || scheduler.hasMutatingActive) return false
        pollOutstanding = true
        nextAt = nowMs + intervalMs
        client.readStatus { outcome ->
            pollOutstanding = false
            if (outcome is SettingsOutcome.Ok) latest.set(outcome.value)
        }
        return true
    }
}
