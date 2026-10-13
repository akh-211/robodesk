package com.robodesk.phonebridge

import java.io.ByteArrayOutputStream
import java.io.File
import java.io.PrintStream
import com.robodesk.phonebridge.BridgeSettingsProtocol as P

private const val SESSION = 0x78563412L
private val VECTOR_KEY = ByteArray(32) { it.toByte() }

private fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it.toInt() and 0xff) }
private fun unhex(text: String) = ByteArray(text.length / 2) { text.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

private class Vector(val name: String, val direction: Int, val kind: Int, val counter: Long, val payload: String, val envelope: String)

private fun findFixture(): File {
    System.getProperty("robodesk.root")?.let { File(it, "tests/fixtures/ble_settings_v1.txt").takeIf { f -> f.isFile }?.let { f -> return f } }
    var dir: File? = File(System.getProperty("user.dir")).absoluteFile
    while (dir != null) {
        val candidate = File(dir, "tests/fixtures/ble_settings_v1.txt")
        if (candidate.isFile) return candidate
        dir = dir.parentFile
    }
    error("tests/fixtures/ble_settings_v1.txt not found; run from the repository or pass -Drobodesk.root=<repo>")
}

private fun loadVectors(): List<Vector> {
    val out = ArrayList<Vector>()
    var name: String? = null
    var fields = HashMap<String, String>()
    fun flush() {
        val n = name ?: return
        out += Vector(n, fields.getValue("direction").toInt(), fields.getValue("kind").toInt(), fields.getValue("counter").toLong(),
            fields.getValue("payload"), fields.getValue("envelope"))
        check(fields.getValue("session") == "78563412" && fields.getValue("age") == "0") { "unexpected header in $n" }
    }
    for (raw in findFixture().readLines()) {
        val line = raw.trim()
        if (line.isEmpty() || line.startsWith("#")) continue
        if (line.startsWith("[")) { flush(); name = line.substring(1, line.length - 1); fields = HashMap() }
        else line.split("=", limit = 2).let { fields[it[0]] = it[1] }
    }
    flush()
    return out
}

private fun requestFor(name: String): P.Request? = when (name) {
    "settings_get_page0" -> P.settingsGet(0)
    "settings_set_page0" -> P.settingsSetPages(settingsDoc())!![0]
    "settings_set_page1" -> P.settingsSetPages(settingsDoc())!![1]
    "secret_wifi_slot1" -> P.secretWifi(1, "HomeNet", "correct horse", false)
    "secret_wifi_keep_password_slot0" -> P.secretWifi(0, "Office", "", true)
    "secret_wifi_clear_slot2" -> P.secretWifi(2, "", "", false)
    "secret_gemini_key" -> P.secretGemini("AIzaSyTEST-key_123")
    "status_request" -> P.statusSnapshot()
    "command_reboot_both" -> P.reboot(3)
    "command_factory_reset" -> P.factoryReset()
    "command_ota_start" -> P.otaStart()
    "memory_list_page0" -> P.memoryList(0, 0)
    "memory_add_drink" -> P.memoryAdd(1, "drink", "kopi")
    "memory_add_name_utf8" -> P.memoryAdd(0, "nama", "Éric")
    "memory_delete" -> P.memoryDelete(2)
    "memory_clear" -> P.memoryClear()
    "reminder_list_page0" -> P.reminderList(0)
    "reminder_add_timer" -> P.reminderAdd(3, 0, 600, "Minum air")
    "reminder_add_daily" -> P.reminderAdd(2, 480, 0, "Sarapan")
    "reminder_add_once" -> P.reminderAdd(1, 0, 1800003600L, "Rapat")
    "reminder_delete" -> P.reminderDelete(2)
    "command_expression_love" -> P.expression(3)
    "command_talk" -> P.talk()
    "command_pause_15m" -> P.pauseInitiative(2)
    "command_resume" -> P.pauseInitiative(0)
    "command_pomodoro_start" -> P.pomodoro(1)
    "command_activity_start_5" -> P.activity(1, 5)
    "command_activity_skip" -> P.activity(5)
    "command_game_accept" -> P.game(1)
    "command_learning_on" -> P.learning(1)
    "command_sound_test_shake" -> P.soundTest(11)
    "command_ir_send" -> P.irSend(0x20DF10EFL)
    "command_ir_learn_tv" -> P.irLearn("TV power")
    "command_ir_replay_tv" -> P.irReplay("TV power")
    "command_experiences_clear" -> P.experiencesClear()
    "ir_profile_list" -> P.irProfileList()
    "integrations_get" -> P.integrationsGet()
    "integrations_set_config" -> P.integrationsSetConfig(true, -6.2f, 106.8f, "https://ha.example", listOf("sensor.temp", "light.lamp"))
    "integrations_set_token" -> P.integrationsSetToken("TESTTOKEN-abc_123")
    "integrations_set_calendar" -> P.integrationsSetCalendar("https://cal.example/feed.ics?k=1")
    "integrations_set_token_clear" -> P.integrationsSetToken("")
    "integrations_refresh" -> P.integrationsRefresh()
    "integrations_clear" -> P.integrationsClear()
    "integrations_status" -> P.integrationsStatus()
    "command_rest_sleep" -> P.rest(true)
    "command_rest_wake" -> P.rest(false)
    "command_pet" -> P.remotePet()
    else -> error("no request builder for $name")
}

private fun settingsDoc() = ("{\"schema\":1,\"robotName\":\"" + "R".repeat(620) + "\"}").toByteArray()

/** name -> (request id, request kind, status) for every robot vector. */
private val ROBOT_EXPECT = mapOf(
    "response_get_page0" to Triple(1L, 8, 0), "response_set_page0_staged" to Triple(2L, 9, 0),
    "response_set_page1_applied" to Triple(3L, 9, 0), "response_set_crc_mismatch" to Triple(3L, 9, 6),
    "response_secret_ok" to Triple(4L, 10, 0), "response_status" to Triple(8L, 11, 0),
    "response_command_busy" to Triple(9L, 12, 1), "response_ota_accepted" to Triple(11L, 12, 0),
    "response_ota_update_busy" to Triple(11L, 12, 11), "response_status_schema1" to Triple(8L, 11, 0),
    "response_memory_list_page0" to Triple(12L, 13, 0), "response_memory_add_drink" to Triple(13L, 14, 0),
    "response_memory_add_name_utf8" to Triple(14L, 14, 0), "response_memory_delete_ok" to Triple(15L, 15, 0),
    "response_memory_delete_not_found" to Triple(15L, 15, 19), "response_memory_clear_ok" to Triple(16L, 16, 0),
    "response_memory_clear_confirm_required" to Triple(16L, 16, 10), "response_memory_add_content_rejected" to Triple(13L, 14, 17),
    "response_memory_add_full" to Triple(13L, 14, 18), "response_reminder_list_page0" to Triple(17L, 17, 0),
    "response_reminder_add_timer" to Triple(18L, 18, 0), "response_reminder_add_daily" to Triple(19L, 18, 0),
    "response_reminder_add_once" to Triple(20L, 18, 0), "response_reminder_delete_ok" to Triple(21L, 19, 0),
    "response_reminder_add_time_not_set" to Triple(20L, 18, 21), "response_reminder_add_full" to Triple(20L, 18, 18),
    "response_status_schema2" to Triple(8L, 11, 0), "response_status_schema3" to Triple(8L, 11, 0), "response_command_rejected" to Triple(27L, 12, 23),
    "response_command_robot_unavailable" to Triple(27L, 12, 4), "response_command_bad_request" to Triple(27L, 12, 2),
    "response_ir_profile_list" to Triple(36L, 20, 0), "response_ir_profile_list_robot_unavailable" to Triple(36L, 20, 4),
    "response_integrations_get" to Triple(37L, 21, 0), "response_integrations_set_ok" to Triple(38L, 22, 0),
    "response_integrations_busy" to Triple(43L, 23, 1), "response_integrations_status" to Triple(44L, 24, 0)
)

private fun pageBody(document: ByteArray, index: Int) = P.settingsSetPages(document)!![index].payloadFor(0)!!

/** Fake robot: answers each request through [handler]; records payloads. */
private class FakeTransport(val handler: (P.Request, ByteArray) -> Pair<Int, ByteArray>?) : SettingsTransport {
    val seen = ArrayList<ByteArray>()
    override fun send(request: P.Request, callback: SettingsDone<P.Response>) {
        val payload = request.payloadFor(SESSION)!!
        seen += payload
        val reply = handler(request, payload)
        callback(if (reply == null) SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.TIMEOUT))
        else SettingsOutcome.Ok(P.Response(1, request.kind, reply.first, reply.second)))
    }
}

private fun <T> await(block: (SettingsDone<T>) -> Unit): SettingsOutcome<T> {
    var result: SettingsOutcome<T>? = null
    block { check(result == null) { "callback invoked twice" }; result = it }
    return checkNotNull(result) { "callback not invoked" }
}

private fun <T> SettingsOutcome<T>.value(): T = (this as SettingsOutcome.Ok<T>).value
private fun SettingsOutcome<*>.reason() = (this as SettingsOutcome.Fail).failure.reason

fun verifyBridgeSettingsProtocol() {
    verifyVectors()
    verifyPagingAndBounds()
    verifyStatusCodes()
    verifySecretsNeverLogged()
    verifyQueue()
    verifyClient()
    println("PASS: BLE settings protocol vectors, paging/CRC, bounds, status codes, secrets, request queue, client")
}

private fun verifyVectors() {
    val vectors = loadVectors()
    check(vectors.isNotEmpty())
    val phoneSender = BridgeAuthenticatedEnvelope.SessionSender(VECTOR_KEY, SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT)
    val robotSender = BridgeAuthenticatedEnvelope.SessionSender(VECTOR_KEY, SESSION, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE)
    val phoneReceiver = BridgeAuthenticatedEnvelope.SessionReceiver(VECTOR_KEY, SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT,
        (P.KIND_SETTINGS_GET..P.KIND_INTEGRATIONS_STATUS).associateWith { 0L })
    val robotReceiver = BridgeAuthenticatedEnvelope.SessionReceiver(VECTOR_KEY, SESSION, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE,
        mapOf(P.KIND_RESPONSE to 0L))
    var phoneCount = 0
    var robotCount = 0
    for (v in vectors) {
        val payload = unhex(v.payload)
        val wire = unhex(v.envelope)
        if (v.direction == 1) {
            phoneCount++
            check(v.counter == phoneCount.toLong()) { "${v.name}: counters must be contiguous" }
            // Encoder: codec payload and envelope are byte-exact.
            val request = requestFor(v.name)!!
            check(request.kind == v.kind) { "${v.name}: kind" }
            check(hex(request.payloadFor(SESSION)!!) == v.payload) { "${v.name}: payload ${hex(request.payloadFor(SESSION)!!)}" }
            check(hex(phoneSender.create(v.kind, 0, payload)!!) == v.envelope) { "${v.name}: envelope" }
            val message = phoneReceiver.accept(wire) ?: error("${v.name}: envelope rejected")
            check(message.kind == v.kind && message.counter == v.counter && message.payload.contentEquals(payload)) { "${v.name}: decode" }
        } else {
            robotCount++
            check(v.counter == robotCount.toLong() && v.kind == P.KIND_RESPONSE) { "${v.name}: counters/kind" }
            check(hex(robotSender.create(v.kind, 0, payload)!!) == v.envelope) { "${v.name}: envelope" }
            val message = robotReceiver.accept(wire) ?: error("${v.name}: envelope rejected")
            val response = P.parseResponse(message.payload) ?: error("${v.name}: response malformed")
            val (id, kind, status) = ROBOT_EXPECT.getValue(v.name)
            check(response.requestId == id && response.kind == kind && response.statusCode == status) { "${v.name}: header" }
            check(P.Status.fromCode(status) != null) { "${v.name}: status" }
            // A replay of the same envelope must be refused.
            check(robotReceiver.accept(wire) == null) { "${v.name}: replay" }
        }
    }
    check(phoneCount == 47 && robotCount == ROBOT_EXPECT.size) { "vector count: $phoneCount/$robotCount" }
    // A robot envelope with ageMs != 0 is not accepted for kind 0x7d.
    val aged = BridgeAuthenticatedEnvelope.SessionSender(VECTOR_KEY, SESSION, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE).create(P.KIND_RESPONSE, 1, ByteArray(6))!!
    check(BridgeAuthenticatedEnvelope.SessionReceiver(VECTOR_KEY, SESSION, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE, mapOf(P.KIND_RESPONSE to 0L)).accept(aged) == null)

    fun body(name: String): ByteArray = unhex(vectors.first { it.name == name }.payload).copyOfRange(6, unhex(vectors.first { it.name == name }.payload).size)
    // Documents carried by the vectors.
    val getPage0 = P.parsePage(body("response_get_page0"))!!
    check(getPage0.index == 0 && getPage0.count == 2 && getPage0.total == 647 && getPage0.crc == 0x9a014325L)
    val assembler = P.PageAssembler()
    check(assembler.accept(getPage0) == P.PageAssembler.Step.NEXT)
    check(assembler.accept(P.parsePage(unhex(vectors.first { it.name == "settings_set_page1" }.payload))!!) == P.PageAssembler.Step.DONE)
    check(assembler.document!!.contentEquals(settingsDoc()))

    val snapshot = P.parseStatus(P.PageAssembler().also {
        check(it.accept(P.parsePage(body("response_status"))!!) == P.PageAssembler.Step.DONE)
    }.document!!)!!
    check(snapshot.c3Version == 20261009L && snapshot.s3Version == 20261008L && snapshot.c3UptimeMs == 123456L && snapshot.s3UptimeMs == 120000L)
    check(snapshot.c3HeapFree == 48000L && snapshot.c3HeapMin == 30000L && snapshot.c3LargestBlock == 24000L && snapshot.s3HeapFree == 200000L && snapshot.s3HeapMin == 150000L)
    check(snapshot.linkFlags == 3 && snapshot.uartConnected && snapshot.s3Valid && snapshot.linkAgeMs == 12L && snapshot.linkRetries == 4L && snapshot.linkCrcErrors == 1L && snapshot.linkDisconnects == 2L)
    check(snapshot.wifiState == 1 && snapshot.wifiRssi == -61 && snapshot.bleFlags == 0x0b && snapshot.configFlags == 0x03)
    check(snapshot.schema == 4 && snapshot.otaState == P.OtaState.FLASHING_S3.code && snapshot.otaError == 0 && snapshot.otaPercent == 42 && snapshot.otaTargetVersion == 20261010L)
    check(snapshot.wifiActiveSlot == 2 && snapshot.pomodoroState == 1 && snapshot.pomodoroRemainingSec == 1234 && snapshot.activityId == 5 && snapshot.activityState == 1)
    check(snapshot.gameState == 1 && snapshot.learningEnabled && snapshot.irProfileCount == 2 && snapshot.irLearnState == 1 && !snapshot.integrationBusy)
    check(snapshot.moodId == 7 && snapshot.moodCause == 9 && snapshot.sleepState == 2) // PD-2.4 schema 4
    val schema3 = P.parseStatus(P.PageAssembler().also {
        check(it.accept(P.parsePage(body("response_status_schema3"))!!) == P.PageAssembler.Step.DONE)
    }.document!!)!!
    check(schema3.schema == 3 && schema3.pomodoroRemainingSec == 1234 && schema3.moodId == 0 && schema3.sleepState == 0)
    check(P.parseStatus(byteArrayOf(4) + ByteArray(P.SNAPSHOT_SIZE - 1)) == null) { "schema 4 needs 79 bytes" }
    val schema2 = P.parseStatus(P.PageAssembler().also {
        check(it.accept(P.parsePage(body("response_status_schema2"))!!) == P.PageAssembler.Step.DONE)
    }.document!!)!!
    check(schema2.schema == 2 && schema2.otaPercent == 42 && schema2.wifiActiveSlot == 0 && schema2.pomodoroRemainingSec == 0 && !schema2.learningEnabled && schema2.irProfileCount == 0)
    check(P.OtaState.fromCode(snapshot.otaState)!!.active && !P.OtaState.DONE.active && P.OtaState.fromCode(9) == null)
    val legacy = P.parseStatus(P.PageAssembler().also {
        check(it.accept(P.parsePage(body("response_status_schema1"))!!) == P.PageAssembler.Step.DONE)
    }.document!!)!!
    check(legacy.schema == 1 && legacy.c3Version == 20261009L && legacy.otaState == 0 && legacy.otaTargetVersion == 0L)
    check(P.formatFirmwareVersion(6L) == "v6" && P.formatFirmwareVersion(0L) == "?" && P.formatFirmwareVersion(0xFFFFFFFFL) == "v4294967295")
    check(P.otaErrorLabel(6) == "Tanda tangan tidak valid" && P.otaErrorLabel(200).contains("200"))

    val memory = P.parseMemoryWindow(P.PageAssembler().also { it.accept(P.parsePage(body("response_memory_list_page0"))!!) }.document!!)!!
    check(memory.revision == 0x7739f445L && memory.total == 2 && !memory.more && memory.facts.size == 2)
    check(P.crc32(memory.raw) == memory.revision)
    memory.facts[0].let { check(it.id == 1L && it.key == "drink" && it.value == "kopi" && it.category == 1 && it.pinned && it.source == 1 && it.updatedEpoch == 1800000000L && it.day == 20833) }
    memory.facts[1].let { check(it.id == 2L && it.key == "nama" && it.value == "Éric" && it.category == 0) }
    val cache = P.encodeMemoryDocument(memory.revision, memory.facts)!!
    check(cache.contentEquals(P.PageAssembler().also { it.accept(P.parsePage(body("response_memory_list_page0"))!!) }.document!!))

    val reminders = P.parseReminderDocument(P.PageAssembler().also { it.accept(P.parsePage(body("response_reminder_list_page0"))!!) }.document!!)!!
    check(reminders.reminders.map { it.id } == listOf(1L, 2L, 3L) && P.crc32(reminders.stableRaw) == reminders.revision)
    reminders.reminders[0].let { check(it.schedule == 3 && it.due == 600L && it.text == "Minum air") }
    reminders.reminders[1].let { check(it.schedule == 2 && it.minute == 480 && it.due == 0L && it.text == "Sarapan") }
    reminders.reminders[2].let { check(it.schedule == 1 && it.due == 1800003600L && it.text == "Rapat") }

    check(P.parseSetBody(body("response_set_page0_staged")) == (0 to false))
    check(P.parseSetBody(body("response_set_page1_applied")) == (1 to true))
    check(P.parseIdRevisionBody(body("response_memory_add_drink")) == (1L to 0x8368a044L))
    check(P.parseRevisionBody(body("response_memory_clear_ok")) == 0L)
    check(P.parseRevisionBody(body("response_memory_delete_ok")) != null)
    check(P.parseIdRevisionBody(body("response_reminder_add_once"))!!.first == 3L)
    check(body("response_secret_ok").isEmpty() && body("response_ota_accepted").isEmpty())
    // IR profile list and integrations (kinds 20..24).
    check(P.parseIrProfiles(body("response_ir_profile_list")) == listOf("TV power", "Fan"))
    check(P.parseIrProfiles(byteArrayOf(0)) == emptyList<String>() && P.parseIrProfiles(ByteArray(0)) == null && P.parseIrProfiles(byteArrayOf(5)) == null)
    check(P.parseIrProfiles(byteArrayOf(1, 2, 'a'.code.toByte())) == null && P.parseIrProfiles(byteArrayOf(1, 1, '/'.code.toByte())) == null)
    check(P.parseIrProfiles(byteArrayOf(1, 1, 'a'.code.toByte(), 0)) == null && P.parseIrProfiles(byteArrayOf(1, 16) + ByteArray(16) { 'a'.code.toByte() }) == null)
    val cfg = P.parseIntegrationsConfig(P.PageAssembler().also { check(it.accept(P.parsePage(body("response_integrations_get"))!!) == P.PageAssembler.Step.DONE) }.document!!)!!
    check(cfg.weatherEnabled && cfg.tokenConfigured && cfg.calendarConfigured && cfg.latitude == -6.2f && cfg.longitude == 106.8f)
    check(cfg.haBase == "https://ha.example" && cfg.entities == listOf("sensor.temp", "light.lamp"))
    val st = P.parseIntegrationsStatus(P.PageAssembler().also { check(it.accept(P.parsePage(body("response_integrations_status"))!!) == P.PageAssembler.Step.DONE) }.document!!)!!
    check(!st.busy && st.weatherConfigured && st.weatherValid && st.homeAssistantConfigured && st.calendarConfigured && st.calendarValid)
    check(st.weatherError == 0 && st.homeAssistantError == 4 && st.calendarError == 0 && st.weatherAgeMs == 60000L && st.weather == "21.5 C, clear")
    check(st.entities.size == 2 && st.entities[0].let { it.slot == 0 && it.valid && it.ageMs == 30000L && it.state == "23.4" } && !st.entities[1].valid && st.entities[1].slot == 1)
    check(st.calendarAgeMs == 120000L && st.calendar == "2026-10-12 09:00Z Rapat")
    // Truncated / trailing / out-of-range documents are refused.
    val cfgDoc = P.PageAssembler().also { it.accept(P.parsePage(body("response_integrations_get"))!!) }.document!!
    check(P.parseIntegrationsConfig(cfgDoc.copyOf(cfgDoc.size - 1)) == null && P.parseIntegrationsConfig(cfgDoc + byteArrayOf(0)) == null)
    check(P.parseIntegrationsConfig(cfgDoc.copyOf().also { it[1] = 8 }) == null && P.parseIntegrationsConfig(cfgDoc.copyOf().also { it[0] = 2 }) == null)
    val stDoc = P.PageAssembler().also { it.accept(P.parsePage(body("response_integrations_status"))!!) }.document!!
    check(P.parseIntegrationsStatus(stDoc.copyOf(stDoc.size - 1)) == null && P.parseIntegrationsStatus(stDoc + byteArrayOf(0)) == null)
    check(P.parseIntegrationsStatus(stDoc.copyOf().also { it[1] = 64 }) == null && P.parseIntegrationsStatus(ByteArray(0)) == null)
    // Request builders enforce the shape the firmware requires (the robot re-validates https/entity syntax).
    check(P.integrationsSetConfig(false, 90f, 180f, "", emptyList()) != null && P.integrationsSetConfig(false, 90.1f, 0f, "", emptyList()) == null)
    check(P.integrationsSetConfig(false, 0f, -180.1f, "", emptyList()) == null && P.integrationsSetConfig(false, Float.NaN, 0f, "", emptyList()) == null)
    check(P.integrationsSetConfig(false, 0f, 0f, "h".repeat(128), emptyList()) == null && P.integrationsSetConfig(false, 0f, 0f, "a b", emptyList()) == null)
    check(P.integrationsSetConfig(false, 0f, 0f, "", listOf("a.b", "c.d", "e.f", "g.h")) == null && P.integrationsSetConfig(false, 0f, 0f, "", listOf("")) == null)
    check(P.integrationsSetConfig(false, 0f, 0f, "", listOf("e".repeat(48))) == null && P.integrationsSetConfig(false, 0f, 0f, "", listOf("é.x")) == null)
    check(P.integrationsSetToken("a".repeat(191)) != null && P.integrationsSetToken("a".repeat(192)) == null && P.integrationsSetToken("a b") == null && P.integrationsSetToken("é") == null)
    check(P.integrationsSetCalendar("a".repeat(255)) != null && P.integrationsSetCalendar("a".repeat(256)) == null && P.integrationsSetCalendar("a\nb") == null)
    check(P.integrationsSetConfig(true, 1f, 2f, "h".repeat(127), listOf("e".repeat(47), "a.b", "c.d"))!!.payloadFor(SESSION)!!.size <= P.MAX_PAYLOAD)
    // Secrets never appear in toString, and wipe() clears them.
    val secretRequest = P.integrationsSetToken("TESTTOKEN-abc_123")!!
    check(!secretRequest.toString().contains("TESTTOKEN") && !cfg.toString().contains("ha.example"))
    secretRequest.wipe()
    check(secretRequest.payloadFor(SESSION)!!.all { it.toInt() == 0 })
    // Malformed responses are refused.
    check(P.parseResponse(ByteArray(5)) == null && P.parseResponse(ByteArray(501)) == null)
    check(P.parseResponse(byteArrayOf(0, 0, 0, 0, 7, 0)) == null && P.parseResponse(byteArrayOf(0, 0, 0, 0, 27, 0)) == null && P.parseResponse(byteArrayOf(0, 0, 0, 0, 26, 0)) != null)
}

private fun verifyPagingAndBounds() {
    val doc = settingsDoc()
    // CRC mismatch after the last page.
    val corrupt = doc.copyOf().also { it[100] = 'X'.code.toByte() }
    val bad = P.PageAssembler()
    check(bad.accept(P.parsePage(pageBody(doc, 0))!!) == P.PageAssembler.Step.NEXT)
    val tampered = pageBody(doc, 1).also { it[P.PAGE_HEADER + 3] = (it[P.PAGE_HEADER + 3].toInt() xor 1).toByte() }
    check(bad.accept(P.parsePage(tampered)!!) == P.PageAssembler.Step.CRC_MISMATCH && bad.document == null)
    // Header (CRC) change on a later page: restart at page 0, then a clean read succeeds.
    val a = P.PageAssembler()
    check(a.accept(P.parsePage(pageBody(doc, 0))!!) == P.PageAssembler.Step.NEXT)
    check(a.accept(P.parsePage(pageBody(corrupt, 1))!!) == P.PageAssembler.Step.RESTART && a.nextPage == 0)
    check(a.accept(P.parsePage(pageBody(corrupt, 0))!!) == P.PageAssembler.Step.NEXT)
    check(a.accept(P.parsePage(pageBody(corrupt, 1))!!) == P.PageAssembler.Step.DONE && a.document!!.contentEquals(corrupt))
    // Length change is also a restart; out of order / not started is a sequence error.
    val b = P.PageAssembler()
    b.accept(P.parsePage(pageBody(doc, 0))!!)
    check(b.accept(P.parsePage(pageBody(doc.copyOf(500), 1))!!) == P.PageAssembler.Step.RESTART)
    check(P.PageAssembler().accept(P.parsePage(pageBody(doc, 1))!!) == P.PageAssembler.Step.SEQUENCE_ERROR)
    val big = ByteArray(4096) { it.toByte() }
    check(P.settingsSetPages(big)!!.size == 9 && P.settingsSetPages(ByteArray(4097)) == null && P.settingsSetPages(ByteArray(0)) == null)
    val full = P.PageAssembler()
    var step = P.PageAssembler.Step.NEXT
    for (i in 0 until 9) step = full.accept(P.parsePage(pageBody(big, i))!!)
    check(step == P.PageAssembler.Step.DONE && full.document!!.contentEquals(big))
    val skip = P.PageAssembler()
    skip.accept(P.parsePage(pageBody(big, 0))!!)
    check(skip.accept(P.parsePage(pageBody(big, 2))!!) == P.PageAssembler.Step.SEQUENCE_ERROR)
    // parsePage strictness.
    val good = pageBody(doc, 0)
    check(P.parsePage(good.copyOf(good.size - 1)) == null && P.parsePage(good + byteArrayOf(0)) == null && P.parsePage(ByteArray(7)) == null)
    check(P.parsePage(good.copyOf().also { it[1] = 3 }) == null && P.parsePage(good.copyOf().also { it[0] = 2 }) == null)
    check(P.parsePage(good.copyOf().also { it[2] = 0; it[3] = 0 }) == null && P.parsePage(good.copyOf().also { it[2] = 1; it[3] = 0x10 }) == null)
    // Request bounds.
    check(P.settingsGet(8) != null && P.settingsGet(9) == null && P.settingsGet(-1) == null)
    check(P.secretWifi(3, "a", "b", false) == null && P.secretWifi(0, "a".repeat(33), "", false) == null && P.secretWifi(0, "a".repeat(32), "p".repeat(64), false) != null)
    check(P.secretWifi(0, "a", "p".repeat(65), false) == null && P.secretWifi(0, "a\u0001", "", false) == null && P.secretWifi(0, "a", "p\u007f", false) == null)
    check(P.secretWifi(0, "a", "pw", true) == null && P.secretWifi(0, "", "", true) == null && P.secretWifi(0, "", "pw", false) == null)
    check(P.secretGemini("a b") == null && P.secretGemini("k".repeat(97)) == null && P.secretGemini("k".repeat(96)) != null && P.secretGemini("") != null)
    check(P.reboot(0) == null && P.reboot(4) == null && P.reboot(1) != null)
    check(P.memoryAdd(6, "k", "v") == null && P.memoryAdd(0, "k".repeat(40), "v") == null && P.memoryAdd(0, "k", "v".repeat(160)) == null)
    check(P.memoryAdd(0, "k".repeat(39), "v".repeat(159)) != null && P.memoryAdd(0, " ", "v") == null && P.memoryAdd(0, "k", "") == null)
    check(P.memoryAdd(0, "k", "a\nb") == null && P.memoryAdd(0, "k", "a\u0085b") == null && P.memoryAdd(0, "k", "a b") == null && P.memoryAdd(0, "k", "a\u007fb") == null)
    check(P.memoryAdd(0, "é".repeat(20), "v") == null && P.memoryAdd(0, "é".repeat(19), "v") != null)
    check(P.memoryDelete(0) == null && P.memoryDelete(0x1_0000_0000L) == null && P.memoryDelete(0xffff_ffffL) != null)
    check(P.reminderAdd(3, 0, 0, "x") == null && P.reminderAdd(3, 0, 86_401, "x") == null && P.reminderAdd(3, 0, 86_400, "x") != null)
    check(P.reminderAdd(2, 1440, 0, "x") == null && P.reminderAdd(2, 480, 5, "x") == null && P.reminderAdd(1, 0, 0, "x") == null && P.reminderAdd(4, 0, 1, "x") == null)
    check(P.reminderAdd(3, 5, 60, "x") == null && P.reminderAdd(3, 0, 60, "t".repeat(112)) == null && P.reminderAdd(3, 0, 60, "t".repeat(111)) != null)
    // Confirm tokens need a valid session id.
    check(P.memoryClear().payloadFor(0) == null && P.factoryReset().payloadFor(SESSION)!!.size == 9)
    // Remote text is sanitised for display.
    check(P.plainText("a\u0007b c".toByteArray(), 0, "a\u0007b c".toByteArray().size) == "a�b�c")
    // Malformed documents are refused.
    check(P.parseStatus(ByteArray(57)) == null && P.parseStatus(ByteArray(59).also { it[0] = 1 }) == null && P.parseStatus(ByteArray(64).also { it[0] = 2 }) == null && P.parseStatus(ByteArray(66).also { it[0] = 2 }) != null)
    check(P.parseStatus(ByteArray(75).also { it[0] = 3 }) == null && P.parseStatus(ByteArray(76).also { it[0] = 3 }) != null && P.parseStatus(ByteArray(80).also { it[0] = 4 }) != null)
    // Live command builders refuse anything outside the documented bounds (same as the firmware shape check).
    check(P.expression(12) == null && P.expression(-1) == null && P.expression(11) != null && P.soundTest(12) == null && P.soundTest(11) != null)
    check(P.pauseInitiative(4) == null && P.pomodoro(0) == null && P.pomodoro(5) == null && P.pomodoro(4) != null)
    check(P.activity(1) == null && P.activity(1, 0) == null && P.activity(1, 14) == null && P.activity(1, 13) != null && P.activity(0) == null && P.activity(8) == null)
    check(P.activity(5, 3) == null && P.activity(7) != null && P.game(0) == null && P.game(3) == null && P.learning(3) == null && P.learning(2) != null)
    check(P.irSend(-1) == null && P.irSend(0x1_0000_0000L) == null && P.irSend(0xFFFFFFFFL)!!.payloadFor(0)!!.size == 6)
    check(P.irLearn("") == null && P.irLearn("a".repeat(16)) == null && P.irLearn("a".repeat(15)) != null && P.irLearn("tv/1") == null && P.irLearn("É") == null && P.irReplay("TV_1-a b") != null)
    check(P.expression(3)!!.payloadFor(0)!!.contentEquals(byteArrayOf(4, 3)) && P.experiencesClear().payloadFor(0)!!.contentEquals(byteArrayOf(13)))
    check(P.parseMemoryWindow(byteArrayOf(1, 0, 0, 0, 0, 1, 1, 0)) == null && P.parseMemoryWindow(byteArrayOf(2, 0, 0, 0, 0, 0, 0, 0)) == null)
    check(P.parseReminderDocument(byteArrayOf(1, 0, 0, 0, 0, 9)) == null)
}

private fun verifyStatusCodes() {
    for (code in 0..23) check(P.Status.fromCode(code)!!.code == code)
    check(P.Status.fromCode(24) == null && P.Status.fromCode(-1) == null)
    check(P.Status.entries.size == 24 && P.Status.REJECTED.code == 23 && P.Status.NOT_IMPLEMENTED.label == "Update via BLE belum tersedia")
    check(P.Status.CRC_MISMATCH.code == 6 && P.Status.CONFIRM_REQUIRED.code == 10 && P.Status.TIME_NOT_SET.code == 21 && P.Status.FORBIDDEN.code == 14)
    check(SettingsFailure(SettingsFailure.Reason.STATUS, P.Status.FULL, 18).message == P.Status.FULL.label)
    check(SettingsFailure(SettingsFailure.Reason.STATUS, null, 99).message.contains("99"))
    check(SettingsFailure(SettingsFailure.Reason.NOT_CONNECTED).message == "Robot belum terhubung")
}

private fun verifySecretsNeverLogged() {
    val originalOut = System.out
    val originalErr = System.err
    val captured = ByteArrayOutputStream()
    val capture = PrintStream(captured, true)
    val secrets = listOf("hunter2-wifi-pass", "MySecretSsid", "AIzaSyDONOTLOG-key_1")
    System.setOut(capture)
    System.setErr(capture)
    try {
        val wifi = P.secretWifi(0, secrets[1], secrets[0], false)!!
        val gemini = P.secretGemini(secrets[2])!!
        val text = "$wifi $gemini ${wifi.kind}"
        check(secrets.none { text.contains(it) }) { "Request.toString leaks" }
        val queue = RobotRequestScheduler(2)
        var outcome: SettingsOutcome<P.Response>? = null
        check(queue.enqueue(wifi) { outcome = it })
        queue.failAll(SettingsFailure(SettingsFailure.Reason.NOT_CONNECTED))
        check((outcome as SettingsOutcome.Fail).failure.message.let { m -> secrets.none { m.contains(it) } })
        // wipe() zeroes the payload buffer after the exchange.
        check(wifi.payloadFor(SESSION)!!.all { it == 0.toByte() })
        gemini.wipe()
        check(gemini.payloadFor(SESSION)!!.all { it == 0.toByte() })
    } finally {
        System.setOut(originalOut)
        System.setErr(originalErr)
    }
    check(captured.size() == 0) { "settings code wrote to stdout/stderr: ${captured.toString().take(40)}" }
    // The not-connected transport path also wipes and reports the failure.
    PhoneSettingsBridge.submit = null
    val request = P.secretGemini("AIzaSyAnother-key")!!
    val result = await<P.Response> { PhoneSettingsBridge.send(request, it) }
    check(result.reason() == SettingsFailure.Reason.NOT_CONNECTED && request.payloadFor(SESSION)!!.all { it == 0.toByte() })
}

private fun verifyQueue() {
    val queue = RobotRequestScheduler(2)
    val results = ArrayList<String>()
    fun cb(tag: String): SettingsDone<P.Response> = { results += tag + ":" + if (it is SettingsOutcome.Ok) "ok" else (it as SettingsOutcome.Fail).failure.reason.name }
    check(queue.enqueue(P.settingsGet(0)!!, cb("a")) && queue.enqueue(P.statusSnapshot(), cb("b")))
    check(!queue.enqueue(P.settingsGet(1)!!, cb("c"))) { "bounded: third request refused" }
    var sentKind = -1
    val started = queue.startNext(1000) { sentKind = it.kind; 41L }
    check(started?.kind == 8 && sentKind == 8 && queue.inFlight && queue.hasWaiting)
    check(queue.startNext(1000) { 99L } == null && sentKind == 8) { "one in flight" }
    check(!queue.onResponse(P.Response(41, 11, 0, ByteArray(0)))) { "kind mismatch ignored" }
    check(!queue.onResponse(P.Response(40, 8, 0, ByteArray(0)))) { "counter mismatch ignored" }
    check(!queue.expire(1000 + P.DEFAULT_TIMEOUT_MS - 1) && results.isEmpty())
    check(queue.onResponse(P.Response(41, 8, 0, ByteArray(0))) && !queue.inFlight && results == listOf("a:ok"))
    check(queue.startNext(2000) { 42L }?.kind == 11)
    // A read is re-sent once with a new counter after its timeout, then fails.
    check(queue.expire(2000 + P.DEFAULT_TIMEOUT_MS) && !queue.inFlight && queue.hasWaiting && results.last() == "a:ok")
    check(!queue.onResponse(P.Response(42, 11, 0, ByteArray(0)))) { "late response after timeout ignored" }
    check(queue.startNext(8000) { 52L }?.kind == 11 && !queue.onResponse(P.Response(42, 11, 0, ByteArray(0))))
    check(queue.expire(8000 + P.DEFAULT_TIMEOUT_MS) && results.last() == "b:TIMEOUT" && !queue.inFlight)
    // Send failure is reported, later entries still go out; failAll clears everything.
    check(queue.enqueue(P.settingsGet(0)!!, cb("d")) && queue.enqueue(P.settingsGet(1)!!, cb("e")))
    check(queue.startNext(3000) { if (it.payloadFor(0)!![0].toInt() == 0) null else 43L }?.kind == 8 && results.last() == "d:INVALID_REQUEST")
    queue.failAll(SettingsFailure(SettingsFailure.Reason.NOT_CONNECTED))
    check(results.takeLast(1) == listOf("e:NOT_CONNECTED") && !queue.inFlight && !queue.hasWaiting)
    // Last SettingsSet page and slow operations use the 10 s timeout.
    check(P.settingsSetPages(settingsDoc())!!.map { it.timeoutMs } == listOf(P.DEFAULT_TIMEOUT_MS, P.SLOW_TIMEOUT_MS) && P.memoryClear().timeoutMs == P.SLOW_TIMEOUT_MS)
}

private fun verifyClient() {
    // readSettings: the document changes after page 0 (restart), then reads cleanly; CRC is verified.
    val docA = settingsDoc()
    val docB = docA.copyOf().also { it[60] = 'Z'.code.toByte() }
    var current = docA
    var calls = 0
    val robot = FakeTransport { _, payload ->
        calls++
        val page = payload[0].toInt()
        if (calls == 2) current = docB
        0 to pageBody(current, page)
    }
    val read = await<ByteArray> { BridgeSettingsClient(robot).readSettings(it) }.value()
    check(read.contentEquals(docB) && robot.seen.map { it[0].toInt() } == listOf(0, 1, 0, 1)) { "restart rule: ${robot.seen.map { it[0] }}" }
    // Endless changes -> bounded restarts.
    var n = 0
    val flaky = FakeTransport { _, payload -> 0 to pageBody(if (payload[0].toInt() == 0) docA else docB.also { it[61] = (n++).toByte() }, payload[0].toInt()) }
    check(await<ByteArray> { BridgeSettingsClient(flaky).readSettings(it) }.reason() == SettingsFailure.Reason.TOO_MANY_RESTARTS)
    // Corrupted final page -> CRC failure; status code -> STATUS failure; timeout -> TIMEOUT.
    val crcBad = FakeTransport { _, payload -> 0 to pageBody(docA, payload[0].toInt()).also { if (payload[0].toInt() == 1) it[10] = 1 } }
    check(await<ByteArray> { BridgeSettingsClient(crcBad).readSettings(it) }.reason() == SettingsFailure.Reason.CRC)
    val busy = FakeTransport { _, _ -> 1 to ByteArray(0) }
    val busyResult = await<ByteArray> { BridgeSettingsClient(busy).readSettings(it) } as SettingsOutcome.Fail
    check(busyResult.failure.reason == SettingsFailure.Reason.STATUS && busyResult.failure.status == P.Status.BUSY)
    check(await<ByteArray> { BridgeSettingsClient(FakeTransport { _, _ -> null }).readSettings(it) }.reason() == SettingsFailure.Reason.TIMEOUT)

    // writeSettings: two pages in order; applied flag only on the last.
    val writer = FakeTransport { _, payload -> 0 to byteArrayOf(payload[0], if (payload[0].toInt() == 1) 1 else 0) }
    check(await<Boolean> { BridgeSettingsClient(writer).writeSettings(docA, it) }.value() && writer.seen.size == 2 && writer.seen[1][0].toInt() == 1)
    val rejecting = FakeTransport { _, payload -> if (payload[0].toInt() == 1) 6 to byteArrayOf(1, 0) else 0 to byteArrayOf(0, 0) }
    val rejected = await<Boolean> { BridgeSettingsClient(rejecting).writeSettings(docA, it) } as SettingsOutcome.Fail
    check(rejected.failure.status == P.Status.CRC_MISMATCH)
    check(await<Boolean> { BridgeSettingsClient(writer).writeSettings(ByteArray(0), it) }.reason() == SettingsFailure.Reason.INVALID_REQUEST)

    // Status snapshot through the client.
    val statusDoc = ByteArray(58).also { it[0] = 1 }
    check(await<P.StatusSnapshot> { BridgeSettingsClient(FakeTransport { _, _ -> 0 to pageBody(statusDoc, 0) }).readStatus(it) }.value().schema == 1)

    // Memory: unchanged revision stops after one round trip; changed revision reads the full list; CRC is enforced.
    val facts = ByteArrayOutputStream()
    fun fact(id: Int, key: String, value: String): ByteArray {
        val k = key.toByteArray(); val v = value.toByteArray()
        return byteArrayOf(id.toByte(), 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 3, k.size.toByte(), v.size.toByte()) + k + v
    }
    val entries = fact(1, "drink", "kopi") + fact(2, "nama", "Eric")
    fun memDoc(revision: Long, raw: ByteArray, count: Int = 2) = byteArrayOf(1, revision.toByte(), (revision shr 8).toByte(), (revision shr 16).toByte(), (revision shr 24).toByte(), count.toByte(), count.toByte(), 0) + raw
    val revision = P.crc32(entries)
    facts.write(entries)
    val memRobot = FakeTransport { _, payload -> 0 to pageBody(memDoc(revision, entries), payload[4].toInt()) }
    val same = await<BridgeSettingsClient.MemoryList> { BridgeSettingsClient(memRobot).listMemory(revision, it) }.value()
    check(same.unchanged && memRobot.seen.size == 1)
    val changed = await<BridgeSettingsClient.MemoryList> { BridgeSettingsClient(memRobot).listMemory(revision + 1, it) }.value()
    check(!changed.unchanged && changed.facts.map { it.key } == listOf("drink", "nama") && changed.revision == revision)
    val wrongCrc = FakeTransport { _, payload -> 0 to pageBody(memDoc(revision + 7, entries), payload[4].toInt()) }
    check(await<BridgeSettingsClient.MemoryList> { BridgeSettingsClient(wrongCrc).listMemory(null, it) }.reason() == SettingsFailure.Reason.CRC)
    // Two windows: "more" continues from the last id.
    val firstEntries = fact(1, "drink", "kopi")
    val secondEntries = fact(2, "nama", "Eric")
    val twoRevision = P.crc32(firstEntries + secondEntries)
    val windowed = FakeTransport { _, payload ->
        val afterId = payload[0].toInt()
        if (afterId == 0) 0 to pageBody(memDoc(twoRevision, firstEntries, 1).also { it[5] = 2; it[7] = 1 }, 0)
        else 0 to pageBody(memDoc(twoRevision, secondEntries, 1).also { it[5] = 2 }, 0)
    }
    val windows = await<BridgeSettingsClient.MemoryList> { BridgeSettingsClient(windowed).listMemory(null, it) }.value()
    check(windows.facts.size == 2 && windowed.seen.map { it[0].toInt() } == listOf(0, 1))

    // Mutations: parse bodies; clear carries the session id; non-Ok maps to STATUS.
    val mut = FakeTransport { request, _ ->
        when (request.kind) {
            P.KIND_MEMORY_ADD, P.KIND_REMINDER_ADD -> 0 to byteArrayOf(5, 0, 0, 0, 9, 0, 0, 0)
            P.KIND_MEMORY_CLEAR -> 0 to ByteArray(4)
            else -> 19 to ByteArray(0)
        }
    }
    val client = BridgeSettingsClient(mut)
    check(await<Pair<Long, Long>> { client.addMemory(1, "drink", "kopi", it) }.value() == (5L to 9L))
    check(await<Pair<Long, Long>> { client.addReminder(3, 0, 60, "Minum", it) }.value() == (5L to 9L))
    check(await<Long> { client.clearMemory(it) }.value() == 0L && hex(mut.seen.last()) == "4d434c5212345678")
    check((await<Long> { client.deleteMemory(77, it) } as SettingsOutcome.Fail).failure.status == P.Status.NOT_FOUND)
    check(await<Pair<Long, Long>> { client.addMemory(9, "x", "y", it) }.reason() == SettingsFailure.Reason.INVALID_REQUEST)

    // IR profiles and integrations through the client: bodies parse, Busy maps to STATUS, bad input never reaches the robot.
    val irRobot = FakeTransport { _, _ -> 0 to byteArrayOf(1, 3, 'T'.code.toByte(), 'V'.code.toByte(), '1'.code.toByte()) }
    check(await<List<String>> { BridgeSettingsClient(irRobot).listIrProfiles(it) }.value() == listOf("TV1"))
    val intRobot = FakeTransport { request, _ -> if (request.kind == P.KIND_INTEGRATIONS_ACTION) 1 to ByteArray(0) else 0 to ByteArray(0) }
    val intClient = BridgeSettingsClient(intRobot)
    check(await<Unit> { intClient.setIntegrationsToken("abc", it) } is SettingsOutcome.Ok && await<Unit> { intClient.setIntegrationsCalendar("", it) } is SettingsOutcome.Ok)
    check(await<Unit> { intClient.setIntegrationsConfig(true, 1f, 2f, "https://x.example", listOf("a.b"), it) } is SettingsOutcome.Ok)
    check((await<Unit> { intClient.clearIntegrations(it) } as SettingsOutcome.Fail).failure.status == P.Status.BUSY)
    check((await<Unit> { intClient.refreshIntegrations(it) } as SettingsOutcome.Fail).failure.status == P.Status.BUSY)
    val seenBefore = intRobot.seen.size
    check(await<Unit> { intClient.setIntegrationsToken("has space", it) }.reason() == SettingsFailure.Reason.INVALID_REQUEST && intRobot.seen.size == seenBefore)
}
