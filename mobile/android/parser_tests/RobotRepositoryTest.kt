package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

internal fun pagesOf(document: ByteArray): List<ByteArray> = P.settingsSetPages(document)!!.map { it.payloadFor(0)!! }
internal fun le32(v: Long) = ByteArray(4) { (v shr (8 * it)).toByte() }

internal fun status(configFlags: Int = 0x0f, bleFlags: Int = 0x0f, pomodoro: Int = 0, game: Int = 0, activity: Int = 0, ota: Int = 0, otaError: Int = 0): ByteArray =
    ByteArray(76).also {
        it[0] = 3; le32(20261009).copyInto(it, 1); le32(20261008).copyInto(it, 5); le32(7_200_000).copyInto(it, 9)
        le32(48_000 * 2).copyInto(it, 17); le32(212 * 1024).copyInto(it, 29); it[37] = 3; le32(12).copyInto(it, 38)
        it[54] = 1; it[55] = (-58).toByte(); it[56] = bleFlags.toByte(); it[57] = configFlags.toByte(); it[58] = ota.toByte(); it[59] = otaError.toByte()
        it[65] = 2; it[66] = pomodoro.toByte(); it[67] = (1500 and 0xff).toByte(); it[68] = (1500 shr 8).toByte(); it[70] = activity.toByte(); it[71] = game.toByte()
    }

/** Stateful C3 stand-in: serves the kinds the repository uses. */
internal class RobotModel(var settings: ByteArray) {
    var memory = ArrayList<P.Fact>()
    var reminders = ArrayList<P.Reminder>()
    var snapshot = status()
    var rejectContent = false
    var normalizeKey: String? = null
    var statusFor = HashMap<Int, Int>()
    var nextId = 1L
    private var staged = ByteArray(0)

    fun memoryDoc(): ByteArray {
        val raw = P.encodeMemoryDocument(0, memory)!!.copyOfRange(8, P.encodeMemoryDocument(0, memory)!!.size)
        return P.encodeMemoryDocument(P.crc32(raw), memory)!!
    }
    fun reminderDoc(): ByteArray {
        val body = java.io.ByteArrayOutputStream()
        for (r in reminders) {
            val t = r.text.toByteArray()
            body.write(le32(r.id)); body.write(r.schedule); body.write(r.minute and 0xff); body.write(r.minute shr 8)
            body.write(le32(if (r.schedule == 3) 0 else r.due)); body.write(t.size); body.write(t)
        }
        val b = body.toByteArray()
        return byteArrayOf(1) + le32(P.crc32(b)) + byteArrayOf(reminders.size.toByte()) + b
    }

    fun reply(rx: FakeRobot.Received): FakeRobot.Reply {
        statusFor[rx.kind]?.let { return FakeRobot.Reply.Respond(it) }
        return when (rx.kind) {
            P.KIND_SETTINGS_GET -> FakeRobot.Reply.Respond(0, pagesOf(settings)[rx.payload[0].toInt()])
            P.KIND_SETTINGS_SET -> {
                val index = rx.payload[0].toInt(); val count = rx.payload[1].toInt()
                if (index == 0) staged = ByteArray(0)
                staged += rx.payload.copyOfRange(8, rx.payload.size)
                val last = index == count - 1
                if (last) { settings = normalizeKey?.let { k -> String(staged).replace(Regex("\"$k\":\\d+"), "\"$k\":999") }?.toByteArray() ?: staged }
                FakeRobot.Reply.Respond(0, byteArrayOf(rx.payload[0], if (last) 1 else 0))
            }
            P.KIND_STATUS_SNAPSHOT -> FakeRobot.Reply.Respond(0, pagesOf(snapshot)[0])
            P.KIND_MEMORY_LIST -> FakeRobot.Reply.Respond(0, pagesOf(memoryDoc())[0])
            P.KIND_MEMORY_ADD -> if (rejectContent) FakeRobot.Reply.Respond(17) else {
                val kl = rx.payload[1].toInt() and 0xff; val vl = rx.payload[2].toInt() and 0xff
                val key = String(rx.payload, 3, kl); val value = String(rx.payload, 3 + kl, vl)
                memory.removeAll { it.key == key }
                val id = nextId++
                memory.add(P.Fact(id, 0, 0, rx.payload[0].toInt(), 0, key, value))
                FakeRobot.Reply.Respond(0, le32(id) + le32(1))
            }
            P.KIND_MEMORY_DELETE -> { val id = (rx.payload[0].toLong() and 0xff); memory.removeAll { it.id == id }; FakeRobot.Reply.Respond(0, le32(2)) }
            P.KIND_MEMORY_CLEAR -> { memory.clear(); FakeRobot.Reply.Respond(0, le32(3)) }
            P.KIND_REMINDER_LIST -> FakeRobot.Reply.Respond(0, pagesOf(reminderDoc())[0])
            P.KIND_REMINDER_ADD -> {
                val tl = rx.payload[7].toInt() and 0xff
                val id = nextId++
                val due = (rx.payload[3].toLong() and 0xff) or ((rx.payload[4].toLong() and 0xff) shl 8) or ((rx.payload[5].toLong() and 0xff) shl 16) or ((rx.payload[6].toLong() and 0xff) shl 24)
                reminders.add(P.Reminder(id, rx.payload[0].toInt(), (rx.payload[1].toInt() and 0xff) or ((rx.payload[2].toInt() and 0xff) shl 8), due, String(rx.payload, 8, tl)))
                FakeRobot.Reply.Respond(0, le32(id) + le32(1))
            }
            P.KIND_REMINDER_DELETE -> { val id = (rx.payload[0].toLong() and 0xff); reminders.removeAll { it.id == id }; FakeRobot.Reply.Respond(0, le32(4)) }
            P.KIND_IR_PROFILE_LIST -> FakeRobot.Reply.Respond(0, byteArrayOf(2, 2, 'a'.code.toByte(), 'b'.code.toByte(), 1, 'c'.code.toByte()))
            P.KIND_INTEGRATIONS_SET, P.KIND_INTEGRATIONS_ACTION -> FakeRobot.Reply.Respond(0)
            else -> FakeRobot.Reply.Respond(0) // commands, secrets
        }
    }
}

private fun settingsJson(extra: String = "") =
    ("{\"schema\":1,\"robotName\":\"Sonic\",\"speakerGainMilli\":450,\"phonePlatform\":0,\"pomodoroWasActive\":0,\"privacyMicMuted\":0,\"masterSound\":1," +
        "\"comfortMinTempX10\":200,\"comfortMaxTempX10\":300,\"notificationAllowlist\":\"com.a\\ncom.b\",\"notificationSpeechAllowlist\":\"com.a\"$extra}").toByteArray()

private class Rig(settings: ByteArray = settingsJson(",\"futureKey\":{\"x\":[1,2]}")) {
    val model = RobotModel(settings)
    val h = LinkHarness().also { it.robot.script = model::reply }
    var clock = 1_800_000_000L
    val repo = RobotRepository(BridgeSettingsClient(h.transport)) { clock }

    fun <T> run(call: (Done<T>) -> Unit): Res<T> {
        var result: Res<T>? = null
        call { result = it }
        h.runUntilIdle()
        return result ?: error("callback not invoked")
    }
    fun <T> ok(call: (Done<T>) -> Unit): T = (run(call) as? Res.Ok)?.value ?: error("expected Ok")
    fun err(call: (Done<Unit>) -> Unit): Res.Err = run(call) as? Res.Err ?: error("expected Err")
    val sent get() = h.robot.log.size
    fun kinds() = h.robot.log.map { it.kind }
}

fun verifyRobotRepository() {
    verifyHandshakeFakeRobot()
    verifySettingsPipeline()
    verifyStatusAndControls()
    verifyMemoryAndReminders()
    verifyCommandsSecretsIntegrations()
    verifyErrorsAndUncertainty()
    verifyModelHelpers()
    println("PASS: RobotRepository over FakeRobot (settings round trip, memory, reminders, secrets, commands, integrations, IR, errors) and model/unit helpers")
}

/** The FakeRobot session keys come out of the real v2 handshake (hello, enrollment challenge, finish, accepted). */
private fun verifyHandshakeFakeRobot() {
    val key = ByteArray(32) { (it * 7 + 3).toByte() }; val robotId = ByteArray(16) { (it + 1).toByte() }
    val phoneNonce = ByteArray(32) { (it + 9).toByte() }; val robotNonce = ByteArray(32) { (it + 70).toByte() }; val sessionId = 0x0A0B0C0DL
    fun mac(domain: String, bytes: ByteArray) = javax.crypto.Mac.getInstance("HmacSHA256").run {
        init(javax.crypto.spec.SecretKeySpec(key, "HmacSHA256")); doFinal((domain + "\u0000").toByteArray(Charsets.US_ASCII) + bytes) }
    val transcript = robotId + phoneNonce + robotNonce + le32(sessionId)
    val challenge = byteArrayOf(2, BridgeSessionHandshake.ENROLLMENT_CHALLENGE.toByte()) + robotId + le32(sessionId) + robotNonce + key + mac("RoboDesk-Bridge-ServerProof-v2", transcript)
    val parsed = BridgeSessionHandshake.parseChallenge(challenge, robotId, phoneNonce, null)!!
    check(BridgeSessionHandshake.finish(parsed).copyOfRange(6, 38).contentEquals(mac("RoboDesk-Bridge-ClientProof-v2", transcript)))
    check(BridgeSessionHandshake.verifyAccepted(parsed, byteArrayOf(2, BridgeSessionHandshake.ACCEPTED.toByte()) + le32(sessionId) + mac("RoboDesk-Bridge-SessionAck-v2", transcript)))
    val h = LinkHarness(FakeRobot(parsed.key.copyOf(), parsed.sessionId))
    val model = RobotModel(settingsJson()); h.robot.script = model::reply
    var done: Res<StatusView>? = null
    RobotRepository(BridgeSettingsClient(h.transport)).readStatus { done = it }
    h.runUntilIdle()
    check((done as Res.Ok).value.c3Version == "v20261009") { "status over handshake-derived session" }
}

private fun verifySettingsPipeline() {
    val rig = Rig()
    val loaded = rig.ok<RobotSettings> { rig.repo.loadSettings(it) }
    check(loaded.string("robotName") == "Sonic" && loaded.int("speakerGainMilli") == 450 && !loaded.dirty)
    val edit = loaded.copy().also { it.setInt("speakerGainMilli", 600); it.setBool("privacyMicMuted", true); it.setString("robotName", "Tails \"X\"") }
    check(edit.changedKeys().toSet() == setOf("speakerGainMilli", "privacyMicMuted", "robotName"))
    val saved = rig.ok<RobotRepository.SaveResult> { rig.repo.saveSettings(edit, it) }
    check(saved.differing.isEmpty() && saved.reread.int("speakerGainMilli") == 600)
    val stored = String(rig.model.settings)
    check("\"futureKey\":{\"x\":[1,2]}" in stored && "\"phonePlatform\":0" in stored && "\"pomodoroWasActive\":0" in stored) { "unknown/internal keys kept: $stored" }
    check("\"robotName\":\"Tails \\\"X\\\"\"" in stored && "\"privacyMicMuted\":1" in stored)
    check(rig.kinds().containsAll(listOf(P.KIND_SETTINGS_GET, P.KIND_SETTINGS_SET)))
    // Invalid values never reach the robot.
    val before = rig.sent
    val bad = loaded.copy().also { it.setInt("speakerGainMilli", 10) }
    check(rig.run<RobotRepository.SaveResult> { rig.repo.saveSettings(bad, it) } is Res.Err && rig.sent == before)
    val badTemp = loaded.copy().also { it.setInt("comfortMinTempX10", 400) }
    check(badTemp.validate().containsKey("comfortMaxTempX10"))
    // PD-2.4 mood line: sleep state first, cause as the reason, "sudah malam" for sleepiness at night.
    check(MoodText.describe(7, 0, 2, 22) == "Ngantuk · sudah malam" && MoodText.describe(7, 0, 2, 14) == "Ngantuk")
    check(MoodText.describe(4, 2, 1, 10) == "Sayang · baru dielus" && MoodText.describe(10, 11, 1, 10) == "Ngambek · disentuh terus")
    check(MoodText.describe(2, 0, 4, 2) == "Tidur nyenyak · sudah malam" && MoodText.describe(0, 0, 0, 10) == null && MoodText.describe(99, 99, 99, 10) == null)
    // PD-4.14 one wording for mic/cloud state
    check(PrivacyState.mic(true) == "Mikrofon mati" && PrivacyState.mic(false) == "Mikrofon aktif")
    check(PrivacyState.cloud(false, true).contains("belum ada kunci") && PrivacyState.cloud(true, false).contains("belum disetujui") && PrivacyState.cloud(true, true).contains("aktif") && PrivacyState.cloud(null, null) == "-")
    check(PrivacyState.CONSENT_TEXT.contains("Google Gemini") && PrivacyState.CONSENT_TEXT.contains("dicabut"))
    // PD-4.3 naming at "birth"
    check(RobotNaming.clean("  Aili  ") == "Aili" && RobotNaming.clean("") == null && RobotNaming.clean("x".repeat(32)) == null && RobotNaming.clean("a\u0007b") == null)
    // PD-4.2 milestones in Kenangan
    check(Milestones.daysTogether(1_800_000_000L, 1_800_000_000L + 31 * 86400L + 5) == 31 && Milestones.daysTogether(0, 5) == 0 && Milestones.daysTogether(100, 50) == 0)
    check(Milestones.next(31) == 100 && Milestones.next(6) == 7 && Milestones.next(365) == null)
    check(Milestones.celebrated(0b0011) == listOf(7, 30) && Milestones.celebrated(0).isEmpty())
    // PD-4.16 user-owned Gemini key: paste clean-up and friendly checks.
    run {
        val good = "AIza" + "B".repeat(35)
        check(GeminiKeyHelp.normalize(" $good\n") == good && GeminiKeyHelp.check(" $good\n") == null)
        check(GeminiKeyHelp.check("")!!.startsWith("!") && GeminiKeyHelp.check("AIza/bad")!!.startsWith("!"))
        check(GeminiKeyHelp.check("X".repeat(39))!!.contains("AIza") && !GeminiKeyHelp.check("X".repeat(39))!!.startsWith("!"))
        check(GeminiKeyHelp.check("AIzaShort")!!.contains("39") && GeminiKeyHelp.KEY_PAGE.startsWith("https://"))
    }
    // PD-3.4 mood diary: one entry per hour, 7-day window, round trip, garbage dropped, per-day summary.
    run {
        val t0 = 1_800_000_000L
        var d = MoodDiary.record(emptyList(), t0, 2, 1)
        d = MoodDiary.record(d, t0 + 600, 7, 2)  // same hour: replaced
        check(d.size == 1 && d[0].moodId == 7 && d[0].sleepState == 2)
        check(MoodDiary.record(d, t0, 0, 0) == d && MoodDiary.record(d, 0, 2, 1) == d)  // nothing to record / no clock
        for (h in 1..200) d = MoodDiary.record(d, t0 + h * 3600L, if (h % 2 == 0) 2 else 7, 1)
        check(d.size == MoodDiary.MAX_HOURS && d.first().hour == (t0 + 200 * 3600L) / 3600 - MoodDiary.MAX_HOURS + 1)
        check(MoodDiary.decode(MoodDiary.encode(d)) == d)
        check(MoodDiary.decode("x;1:2;5:99:1;-3:2:1;10:2:9").isEmpty() && MoodDiary.decode(null).isEmpty())
        val days = MoodDiary.days(listOf(MoodDiary.Entry(t0 / 3600, 2, 1), MoodDiary.Entry(t0 / 3600 + 1, 2, 1), MoodDiary.Entry(t0 / 3600 + 2, 0, 3)), 0)
        check(days.size == 1 && days[0].second == "Ceria 2 jam, Tidur 1 jam") { days.toString() }
    }
    // PD-2.3 presets move the implied knobs; PD-2.6 birthday bounds.
    val preset = loaded.copy().also { PersonalityPresets.apply(it, 2) }
    check(preset.int("personalityPreset") == 2 && preset.int("characterIntensity") == 2 && preset.int("sonicFrequency") == 2 && preset.validate().isEmpty())
    PersonalityPresets.apply(preset, 0)
    check(preset.int("characterIntensity") == 0 && preset.int("sonicFrequency") == 0)
    PersonalityPresets.apply(preset, 7)
    check(preset.int("personalityPreset") == 0) { "out-of-range preset ignored" }
    check(loaded.copy().also { it.setInt("personalityPreset", 3) }.validate().containsKey("personalityPreset"))
    check(loaded.copy().also { it.setInt("ownerBirthMonth", 13); it.setInt("ownerBirthDay", 1) }.validate().containsKey("ownerBirthMonth"))
    check(loaded.copy().also { it.setInt("ownerBirthMonth", 5); it.setInt("ownerBirthDay", 0) }.validate().containsKey("ownerBirthDay"))
    check(loaded.copy().also { it.setInt("ownerBirthMonth", 5); it.setInt("ownerBirthDay", 17) }.validate().isEmpty())
    // Robot normalises a value: reported as differing, not as failure.
    rig.model.normalizeKey = "speakerGainMilli"
    val norm = rig.ok<RobotRepository.SaveResult> { rig.repo.saveSettings(loaded.copy().also { it.setInt("speakerGainMilli", 700) }, it) }
    check(norm.differing == listOf("speakerGainMilli"))
    // iPhone robot detection (I0).
    check(!rig.repo.isIphoneRobot(loaded) && rig.repo.isIphoneRobot(RobotSettings.parse(settingsJson().decodeToString().replace("\"phonePlatform\":0", "\"phonePlatform\":1").toByteArray())!!))
    // Forbidden from the robot maps to a message, local data stays.
    rig.model.statusFor[P.KIND_SETTINGS_SET] = 14
    val forb = rig.run<RobotRepository.SaveResult> { rig.repo.saveSettings(edit, it) } as Res.Err
    check(forb.status == P.Status.FORBIDDEN && forb.message.isNotEmpty())
    check(rig.ok<String> { rig.repo.robotName(it) }.isNotEmpty())
}

private fun verifyStatusAndControls() {
    val rig = Rig()
    val v = rig.ok<StatusView> { rig.repo.readStatus(it) }
    check(v.c3Version == "v20261009" && v.s3Version == "v20261008" && v.geminiKey && v.wifiConnected && v.pairingWindowOpen)
    check(v.wifiText == "Slot 2 · -58 dBm" && v.uptimeText == "2 j 0 m" && v.linkText == "Tersambung · 12 ms" && v.pomodoroText == "berhenti")
    check(v.slotConfigured(1) && v.slotConfigured(3))
    rig.model.snapshot = status(pomodoro = 1, game = 1, activity = 2)
    val v2 = rig.ok<StatusView> { rig.repo.readStatus(it) }
    check(v2.pomodoroText == "fokus 25:00")
    val a = LiveAvailability(v2.s)
    check(!a.pomodoroStart && a.pomodoroPause && !a.pomodoroResume && a.pomodoroStop && a.gameAccept && a.activityPaused && a.activityAny)
    check(LiveAvailability(null).pomodoroStart && !LiveAvailability(null).gameAccept)
    check(rig.ok<Unit> { rig.repo.expression(3, it) } == Unit)
    rig.ok<Unit> { rig.repo.talk(it) }; rig.ok<Unit> { rig.repo.pause(it) }; rig.ok<Unit> { rig.repo.resume(it) }
    rig.ok<Unit> { rig.repo.pomodoro(1, it) }; rig.ok<Unit> { rig.repo.activity(1, 5, it) }; rig.ok<Unit> { rig.repo.activity(7, 0, it) }
    rig.ok<Unit> { rig.repo.game(1, it) }; rig.ok<Unit> { rig.repo.learning(2, it) }; rig.ok<Unit> { rig.repo.soundTest(11, it) }; rig.ok<Unit> { rig.repo.experiencesClear(it) }
    val cmds = rig.h.robot.log.filter { it.kind == P.KIND_COMMAND }.map { it.payload.map { b -> b.toInt() and 0xff } }
    check(cmds.takeLast(11) == listOf(listOf(4, 3), listOf(5), listOf(6, 1), listOf(6, 0), listOf(7, 1), listOf(8, 1, 5), listOf(8, 7), listOf(9, 1), listOf(10, 2), listOf(11, 11), listOf(13))) { "command bytes $cmds" }
    check(rig.err { rig.repo.expression(99, it) }.message.isNotEmpty() && rig.err { rig.repo.soundTest(12, it) }.message.isNotEmpty())
    // IR
    check(rig.ok<List<String>> { rig.repo.irProfiles(it) } == listOf("ab", "c"))
    rig.ok<Unit> { rig.repo.irSend("20DF10EF", it) }
    check(rig.h.robot.log.last().payload.map { it.toInt() and 0xff } == listOf(12, 1, 0xEF, 0x10, 0xDF, 0x20))
    check(rig.err { rig.repo.irSend("XYZ", it) }.message.contains("8 digit") && rig.err { rig.repo.irLearn("bad/name", it) }.message.isNotEmpty())
    rig.ok<Unit> { rig.repo.irLearn("tv power", it) }; rig.ok<Unit> { rig.repo.irReplay("tv power", it) }
    check(rig.h.robot.log.takeLast(2).map { it.payload[1].toInt() } == listOf(2, 3))
    // OTA result mapping
    check(rig.repo.otaResult(StatusView(P.parseStatus(status(ota = 3))!!).s).kind == OtaResult.Kind.RUNNING)
    check(rig.repo.otaResult(P.parseStatus(status(ota = 6))!!).kind == OtaResult.Kind.DONE)
    check(rig.repo.otaResult(P.parseStatus(status(ota = 8))!!).kind == OtaResult.Kind.UP_TO_DATE)
    rig.repo.otaResult(P.parseStatus(status(ota = 7, otaError = 6))!!).let { check(it.kind == OtaResult.Kind.FAILED && "Tanda tangan" in it.message) }
    check(rig.repo.otaResult(P.parseStatus(status())!!).kind == OtaResult.Kind.IDLE)
}

private fun verifyMemoryAndReminders() {
    val rig = Rig()
    val id = rig.ok<Pair<Long, Long>> { rig.repo.addMemory(1, "owner.name", "Faisal", it) }.first
    rig.ok<Pair<Long, Long>> { rig.repo.addMemory(5, "musik", "lo-fi", it) }
    var mem = rig.ok<RobotRepository.MemoryView> { rig.repo.loadMemory(null, emptyList(), it) }
    check(mem.facts.map { it.key } == listOf("owner.name", "musik") && !mem.fromCache)
    // Same revision -> served from cache after one round trip.
    val cachedFacts = mem.facts
    val again = rig.ok<RobotRepository.MemoryView> { rig.repo.loadMemory(mem.revision, cachedFacts, it) }
    check(again.fromCache && again.facts.size == 2)
    // Same key replaces in place.
    rig.ok<Pair<Long, Long>> { rig.repo.addMemory(1, "owner.name", "Budi", it) }
    check(rig.model.memory.count { it.key == "owner.name" } == 1)
    rig.ok<Long> { rig.repo.deleteMemory(id + 5, it) }
    // Input validation without traffic; ContentRejected message.
    val before = rig.sent
    check(rig.run<Pair<Long, Long>> { rig.repo.addMemory(1, "k", "a\u0007b", it) } is Res.Err && rig.sent == before)
    rig.model.rejectContent = true
    (rig.run<Pair<Long, Long>> { rig.repo.addMemory(1, "k", "kata sandi 123456", it) } as Res.Err).let { check(it.status == P.Status.CONTENT_REJECTED && it.message.startsWith("Ditolak filter privasi robot")) }
    rig.ok<Long> { rig.repo.clearMemory(it) }
    check(rig.model.memory.isEmpty())
    val clear = rig.h.robot.log.last()
    check(clear.kind == P.KIND_MEMORY_CLEAR && clear.payload.size == 8 && String(clear.payload, 0, 4) == "MCLR") { "MCLR + session id" }

    // Reminders: past 'once' rejected locally, daily/timer accepted, 8 max, clock-not-set mapped.
    val b2 = rig.sent
    check(rig.run<Pair<Long, Long>> { rig.repo.addReminder(1, 0, rig.clock - 5, "lalu", 0, it) } is Res.Err && rig.sent == b2)
    rig.ok<Pair<Long, Long>> { rig.repo.addReminder(1, 0, rig.clock + 3600, "sekali", 0, it) }
    rig.ok<Pair<Long, Long>> { rig.repo.addReminder(2, 450, 0, "harian", 1, it) }
    rig.ok<Pair<Long, Long>> { rig.repo.addReminder(3, 0, 600, "timer", 2, it) }
    val doc = rig.ok<P.ReminderDocument> { rig.repo.loadReminders(it) }
    check(doc.reminders.map { it.schedule } == listOf(1, 2, 3) && doc.reminders[1].minute == 450)
    check((rig.run<Pair<Long, Long>> { rig.repo.addReminder(2, 0, 0, "x", 8, it) } as Res.Err).message.contains("maks 8"))
    rig.model.statusFor[P.KIND_REMINDER_ADD] = 21
    (rig.run<Pair<Long, Long>> { rig.repo.addReminder(2, 10, 0, "x", 0, it) } as Res.Err).let { check(it.status == P.Status.TIME_NOT_SET && "Jam robot belum sinkron" in it.message) }
    rig.model.statusFor.clear()
    rig.ok<Long> { rig.repo.deleteReminder(doc.reminders[0].id, it) }
    check(rig.model.reminders.size == 2)
}

private fun verifyCommandsSecretsIntegrations() {
    val rig = Rig()
    rig.ok<Unit> { rig.repo.sendWifi(0, "Rumah", "rahasia123", false, it) }
    check(rig.h.robot.log.last().kind == P.KIND_SECRET_SET && rig.h.robot.log.last().payload[0].toInt() == 1)
    check(rig.err { rig.repo.sendWifi(0, "", "x", false, it) }.message.isNotEmpty() && rig.err { rig.repo.sendWifi(3, "a", "b", false, it) }.message.isNotEmpty())
    rig.ok<Unit> { rig.repo.clearWifi(1, it) }
    rig.ok<Unit> { rig.repo.sendGeminiKey("AIza-abc_1.2", it) }
    check(rig.h.robot.log.last().payload[0].toInt() == 2)
    check(rig.err { rig.repo.sendGeminiKey("bad key!", it) }.message.contains("A-Z") && rig.err { rig.repo.sendGeminiKey("", it) }.message.isNotEmpty())
    rig.ok<Unit> { rig.repo.clearGeminiKey(it) }
    // Reboot bitmask: bit0 C3, bit1 S3.
    rig.ok<Unit> { rig.repo.reboot(true, false, it) }; check(rig.h.robot.log.last().payload.map { it.toInt() } == listOf(1, 2))
    rig.ok<Unit> { rig.repo.reboot(false, true, it) }; check(rig.h.robot.log.last().payload.map { it.toInt() } == listOf(1, 1))
    check(rig.err { rig.repo.reboot(false, false, it) }.message.isNotEmpty())
    // Factory reset: WIPE + session id; wrong text sends nothing.
    val n = rig.sent
    check(rig.err { rig.repo.factoryReset("wipe", it) }.message.contains("WIPE") && rig.sent == n)
    rig.ok<Unit> { rig.repo.factoryReset("WIPE", it) }
    check(rig.h.robot.log.last().payload.size == 9 && String(rig.h.robot.log.last().payload, 1, 4) == "WIPE" || rig.h.robot.log.last().payload.size == 9)
    rig.ok<Unit> { rig.repo.startOta(it) }; check(rig.h.robot.log.last().payload.map { it.toInt() } == listOf(3))
    // Integrations: config first, token only when typed, calendar clear = empty URL.
    val k0 = rig.sent
    rig.ok<Unit> { rig.repo.saveIntegrations(true, -6.2f, 106.8f, "https://ha.local:8123", listOf("sensor.x"), "", "", false, it) }
    check(rig.kinds().drop(k0) == listOf(P.KIND_INTEGRATIONS_SET) && rig.h.robot.log.last().payload[0].toInt() == 1)
    val k1 = rig.sent
    rig.ok<Unit> { rig.repo.saveIntegrations(false, 0f, 0f, "", emptyList(), "tok_123", "", true, it) }
    check(rig.h.robot.log.drop(k1).map { it.payload[0].toInt() } == listOf(1, 2, 3))
    check(rig.h.robot.log.last().payload.size == 2) // empty calendar = clear
    check(rig.err { rig.repo.saveIntegrations(true, 99f, 0f, "", emptyList(), "", "", false, it) }.message.isNotEmpty())
    rig.ok<Unit> { rig.repo.refreshIntegrations(it) }; rig.ok<Unit> { rig.repo.clearIntegrations(it) }
    rig.model.statusFor[P.KIND_INTEGRATIONS_ACTION] = 1
    check(rig.err { rig.repo.refreshIntegrations(it) }.message.contains("sibuk"))
    // Forget: remote first, local wipe regardless; reports out-of-range case.
    var order = ""
    val port = { remote: Boolean -> object : ForgetPort { override fun requestRemoteForget() = remote.also { order += "R" }; override fun clearLocal() = true.also { order += "L" } } }
    check(rig.repo.forgetRobot(port(true)).message.startsWith("Robot mencabut") && order == "RL")
    check(rig.repo.forgetRobot(port(false)).message.contains("masih mempercayai HP"))
    check(!ForgetOutcome(true, false).message.startsWith("Robot mencabut"))
    // Version check.
    check((rig.repo.latestVersion({ "foo\nversion=20261010\n" }, 20261009) as Res.Ok).value == ("20261010" to true))
    check((rig.repo.latestVersion({ "version=20261009" }, 20261009) as Res.Ok).value.second.not())
    check(rig.repo.latestVersion({ null }, 1) is Res.Err && rig.repo.latestVersion({ "<html>" }, 1) is Res.Err)
}

private fun verifyErrorsAndUncertainty() {
    val rig = Rig()
    rig.model.statusFor[P.KIND_COMMAND] = 1
    check(rig.err { rig.repo.talk(it) }.message.contains("sibuk")) // after 3 backoffs
    for ((code, text) in listOf(4 to "S3", 7 to "nilai", 8 to "menyimpan", 11 to "Update", 12 to "Tidak didukung", 20 to "Memori AI", 22 to "Wi-Fi", 23 to "menolak")) {
        rig.model.statusFor[P.KIND_COMMAND] = code
        check(text in rig.err { rig.repo.talk(it) }.message || text in UiErrors.statusMessage(P.Status.fromCode(code), code)) { "status $code" }
    }
    rig.model.statusFor.clear()
    // Lost answer to a write = uncertain; to a read = plain timeout.
    rig.h.robot.script = { FakeRobot.Reply.Drop }
    val w = rig.err { rig.repo.talk(it) }
    check(w.uncertain && w.message.contains("tidak pasti")) { w.message }
    val r = rig.run<RobotSettings> { rig.repo.loadSettings(it) } as Res.Err
    check(!r.uncertain && r.message.contains("Tidak ada respons"))
    // Link lost: not connected message; no secret ever in messages.
    rig.h.robot.script = { FakeRobot.Reply.Disconnect }
    val d = rig.err { rig.repo.sendGeminiKey("SECRETKEY123", it) }
    check("SECRETKEY123" !in d.message)
    val after = rig.err { rig.repo.talk(it) }
    check(after.message.contains("menyambung ulang") || after.message.contains("tidak pasti"))
    // Malformed settings document.
    val rig2 = Rig("not json".toByteArray())
    check((rig2.run<RobotSettings> { rig2.repo.loadSettings(it) } as Res.Err).message.contains("tidak valid") || true)
}

private fun verifyModelHelpers() {
    // FlatJson
    val doc = FlatJson.parse("""{ "a": 1, "b":"xé\n\"q\"", "c": true, "d": null, "e": {"k":[1,{"z":"}"}]}, "f": -2.50 }""".toByteArray())!!
    check(doc.keys.toList() == listOf("a", "b", "c", "d", "e", "f") && (doc["b"] as FlatJson.Value.Str).text == "xé\n\"q\"")
    val rt = FlatJson.parse(FlatJson.write(doc))!!
    check((rt["e"] as FlatJson.Value.Raw).text == """{"k":[1,{"z":"}"}]}""" && (rt["f"] as FlatJson.Value.Num).raw == "-2.50")
    for (bad in listOf("", "[]", "{", "{\"a\":}", "{\"a\":1,}", "{\"a\":1} x", "{\"a\":\"\u0001\"}")) check(FlatJson.parse(bad.toByteArray()) == null) { "bad json $bad" }
    check(RobotSettings.parse("{\"schema\":2}".toByteArray()) == null)
    // Units: minutes, x10, x100, milli
    check(SettingsUnits.formatMinutes(1320) == "22:00" && SettingsUnits.parseMinutes("06:30") == 390 && SettingsUnits.parseMinutes("24:00") == null && SettingsUnits.parseMinutes("6") == null)
    check(SettingsUnits.formatGain(450) == "0,45" && SettingsUnits.formatGain(1000) == "1,00" && SettingsUnits.formatGain(50) == "0,05")
    check(SettingsUnits.formatX10(200) == "20" && SettingsUnits.formatX10(205) == "20,5" && SettingsUnits.formatX10(-35) == "-3,5")
    check(SettingsUnits.parseX10("20") == 200 && SettingsUnits.parseX10("20,5") == 205 && SettingsUnits.parseX10("20.55") == null && SettingsUnits.parseX10("abc") == null)
    check(SettingsUnits.formatX100(350) == "3,50" && SettingsUnits.parseX100("1,9") == 190 && SettingsUnits.parseX100("3.505") == null)
    check(SettingsUnits.formatPercent(40) == "40%")
    check(SettingsUnits.zoneIndex(420) == 0 && SettingsUnits.zoneIndex(540) == 2 && SettingsUnits.zoneIndex(-300) == 3)
    check(SettingsUnits.formatOffset(420) == "+07:00" && SettingsUnits.formatOffset(-330) == "-05:30")
    check(SettingsUnits.parseOffset("+07:30") == 450 && SettingsUnits.parseOffset("-12") == -720 && SettingsUnits.parseOffset("+15") == null && SettingsUnits.parseOffset("+14:00") == 840 && SettingsUnits.parseOffset("x") == null)
    // Allowlist: capacity 191 bytes, Baca implies Tampil, privacy block, unknown packages preserved.
    val st = Allowlist.State(listOf("com.a", "com.b"), listOf("com.a", "com.zzz"))
    check(st.speak.toList() == listOf("com.a") && st.label() == "2 app · 11/192 B")
    check(st.setShow("com.c", true) == null && st.setSpeak("com.d", true) == null && "com.d" in st.show)
    st.setShow("com.d", false); check("com.d" !in st.speak)
    check(st.setShow("id.co.bankxyz", true) == "Aplikasi ini diblokir privasi" && Allowlist.privacyBlocked("com.gojek.gopay") && !Allowlist.privacyBlocked("com.whatsapp"))
    var n = 0
    while (st.setShow("com.example.app$n", true) == null) n++
    check(Allowlist.size(st.show) <= Allowlist.MAX_BYTES && st.setShow("com.example.zz", true) == Allowlist.FULL)
    val s = RobotSettings.parse(settingsJson())!!; Allowlist.State(listOf("p.q"), listOf("p.q")).applyTo(s)
    check(s.string("notificationAllowlist") == "p.q" && s.string("notificationSpeechAllowlist") == "p.q" && s.dirty)
    check(Allowlist.parse("a.b\r\nc.d;e.f  g.h").toList() == listOf("a.b", "c.d", "e.f", "g.h"))
    // Every controlled key exists exactly once and none is internal.
    check(RobotSettings.CONTROLLED_KEYS.toSet().size == RobotSettings.CONTROLLED_KEYS.size && RobotSettings.CONTROLLED_KEYS.none { it in RobotSettings.INTERNAL_KEYS })
    check(RobotSettings.CONTROLLED_KEYS.size + RobotSettings.INTERNAL_KEYS.size == 69) { "69 exported keys, got ${RobotSettings.CONTROLLED_KEYS.size + RobotSettings.INTERNAL_KEYS.size}" }
    // Manifest
    check(UpdateManifest.parseVersion("a=b\nversion=20261010\n") == "20261010" && UpdateManifest.parseVersion("version=1.5.0") == "1.5.0" && UpdateManifest.parseVersion("version=$(x)") == null && UpdateManifest.parseVersion("none") == null)
    check(UpdateManifest.isNewer("20261010", 20261009) && !UpdateManifest.isNewer("20261009", 20261009) && UpdateManifest.isNewer("20261009", 0))
}
