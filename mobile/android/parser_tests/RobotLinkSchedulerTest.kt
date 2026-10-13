package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

private fun noop(): SettingsDone<P.Response> = { }

private fun statusDoc(otaState: Int = 0, schema: Int = 3): ByteArray = ByteArray(if (schema == 3) 76 else 58).also { it[0] = schema.toByte(); if (schema == 3) it[58] = otaState.toByte() }

private fun pages(document: ByteArray): List<ByteArray> = P.settingsSetPages(document)!!.map { it.payloadFor(0)!! }

/** Robot that serves [document] for SettingsGet (kind 8) and status snapshots (kind 11); everything else is acknowledged. */
private fun documentRobot(settingsDoc: ByteArray, status: ByteArray = statusDoc()): (FakeRobot.Received) -> FakeRobot.Reply = { rx ->
    when (rx.kind) {
        P.KIND_SETTINGS_GET -> FakeRobot.Reply.Respond(0, pages(settingsDoc)[rx.payload[0].toInt()])
        P.KIND_STATUS_SNAPSHOT -> FakeRobot.Reply.Respond(0, pages(status)[0])
        P.KIND_SETTINGS_SET -> FakeRobot.Reply.Respond(0, byteArrayOf(rx.payload[0], if (rx.payload[0].toInt() == pages(settingsDoc).lastIndex) 1 else 0))
        else -> FakeRobot.Reply.Respond()
    }
}

private fun settingsDocument(size: Int = 647) = ("{\"schema\":1,\"robotName\":\"" + "R".repeat(size - 28) + "\"}").toByteArray()

fun verifyRobotLinkAndScheduler() {
    verifyLinkState()
    verifySchedulerLanes()
    verifyRetriesAndTimeouts()
    verifyClientOverFakeRobot()
    verifyDisconnectAndSessions()
    verifyPoller()
    verifyFuzz()
    println("PASS: link state, scheduler lanes/timeouts/retries/sessions/uncertain outcomes, poller and FakeRobot over real envelopes+framing")
}

private fun verifyLinkState() {
    val backoff = ReconnectBackoff()
    check(listOf(backoff.next(), backoff.next(), backoff.next(), backoff.next(), backoff.next(), backoff.next(), backoff.next()) ==
        listOf(1000L, 2000L, 4000L, 8000L, 16000L, 30000L, 30000L))
    backoff.reset(); check(backoff.attempt == 0 && backoff.next() == 1000L)
    check(LinkPhaseMapper.map(BridgeConnectionPhase.IDLE, 0, null).phase == LinkPhase.OFFLINE)
    check(LinkPhaseMapper.map(BridgeConnectionPhase.CONNECTING, 0, null).phase == LinkPhase.CONNECTING)
    check(LinkPhaseMapper.map(BridgeConnectionPhase.PAIRING, 0, null).phase == LinkPhase.BONDING)
    check(LinkPhaseMapper.map(BridgeConnectionPhase.AUTHENTICATING, 0, null).phase == LinkPhase.HANDSHAKE)
    check(LinkPhaseMapper.map(BridgeConnectionPhase.POLICY, 0, null).phase == LinkPhase.HANDSHAKE)
    check(LinkPhaseMapper.map(BridgeConnectionPhase.READY, 0, null).ready)
    LinkPhaseMapper.map(BridgeConnectionPhase.OFFLINE, 3, "x").let { check(it.phase == LinkPhase.RECONNECTING && it.attempt == 3) }
    check(LinkPhaseMapper.map(BridgeConnectionPhase.OFFLINE, 0, null).phase == LinkPhase.OFFLINE)
    LinkPhaseMapper.map(BridgeConnectionPhase.SECURITY_ERROR, 0, "sebab").let { check(it.phase == LinkPhase.ERROR && it.cause == "sebab") }
    check(BridgeConnectionPhase.entries.all { LinkPhaseMapper.map(it, 0, null).phase in LinkPhase.entries })
    val seen = ArrayList<LinkPhase>()
    val off = RobotLink.state.observe { seen += it.phase }
    RobotLink.state.set(LinkState(LinkPhase.READY)); RobotLink.state.set(LinkState(LinkPhase.READY)); RobotLink.state.set(LinkState(LinkPhase.RECONNECTING, 1))
    off(); RobotLink.state.set(LinkState(LinkPhase.OFFLINE))
    check(seen == listOf(LinkPhase.OFFLINE, LinkPhase.READY, LinkPhase.RECONNECTING)) { "observable: $seen" }
}

private fun verifySchedulerLanes() {
    val h = LinkHarness()
    h.robot.script = { FakeRobot.Reply.Respond(0, ByteArray(0), delayMs = 120) }
    val order = ArrayList<Int>()
    fun cb(tag: Int): SettingsDone<P.Response> = { order += tag }
    // First request goes out immediately; the others queue up behind it (exactly one in flight) and are then served by lane.
    check(h.scheduler.enqueue(P.settingsGet(0)!!, cb(0)))
    h.step()
    check(h.scheduler.enqueue(P.memoryList(0, 0)!!, cb(4)))                // PULL
    check(h.scheduler.enqueue(P.statusSnapshot(), cb(3)))                  // POLL
    check(h.scheduler.enqueue(P.settingsSetPages(settingsDocument())!![0], cb(2)))  // SAVE
    check(h.scheduler.enqueue(P.talk(), cb(1)))                            // USER
    h.runUntilIdle()
    check(order == listOf(0, 1, 2, 3, 4)) { "lane order $order" }
    check(h.robot.maxOutstanding == 1) { "exactly one request in flight, saw ${h.robot.maxOutstanding}" }
    check(h.robot.phoneCounters == listOf(1L, 2L, 3L, 4L, 5L) && h.robot.robotCounters == listOf(1L, 2L, 3L, 4L, 5L)) { "counters rise strictly per direction" }

    // Continuation pages stay ahead of a user action queued while the upload is under way.
    val h2 = LinkHarness(); h2.robot.script = documentRobot(settingsDocument(1000))
    val order2 = ArrayList<String>()
    BridgeSettingsClient(h2.transport).writeSettings(settingsDocument(1000)) { order2 += "set" }
    h2.step()
    h2.scheduler.enqueue(P.talk()) { order2 += "talk" }
    h2.runUntilIdle()
    check(h2.robot.log.map { it.kind } == listOf(P.KIND_SETTINGS_SET, P.KIND_SETTINGS_SET, P.KIND_SETTINGS_SET, P.KIND_COMMAND) && order2 == listOf("set", "talk")) { "set pages interleaved: ${h2.robot.log.map { it.kind }}" }

    // Capacity is bounded and a full queue is reported to the caller.
    val full = RobotRequestScheduler(2)
    check(full.enqueue(P.talk(), noop()) && full.enqueue(P.talk(), noop()) && !full.enqueue(P.talk(), noop()))
    check(RequestPriority.mutating(P.KIND_COMMAND) && RequestPriority.mutating(P.KIND_SECRET_SET) && !RequestPriority.mutating(P.KIND_SETTINGS_GET) &&
        !RequestPriority.mutating(P.KIND_STATUS_SNAPSHOT) && !RequestPriority.mutating(P.KIND_INTEGRATIONS_STATUS))
    check((P.KIND_SETTINGS_GET..P.KIND_INTEGRATIONS_STATUS).all { RequestPriority.forKind(it) in RequestPriority.CONTINUATION..RequestPriority.PULL })
}

private fun verifyRetriesAndTimeouts() {
    // Read request: lost reply -> re-sent once with a NEW counter -> then TIMEOUT.
    val h = LinkHarness(); h.robot.script = { FakeRobot.Reply.Drop }
    var out: SettingsOutcome<P.Response>? = null
    h.scheduler.enqueue(P.statusSnapshot()) { out = it }
    h.runUntilIdle()
    check(h.robot.log.size == 2 && h.robot.log[1].counter > h.robot.log[0].counter) { "retry must use a new counter" }
    check((out as SettingsOutcome.Fail).failure.reason == SettingsFailure.Reason.TIMEOUT && !h.scheduler.needsReread)
    check(h.robot.log[1].atMs - h.robot.log[0].atMs >= P.DEFAULT_TIMEOUT_MS)

    // Mutating request: lost reply -> UNCERTAIN, never re-sent, caller must re-read.
    val m = LinkHarness(); m.robot.script = { FakeRobot.Reply.Drop }
    val wifi = P.secretWifi(0, "HomeNet", "correct horse", false)!!
    var secretOut: SettingsOutcome<P.Response>? = null
    m.scheduler.enqueue(wifi) { secretOut = it }
    m.runUntilIdle()
    check(m.robot.log.size == 1 && (secretOut as SettingsOutcome.Fail).failure.reason == SettingsFailure.Reason.UNCERTAIN && m.scheduler.needsReread)
    check(wifi.payloadFor(m.robot.sessionId)!!.all { it == 0.toByte() }) { "secret buffer wiped after failure" }
    m.scheduler.acknowledgeReread(); check(!m.scheduler.needsReread)

    // A lost SettingsSet page is also UNCERTAIN and not re-sent.
    val s = LinkHarness(); s.robot.script = { FakeRobot.Reply.Drop }
    var setOut: SettingsOutcome<Boolean>? = null
    BridgeSettingsClient(s.transport).writeSettings(settingsDocument(647)) { setOut = it }
    s.runUntilIdle()
    check((setOut as SettingsOutcome.Fail).failure.reason == SettingsFailure.Reason.UNCERTAIN && s.robot.log.size == 1)

    // Busy: short backoff and a fresh counter each time, then success.
    val b = LinkHarness(); var busyCount = 0
    b.robot.script = { if (busyCount++ < 2) FakeRobot.Reply.Respond(P.Status.BUSY.code) else FakeRobot.Reply.Respond() }
    var busyOut: SettingsOutcome<P.Response>? = null
    b.scheduler.enqueue(P.talk()) { busyOut = it }
    b.runUntilIdle()
    check((busyOut as SettingsOutcome.Ok).value.statusCode == 0 && b.robot.log.size == 3)
    check(b.robot.log.map { it.counter } == listOf(1L, 2L, 3L) && b.robot.log[1].atMs - b.robot.log[0].atMs >= RobotRequestScheduler.BUSY_BACKOFF_MS)
    // Persistent Busy gives up after the retry budget and reports the Busy status to the caller.
    val bb = LinkHarness(); bb.robot.script = { FakeRobot.Reply.Respond(P.Status.BUSY.code) }
    var persistent: SettingsOutcome<P.Response>? = null
    bb.scheduler.enqueue(P.talk()) { persistent = it }
    bb.runUntilIdle()
    check(bb.robot.log.size == 1 + RobotRequestScheduler.MAX_BUSY_RETRIES && (persistent as SettingsOutcome.Ok).value.statusCode == P.Status.BUSY.code)
    var viaClient: SettingsOutcome<P.Response>? = null
    val bc = LinkHarness(); bc.robot.script = { FakeRobot.Reply.Respond(P.Status.BUSY.code) }
    BridgeSettingsClient(bc.transport).call(P.talk()) { viaClient = it }
    bc.runUntilIdle()
    check((viaClient as SettingsOutcome.Fail).failure.status == P.Status.BUSY)

    // A response with the wrong kind, counter or session is ignored; a late answer after a timeout is ignored too.
    val w = LinkHarness(); w.robot.script = { FakeRobot.Reply.Drop }
    w.scheduler.enqueue(P.statusSnapshot(), noop()); w.step()
    val c = w.robot.log[0].counter
    check(!w.scheduler.onResponse(P.Response(c, 8, 0, ByteArray(0)), w.now, w.robot.sessionId))
    check(!w.scheduler.onResponse(P.Response(c + 1, 11, 0, ByteArray(0)), w.now, w.robot.sessionId))
    check(!w.scheduler.onResponse(P.Response(c, 11, 0, ByteArray(0)), w.now, w.robot.sessionId + 1))
    check(w.scheduler.onResponse(P.Response(c, 11, 0, ByteArray(0)), w.now, w.robot.sessionId))
}

private fun verifyClientOverFakeRobot() {
    // Full multi-page SettingsGet/Set through envelopes, framing at MTU 23 and the scheduler.
    val doc = settingsDocument(1800)
    val h = LinkHarness(); h.robot.script = documentRobot(doc)
    var read: SettingsOutcome<ByteArray>? = null
    BridgeSettingsClient(h.transport).readSettings { read = it }
    h.runUntilIdle()
    check((read as SettingsOutcome.Ok).value.contentEquals(doc) && h.robot.log.size == P.pageCount(doc.size))
    var written: SettingsOutcome<Boolean>? = null
    BridgeSettingsClient(h.transport).writeSettings(doc) { written = it }
    h.runUntilIdle()
    check((written as SettingsOutcome.Ok).value)

    // SequenceError on a later page restarts the upload at page 0 (bounded), then succeeds.
    val r = LinkHarness(); var failedOnce = false
    r.robot.script = { rx ->
        val page = rx.payload[0].toInt()
        if (rx.kind == P.KIND_SETTINGS_SET && page == 1 && !failedOnce) { failedOnce = true; FakeRobot.Reply.Respond(P.Status.SEQUENCE_ERROR.code) }
        else FakeRobot.Reply.Respond(0, byteArrayOf(page.toByte(), if (page == 1) 1 else 0))
    }
    var restarted: SettingsOutcome<Boolean>? = null
    BridgeSettingsClient(r.transport).writeSettings(settingsDocument(647)) { restarted = it }
    r.runUntilIdle()
    check((restarted as SettingsOutcome.Ok).value && r.robot.log.map { it.payload[0].toInt() } == listOf(0, 1, 0, 1))
    val always = LinkHarness(); always.robot.script = { rx -> if (rx.payload[0].toInt() == 0) FakeRobot.Reply.Respond(0, byteArrayOf(0, 0)) else FakeRobot.Reply.Respond(P.Status.CRC_MISMATCH.code) }
    var gaveUp: SettingsOutcome<Boolean>? = null
    BridgeSettingsClient(always.transport).writeSettings(settingsDocument(647)) { gaveUp = it }
    always.runUntilIdle()
    check((gaveUp as SettingsOutcome.Fail).failure.status == P.Status.CRC_MISMATCH && always.robot.log.size == 6)

    // ContentRejected / Timeout injected on memory add are mapped by the client without retrying the write.
    val m = LinkHarness(); m.robot.script = { FakeRobot.Reply.Respond(P.Status.CONTENT_REJECTED.code) }
    var rejected: SettingsOutcome<Pair<Long, Long>>? = null
    BridgeSettingsClient(m.transport).addMemory(0, "nama", "isi") { rejected = it }
    m.runUntilIdle()
    check((rejected as SettingsOutcome.Fail).failure.status == P.Status.CONTENT_REJECTED && m.robot.log.size == 1)

    // A damaged reply envelope (MAC failure) is dropped by the phone and the request times out as a read retry.
    val t = LinkHarness(); var tampered = 0
    t.robot.script = { if (tampered++ == 0) FakeRobot.Reply.Tamper else FakeRobot.Reply.Respond(0, pages(statusDoc())[0]) }
    var snap: SettingsOutcome<P.StatusSnapshot>? = null
    BridgeSettingsClient(t.transport).readStatus { snap = it }
    t.runUntilIdle()
    check((snap as SettingsOutcome.Ok).value.schema == 3 && t.robot.log.size == 2)
}

private fun verifyDisconnectAndSessions() {
    // Link drops while SettingsSet is mid-flight: in-flight => UNCERTAIN, queued => NOT_CONNECTED, secrets wiped, re-read required.
    val h = LinkHarness(); h.robot.script = { rx -> if (rx.kind == P.KIND_SETTINGS_SET) FakeRobot.Reply.Disconnect else FakeRobot.Reply.Respond() }
    var set: SettingsOutcome<Boolean>? = null
    val queued = ArrayList<SettingsOutcome<P.Response>>()
    val secret = P.secretGemini("AIzaSyDISCONNECT-key_1")!!
    h.scheduler.enqueue(secret) { queued += it }
    BridgeSettingsClient(h.transport).writeSettings(settingsDocument(1200)) { set = it }
    h.runUntilIdle()
    // Secret (USER lane) went first and completed; the SettingsSet page 0 then hit the drop.
    check(queued.single() is SettingsOutcome.Ok && secret.payloadFor(h.robot.sessionId)!!.all { it == 0.toByte() })
    check((set as SettingsOutcome.Fail).failure.reason == SettingsFailure.Reason.UNCERTAIN && h.scheduler.needsReread && !h.scheduler.inFlight)
    var after: SettingsOutcome<P.Response>? = null
    h.robot.dropped = false
    h.scheduler.enqueue(P.talk()) { after = it }
    h.scheduler.onDisconnected()
    check((after as SettingsOutcome.Fail).failure.reason == SettingsFailure.Reason.NOT_CONNECTED)

    // Queued secrets are wiped and fail NOT_CONNECTED when the session ends before they were sent.
    val g = RobotRequestScheduler(); val wifi = P.secretWifi(1, "Net", "pw-secret", false)!!
    var wifiOut: SettingsOutcome<P.Response>? = null
    g.enqueue(wifi) { wifiOut = it }; g.onDisconnected()
    check((wifiOut as SettingsOutcome.Fail).failure.reason == SettingsFailure.Reason.NOT_CONNECTED && wifi.payloadFor(1)!!.all { it == 0.toByte() })

    // New session: counters restart at 1 for both directions and a late reply of the old session is ignored.
    val old = LinkHarness(); old.robot.script = { FakeRobot.Reply.Drop }
    old.scheduler.enqueue(P.statusSnapshot(), noop()); old.step()
    val staleCounter = old.robot.log[0].counter
    val fresh = LinkHarness(FakeRobot(key = ByteArray(32) { (it + 9).toByte() }, sessionId = 0x0A0B0C0DL))
    fresh.robot.script = { FakeRobot.Reply.Respond(0, ByteArray(0), delayMs = 100) }
    fresh.scheduler.enqueue(P.statusSnapshot(), noop()); fresh.step()
    check(fresh.robot.phoneCounters == listOf(1L) && staleCounter == 1L)
    check(!fresh.scheduler.onResponse(fresh.robot.staleResponseFor(P.KIND_STATUS_SNAPSHOT, 1), fresh.now, old.robot.sessionId)) { "old-session reply must be dropped" }
    check(fresh.scheduler.inFlight)
    fresh.runUntilIdle(); check(!fresh.scheduler.inFlight)

    // Replayed response envelopes are refused by the phone receiver (strict counters).
    val rp = LinkHarness(); rp.robot.script = { FakeRobot.Reply.Respond() }
    rp.scheduler.enqueue(P.talk(), noop()); rp.step()
    rp.robot.deliverDue(rp.now + 10, rp.scheduler, replayDuplicates = true)

    // OTA: ota-start accepted, the BLE link then goes away; after reconnect a new session reads the result from the snapshot.
    val ota = LinkHarness(); ota.robot.script = { rx -> if (rx.kind == P.KIND_COMMAND) FakeRobot.Reply.Respond() else FakeRobot.Reply.Respond(0, pages(statusDoc(P.OtaState.FLASHING_S3.code))[0]) }
    var otaOut: SettingsOutcome<P.Response>? = null
    ota.scheduler.enqueue(P.otaStart()) { otaOut = it }
    ota.runUntilIdle()
    check((otaOut as SettingsOutcome.Ok).value.statusCode == 0)
    ota.scheduler.onDisconnected()
    val back = LinkHarness(FakeRobot(sessionId = 0x11223344L)); back.robot.script = { FakeRobot.Reply.Respond(0, pages(statusDoc(P.OtaState.DONE.code))[0]) }
    var result: SettingsOutcome<P.StatusSnapshot>? = null
    BridgeSettingsClient(back.transport).readStatus { result = it }
    back.runUntilIdle()
    check((result as SettingsOutcome.Ok).value.otaState == P.OtaState.DONE.code)
}

private fun verifyPoller() {
    val h = LinkHarness(); h.robot.script = documentRobot(settingsDocument(), statusDoc())
    val client = BridgeSettingsClient(h.transport)
    val poller = StatusPoller(client, h.scheduler, 5_000L)
    check(!poller.tick(h.now)) { "inactive until a screen needs status" }
    poller.screenNeedsStatus = true; poller.connected = true
    check(!poller.tick(h.now)) { "stops in the background" }
    poller.foreground = true
    check(poller.tick(h.now)); check(!poller.tick(h.now)) { "one poll outstanding" }
    h.runUntilIdle(); check(poller.latest.value?.schema == 3)
    check(!poller.tick(h.now + 1_000)) { "interval not elapsed" }
    h.advance(5_000)
    // A queued write pauses polling; the poll resumes once the write is done.
    h.scheduler.enqueue(P.talk(), noop())
    check(!poller.tick(h.now)) { "paused while a write is queued" }
    h.runUntilIdle()
    check(poller.tick(h.now)); h.runUntilIdle()
    poller.foreground = false; h.advance(6_000)
    check(!poller.tick(h.now)) { "background stops polling" }
    check(h.robot.log.count { it.kind == P.KIND_STATUS_SNAPSHOT } == 2)
}

private fun verifyFuzz() {
    // Random garbage must never crash parsing or complete a request it does not match.
    val rnd = java.util.Random(42)
    val sched = RobotRequestScheduler(); sched.onSessionStart(7)
    var done = 0
    sched.enqueue(P.statusSnapshot()) { done++ }
    sched.startNext(0) { 5L }
    var accepted = 0
    repeat(5000) {
        val bytes = ByteArray(rnd.nextInt(520)).also { rnd.nextBytes(it) }
        val response = try { P.parseResponse(bytes) } catch (e: Exception) { error("parseResponse threw: $e") }
        if (response != null && sched.onResponse(response, 0, 7)) accepted++
        try { P.parsePage(bytes); P.parseStatus(bytes); P.parseMemoryWindow(bytes); P.parseReminderDocument(bytes); P.parseIrProfiles(bytes) } catch (e: Exception) { error("parser threw: $e") }
        try { P.parseIntegrationsConfig(bytes); P.parseIntegrationsStatus(bytes) } catch (e: Exception) { error("parser threw: $e") }
        BridgeGattFraming.Reassembler().accept(bytes, 0)
    }
    check(accepted <= 1 && (accepted == 0) == sched.inFlight && done == accepted)
}
