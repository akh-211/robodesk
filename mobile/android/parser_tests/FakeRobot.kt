package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

/**
 * Host-test stand-in for the C3 gateway (I7). It speaks the real wire format end to end: HMAC envelopes with per-direction counters,
 * GATT framing at MTU 23, kind 0x7d responses, and scripted replies including injected errors and link loss.
 */
internal class FakeRobot(val key: ByteArray = ByteArray(32) { (it * 5 + 1).toByte() }, val sessionId: Long = 0x01020304L) {
    class Received(val kind: Int, val counter: Long, val payload: ByteArray, val atMs: Long)

    sealed class Reply {
        /** Answer with [status] and [body] after [delayMs]. */
        class Respond(val status: Int = 0, val body: ByteArray = ByteArray(0), val delayMs: Long = 0) : Reply()
        /** Never answer (request times out on the phone). */
        object Drop : Reply()
        /** The BLE link drops instead of answering. */
        object Disconnect : Reply()
        /** Answer with a damaged envelope (MAC failure on the phone). */
        object Tamper : Reply()
    }

    val log = ArrayList<Received>()
    var script: (Received) -> Reply = { Reply.Respond() }
    var dropped = false
    var outstanding = 0
    var maxOutstanding = 0

    private val robotReceiver = BridgeAuthenticatedEnvelope.SessionReceiver(key, sessionId, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT,
        (1..255).associateWith { 20_000L })
    private val robotSender = BridgeAuthenticatedEnvelope.SessionSender(key, sessionId, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE)
    private val phoneSender = BridgeAuthenticatedEnvelope.SessionSender(key, sessionId, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT)
    private val phoneReceiver = BridgeAuthenticatedEnvelope.SessionReceiver(key, sessionId, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE, mapOf(0x7d to 0L))
    private val robotFraming = BridgeGattFraming.Reassembler()
    private val phoneFraming = BridgeGattFraming.Reassembler()
    private var frameId = 1
    private class Due(val atMs: Long, val wire: ByteArray, val counterOfRequest: Long)
    private val due = ArrayList<Due>()
    val phoneCounters = ArrayList<Long>()
    val robotCounters = ArrayList<Long>()
    var lastResponseWire: ByteArray? = null

    private fun throughGatt(message: ByteArray, receiver: BridgeGattFraming.Reassembler): ByteArray? {
        val frames = BridgeGattFraming.fragments(message, 23, frameId++.also { if (frameId > 0xffff) frameId = 1 }) ?: return null
        var complete: ByteArray? = null
        frames.forEachIndexed { i, f -> val r = receiver.accept(f, 0L); if (i == frames.lastIndex) complete = r else check(r == null) }
        return complete
    }

    /** Phone side: builds the envelope for [request], sends it through framing to the robot, queues the scripted reply. Returns the envelope counter. */
    fun transmit(request: P.Request, nowMs: Long): Long? {
        val payload = request.payloadFor(sessionId) ?: return null
        val envelope = phoneSender.create(request.kind, 0, payload) ?: return null
        val counter = (envelope[12].toLong() and 0xff) or ((envelope[13].toLong() and 0xff) shl 8) or ((envelope[14].toLong() and 0xff) shl 16) or ((envelope[15].toLong() and 0xff) shl 24)
        phoneCounters += counter
        val wire = throughGatt(envelope, robotFraming) ?: error("framing lost the request")
        val message = robotReceiver.accept(wire) ?: error("robot rejected the phone envelope (MAC/counter/session)")
        check(message.counter == counter && message.kind == request.kind)
        val received = Received(message.kind, message.counter, message.payload, nowMs)
        log += received
        outstanding++
        maxOutstanding = maxOf(maxOutstanding, outstanding)
        when (val reply = script(received)) {
            is Reply.Respond -> queue(received, reply.status, reply.body, nowMs + reply.delayMs, tamper = false)
            is Reply.Tamper -> queue(received, 0, ByteArray(0), nowMs, tamper = true)
            is Reply.Drop -> { }
            is Reply.Disconnect -> dropped = true
        }
        return counter
    }

    private fun queue(request: Received, status: Int, body: ByteArray, atMs: Long, tamper: Boolean) {
        val payload = ByteArray(P.RESPONSE_HEADER + body.size)
        for (i in 0 until 4) payload[i] = (request.counter shr (8 * i)).toByte()
        payload[4] = request.kind.toByte()
        payload[5] = status.toByte()
        body.copyInto(payload, P.RESPONSE_HEADER)
        val envelope = robotSender.create(P.KIND_RESPONSE, 0, payload)!!
        robotCounters += (envelope[12].toLong() and 0xff) or ((envelope[13].toLong() and 0xff) shl 8)
        if (tamper) envelope[envelope.size - 1] = (envelope[envelope.size - 1].toInt() xor 1).toByte()
        due += Due(atMs, envelope, request.counter)
    }

    /** Delivers every reply due at [nowMs] to [scheduler] through framing and envelope verification. */
    fun deliverDue(nowMs: Long, scheduler: RobotRequestScheduler, replayDuplicates: Boolean = false) {
        val ready = due.filter { it.atMs <= nowMs }
        due.removeAll(ready)
        for (item in ready) {
            outstanding = maxOf(0, outstanding - 1)
            val wire = throughGatt(item.wire, phoneFraming) ?: continue
            lastResponseWire = wire
            val message = phoneReceiver.accept(wire) ?: continue
            val response = P.parseResponse(message.payload) ?: continue
            scheduler.onResponse(response, nowMs, sessionId)
            if (replayDuplicates) check(phoneReceiver.accept(wire) == null) { "replayed response envelope must be refused" }
        }
    }

    /** A reply that arrives after the session ended: only used to prove stale replies are ignored. */
    fun staleResponseFor(requestKind: Int, requestCounter: Long, status: Int = 0): P.Response = P.Response(requestCounter, requestKind, status, ByteArray(0))
}

/** Virtual-clock driver around a scheduler and a [FakeRobot]. */
internal class LinkHarness(val robot: FakeRobot = FakeRobot()) {
    val scheduler = RobotRequestScheduler().also { it.onSessionStart(robot.sessionId) }
    var now = 1_000L

    val transport = SettingsTransport { request, callback ->
        if (robot.dropped) { request.wipe(); callback(SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.NOT_CONNECTED))) }
        else if (!scheduler.enqueue(request, callback)) { request.wipe(); callback(SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.QUEUE_FULL))) }
    }

    /** One scheduling step: due replies, deadlines, then start at most one request. */
    fun step() {
        robot.deliverDue(now, scheduler)
        scheduler.expire(now)
        if (!robot.dropped) scheduler.startNext(now) { robot.transmit(it, now) }
        if (robot.dropped) { scheduler.onDisconnected() }
    }

    fun advance(ms: Long, stepMs: Long = 50L) {
        var left = ms
        while (left > 0) { val d = minOf(stepMs, left); now += d; left -= d; step() }
    }

    fun runUntilIdle(limitMs: Long = 120_000L) {
        var elapsed = 0L
        step()
        while ((scheduler.inFlight || scheduler.hasWaiting) && elapsed < limitMs) { now += 50; elapsed += 50; step() }
    }
}
