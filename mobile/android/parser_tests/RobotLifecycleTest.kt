package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

/** I5 / A10 lifecycle logic against FakeRobot: reboot, OTA wait + result, factory reset, notices, replay/revoke without loops. */
fun verifyRobotLifecycle() {
    verifyOtaTracker()
    verifyOtaOverFakeRobot()
    verifyRebootAndFactoryReset()
    verifyNoticesAndGuards()
    verifyRejectedAck()
    verifyPairingFlow()
    println("PASS: lifecycle (OTA wait/result per otaError, reboot S3/C3, factory reset, notices, handshake/replay guards) over FakeRobot")
}

private fun snap(ota: Int = 0, err: Int = 0, link: Int = 3, version: Long = 20261009) =
    P.parseStatus(status(ota = ota, otaError = err).also { it[37] = link.toByte(); le32(version).copyInto(it, 1) })!!

private fun tracker(clock: LongArray) = OtaTracker({ clock[0] }, { s -> RobotRepository(BridgeSettingsClient(SettingsTransport { r, c -> r.wipe(); c(SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.NOT_CONNECTED))) })).otaResult(s) })

private fun verifyOtaTracker() {
    val t = longArrayOf(1_000)
    val o = tracker(t)
    check(!o.tracking && !o.active)
    o.onStatus(snap(ota = 3)); check(o.active) { "running update seen while idle is adopted (lost ota-start answer / app restart)" }
    o.dismiss(); check(!o.tracking)
    // A stale DONE from an earlier run, still connected and shortly after start, is ignored.
    o.start(); o.onStatus(snap(ota = 6)); check(o.active && o.result == null)
    // BLE drops (normal), reconnect, first snapshot decides.
    o.onLink(false); t[0] += 90_000; o.onLink(true); o.onStatus(snap(ota = 4)); check(o.active) { "still running after reconnect" }
    o.onStatus(snap(ota = 6)); check(o.result?.kind == OtaResult.Kind.DONE && "Selesai" in o.result!!.message && !o.active)
    o.onStatus(snap(ota = 7, err = 6)); check(o.result?.kind == OtaResult.Kind.DONE) { "first settled result sticks" }
    // Every otaError code has its own text.
    val labels = (listOf(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 255)).map { code ->
        val x = tracker(longArrayOf(1_000)); x.start(); x.onLink(false); x.onLink(true); x.onStatus(snap(ota = 7, err = code))
        check(x.result?.kind == OtaResult.Kind.FAILED) { "failed $code" }
        x.result!!.message
    }
    check(labels.toSet().size == labels.size && labels.none { "Kode" in it }) { "per-error messages: $labels" }
    // UP_TO_DATE.
    val u = tracker(t); u.start(); u.onLink(false); u.onLink(true); u.onStatus(snap(ota = 8)); check(u.result?.kind == OtaResult.Kind.UP_TO_DATE)
    // 10 minutes offline -> "check the robot"; connected-but-running has its own text; nothing before the limit.
    val w = longArrayOf(5_000); val a = tracker(w); a.start(); a.onLink(false)
    w[0] += OtaTracker.WAIT_MS; a.tick(); check(a.active)
    w[0] += 1; a.tick(); check(a.result?.kind == OtaResult.Kind.FAILED && "Belum tersambung" in a.result!!.message && "10 menit" in a.result!!.message)
    val b = tracker(w); b.start(); b.onStatus(snap(ota = 3)); w[0] += OtaTracker.WAIT_MS + 1; b.tick(); check("belum selesai" in b.result!!.message)
    a.dismiss(); check(!a.tracking && a.result == null)
}

/** ota-start answered, then the C3 goes away; after the reconnect the next snapshot carries the result. */
private fun verifyOtaOverFakeRobot() {
    val model = RobotModel("{\"schema\":1}".toByteArray())
    val h = LinkHarness().also { it.robot.script = { rx -> model.reply(rx).also { if (rx.kind == P.KIND_COMMAND && rx.payload[0].toInt() == 3) model.snapshot = status(ota = 3) } } }
    val repo = RobotRepository(BridgeSettingsClient(h.transport))
    val clock = longArrayOf(1_000)
    val ota = tracker(clock)
    fun <T> run(call: (Done<T>) -> Unit): Res<T> { var r: Res<T>? = null; call { r = it }; h.runUntilIdle(); return r!! }
    check(run<Unit> { repo.startOta(it) } is Res.Ok)
    check(h.robot.log.last().payload[0].toInt() == 3) { "ota-start is Command op 3 (no URL, no binary)" }
    ota.start()
    h.robot.dropped = true; ota.onLink(false)                      // C3 restarts: BLE gone
    val whileDown = run<StatusView> { repo.readStatus(it) }
    check(whileDown is Res.Err && whileDown.message.contains("menyambung ulang")) { "no write/read while offline" }
    clock[0] += 4 * 60_000; h.now += 4 * 60_000
    h.robot.dropped = false; h.scheduler.onSessionStart(h.robot.sessionId); ota.onLink(true)                      // reconnect, new link
    model.snapshot = status(ota = 6)
    val v = (run<StatusView> { repo.readStatus(it) } as Res.Ok).value
    ota.onStatus(v.s)
    check(ota.result?.kind == OtaResult.Kind.DONE) { "result read after reconnect" }
    // Failure path (Wi-Fi off: robot never restarts, BLE stays up): terminal state is ignored for 30 s, then shown.
    val c = tracker(clock); c.start(); model.snapshot = status(ota = 7, otaError = 1)
    c.onStatus(P.parseStatus(model.snapshot)!!); check(c.active)
    clock[0] += OtaTracker.SETTLE_MS + 1; c.onStatus(P.parseStatus(model.snapshot)!!)
    check(c.result?.kind == OtaResult.Kind.FAILED && "Wi-Fi" in c.result!!.message)
}

private fun verifyRebootAndFactoryReset() {
    val model = RobotModel("{\"schema\":1}".toByteArray())
    var dropOn: Int? = null
    val h = LinkHarness().also { it.robot.script = { rx -> if (rx.kind == P.KIND_COMMAND && rx.payload[0].toInt() == dropOn) FakeRobot.Reply.Disconnect else model.reply(rx) } }
    val repo = RobotRepository(BridgeSettingsClient(h.transport))
    fun <T> run(call: (Done<T>) -> Unit): Res<T> { var r: Res<T>? = null; call { r = it }; h.runUntilIdle(); return r!! }
    // S3 reboot: answered; the link to the phone survives, only the C3<->S3 flag drops and returns.
    check(run<Unit> { repo.reboot(true, false, it) } is Res.Ok && h.robot.log.last().payload.map { it.toInt() } == listOf(1, 2))
    model.snapshot = status().also { it[37] = 0 }
    val down = (run<StatusView> { repo.readStatus(it) } as Res.Ok).value
    val link = LinkState(LinkPhase.READY)
    check(LinkNotices.compute(link, true, true, true, false, down.s)?.kind == UiNotice.Kind.S3_LINK)
    model.snapshot = status().also { it[37] = 3 }
    val up = (run<StatusView> { repo.readStatus(it) } as Res.Ok).value
    check(LinkNotices.compute(link, true, true, true, false, up.s) == null)
    // C3 reboot: BLE drops instead of answering; that is the expected outcome, not an error.
    dropOn = 1
    check(run<Unit> { repo.reboot(false, true, it) } is Res.Ok)
    check(h.robot.dropped)
    check(LinkNotices.compute(LinkState(LinkPhase.RECONNECTING, 1), true, true, true, false, null)?.kind == UiNotice.Kind.RECONNECTING)
    // A lost answer to an S3-only reboot stays uncertain (the link should have survived).
    h.robot.dropped = false; h.scheduler.onSessionStart(h.robot.sessionId); dropOn = 1
    val s3Lost = run<Unit> { repo.reboot(true, false, it) }
    check(s3Lost is Res.Err && s3Lost.uncertain)
    // Factory reset: the token is checked locally first, then Command op 2.
    h.robot.dropped = false; h.scheduler.onSessionStart(h.robot.sessionId); dropOn = null
    val before = h.robot.log.size
    check(run<Unit> { repo.factoryReset("wipe", it) } is Res.Err && h.robot.log.size == before)
    check(run<Unit> { repo.factoryReset("WIPE", it) } is Res.Ok && h.robot.log.last().payload[0].toInt() == 2)
    // Answer lost: not reported as success (the app keeps credentials until the user forgets the robot).
    dropOn = 2
    val lost = run<Unit> { repo.factoryReset("WIPE", it) }
    check(lost is Res.Err && lost.uncertain)
}

private fun verifyNoticesAndGuards() {
    val ready = LinkState(LinkPhase.READY)
    val off = LinkState(LinkPhase.OFFLINE)
    val s = snap()
    fun n(link: LinkState = ready, robot: Boolean = true, bt: Boolean = true, perm: Boolean = true, ota: Boolean = false, st: P.StatusSnapshot? = s) =
        LinkNotices.compute(link, robot, bt, perm, ota, st)?.kind
    check(n() == null)
    check(n(robot = false) == UiNotice.Kind.NO_ROBOT && n(perm = false) == UiNotice.Kind.PERMISSION && n(bt = false) == UiNotice.Kind.BLUETOOTH_OFF)
    check(n(link = off) == UiNotice.Kind.RECONNECTING && n(link = off, ota = true) == UiNotice.Kind.UPDATING)
    check(LinkNotices.compute(off, true, true, true, true, s)!!.bad.not())
    check(n(link = LinkState(LinkPhase.ERROR, 0, "x")) == UiNotice.Kind.SECURITY)
    check(n(link = LinkState(LinkPhase.RECONNECTING, 2, handshakeFailures = 2)) == UiNotice.Kind.RECONNECTING)
    check(n(link = LinkState(LinkPhase.RECONNECTING, 3, handshakeFailures = 3)) == UiNotice.Kind.HANDSHAKE_STUCK)
    check("iPhone" in LinkNotices.compute(LinkState(LinkPhase.RECONNECTING, 3, handshakeFailures = 3), true, true, true, false, null)!!.text)
    check(n(st = snap(link = 0)) == UiNotice.Kind.S3_LINK && n(st = null) == null)
    // Re-pairing: offered with an action when the robot opens a pairing window (reset), on repeated handshake failures, security errors,
    // or a stale bond only while the robot is actually seen; never while connected, and the pairing beacon marker is parsed strictly.
    fun repair(link: LinkState, seen: Boolean = false, open: Boolean = false) = LinkNotices.compute(link, true, true, true, false, s, false, seen, open)
    check(repair(off, open = true)!!.kind == UiNotice.Kind.REPAIR && repair(off, open = true)!!.action == LinkNotices.REPAIR_ACTION)
    check(repair(ready, open = true) == null)
    check(repair(LinkState(LinkPhase.RECONNECTING, 3, handshakeFailures = 3))!!.action == LinkNotices.REPAIR_ACTION)
    check(repair(LinkState(LinkPhase.ERROR, 0, "x"))!!.action == LinkNotices.REPAIR_ACTION)
    check(repair(off.copy(staleBond = true), seen = false)!!.kind == UiNotice.Kind.RECONNECTING && repair(off.copy(staleBond = true), seen = true)!!.kind == UiNotice.Kind.REPAIR)
    check(repair(off)!!.action == null)
    // A selected robot without a trust record: the app stays idle and offers to start pairing (no background pairing requests).
    val unpaired = LinkNotices.compute(off, true, true, true, false, s, paired = false)!!
    check(unpaired.kind == UiNotice.Kind.NOT_PAIRED && unpaired.action == LinkNotices.PAIR_ACTION)
    check(LinkNotices.compute(off, false, true, true, false, s, paired = false)!!.kind == UiNotice.Kind.NO_ROBOT)
    check(LinkNotices.compute(off, true, false, true, false, s, paired = false)!!.kind == UiNotice.Kind.BLUETOOTH_OFF)
    check(PairingBeacon.parse(byteArrayOf(0x52, 0x44, 1)) == true && PairingBeacon.parse(byteArrayOf(0x52, 0x44, 0)) == false)
    check(PairingBeacon.parse(null) == null && PairingBeacon.parse(byteArrayOf(0x52, 0x44)) == null && PairingBeacon.parse(byteArrayOf(0x41, 0x44, 1)) == null)
    // The notice text never carries link causes or anything robot-supplied.
    check("secret" !in LinkNotices.compute(LinkState(LinkPhase.ERROR, 0, "secret"), true, true, true, false, null)!!.text)
    // Mapper keeps the failure counter; the guard counts handshakes that died and resets on READY.
    check(LinkPhaseMapper.map(BridgeConnectionPhase.OFFLINE, 2, "x", 3).handshakeFailures == 3 && LinkPhaseMapper.map(BridgeConnectionPhase.READY, 0, null).handshakeFailures == 0)
    val g = HandshakeGuard()
    repeat(2) { g.onHandshakeFailed() }; check(!g.stuck)
    g.onHandshakeFailed(); check(g.stuck); g.onReady(); check(!g.stuck && g.failures == 0)
    // Bluetooth off/on and reconnect: 1 s doubling to 30 s, reset by BT on / READY.
    val b = ReconnectBackoff()
    check(generateSequence { b.next() }.take(7).toList() == listOf(1000L, 2000L, 4000L, 8000L, 16000L, 30000L, 30000L))
    b.reset(); check(b.next() == 1000L)
    // Session refusal guard: one resync, then stop.
    val r = SessionRejectGuard()
    check(r.onRejected() == SessionRejectGuard.Action.RESYNC && r.onRejected() == SessionRejectGuard.Action.RESYNC)
    check(r.onRejected() == SessionRejectGuard.Action.GIVE_UP)
    r.onAccepted(); check(r.onRejected() == SessionRejectGuard.Action.RESYNC)
}

/** Replay / revoke seen from the phone: a refused envelope fails the request at once, is never re-sent, and cannot loop. */
private fun verifyRejectedAck() {
    val h = LinkHarness().also { it.robot.script = { FakeRobot.Reply.Drop } }
    var out: SettingsOutcome<P.Response>? = null
    check(h.scheduler.enqueue(P.talk()) { out = it })
    h.step()
    check(h.robot.log.size == 1 && h.scheduler.inFlight)
    val counter = h.robot.phoneCounters.last()
    check(!h.scheduler.onRejectedAck(counter + 1) && !h.scheduler.onRejectedAck(counter, 999L)) { "wrong counter / old session ignored" }
    check(h.scheduler.onRejectedAck(counter))
    val fail = (out as SettingsOutcome.Fail).failure
    check(fail.reason == SettingsFailure.Reason.REJECTED && !h.scheduler.needsReread && !h.scheduler.inFlight)
    h.advance(60_000)
    check(h.robot.log.size == 1) { "a refused request is never re-sent automatically" }
    check(!h.scheduler.onRejectedAck(counter)) { "a second refusal for the same counter is ignored" }
    // A refused read is not retried either (unlike a timeout).
    var read: SettingsOutcome<P.Response>? = null
    check(h.scheduler.enqueue(P.statusSnapshot()) { read = it })
    h.step(); h.scheduler.onRejectedAck(h.robot.phoneCounters.last())
    h.advance(30_000)
    check(h.robot.log.size == 2 && (read as SettingsOutcome.Fail).failure.reason == SettingsFailure.Reason.REJECTED)
}

/** Pairing: identity is empty until the robot approves the bond (must be waited for, not a failure); fast retry only during the search. */
private fun verifyPairingFlow() {
    fun identity(flags: Long): ByteArray = ByteArray(26).also {
        it[0] = 2; it[1] = 1
        for (i in 0..3) it[2 + i] = (flags shr (8 * i)).toByte()
        for (i in 0 until 16) it[6 + i] = (0x30 + i).toByte()
    }
    check(IdentityGate.parse(ByteArray(0)) === IdentityGate.Parsed.AwaitingApproval) { "empty identity = bonded but not approved yet" }
    check(IdentityGate.parse(ByteArray(25)) === IdentityGate.Parsed.Invalid && IdentityGate.parse(identity(5).also { it[1] = 2 }) === IdentityGate.Parsed.Invalid)
    val ok = IdentityGate.parse(identity(5)) as IdentityGate.Parsed.Ok
    check(ok.flags == 5L && ok.robotId.size == 16 && ok.robotId[0] == 0x30.toByte())
    // flags: bit0 ready, bit1 active key, bit2 enrollment window (opened on approve).
    check(IdentityGate.decide(5L, false) == IdentityGate.Verdict.PROCEED) { "enrollment window: first handshake without a stored key" }
    check(IdentityGate.decide(3L, true) == IdentityGate.Verdict.PROCEED && IdentityGate.decide(3L, false) == IdentityGate.Verdict.KEY_MISSING)
    check(IdentityGate.decide(1L, false) == IdentityGate.Verdict.NO_CREDENTIAL) { "approved but no Android enrollment (iPhone mode)" }
    val s = PairingSearch(120_000L)
    check(!s.active(0)); s.begin(1_000)
    check(s.active(1_001) && s.active(120_999) && !s.active(121_000)); s.end(); check(!s.active(1_500))
    check(PairingSearch.RETRY_MS < 5_000L && PairingSearch.APPROVAL_MS > 60_000L && PairingSearch.OPERATION_MS > 20_000L)
    // A rejected try during the window must not feed the handshake guard; the failure/stage line travels with the link state.
    LinkPhaseMapper.map(BridgeConnectionPhase.PAIRING, 0, "x", 0, "menunggu").let { check(it.phase == LinkPhase.BONDING && it.detail == "menunggu") }
    check(LinkPhaseMapper.map(BridgeConnectionPhase.OFFLINE, 4, null, 0, "gagal").detail == "gagal" && LinkState().detail == null)
}
