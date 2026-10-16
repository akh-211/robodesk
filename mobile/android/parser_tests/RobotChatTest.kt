package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

/** Chat codec (kinds 25/26) and the ChatController state machine against a scripted port. */
fun verifyRobotChat() {
    verifyChatCodec()
    verifyChatController()
}

private fun u16(b: ByteArray, o: Int) = (b[o].toInt() and 0xff) or ((b[o + 1].toInt() and 0xff) shl 8)

private fun verifyChatCodec() {
    check(P.KIND_CHAT_SEND == 25 && P.KIND_CHAT_POLL == 26)
    // Whitespace/control runs collapse to one space; empty and over-long lines are refused.
    check(P.chatLine("  Halo \n\t robot  ") == "Halo robot")
    check(P.chatLine("   \n ") == null && P.chatLine("") == null)
    check(P.chatLine("x".repeat(280)) != null && P.chatLine("x".repeat(281)) == null)
    check(P.chatLine("é".repeat(141)) == null && P.chatLine("é".repeat(140)) != null)  // limit is bytes, not characters
    val send = P.chatSend("Halo robot, apa kabar?")!!
    check(send.kind == P.KIND_CHAT_SEND)
    val payload = send.payloadFor(1L)!!
    check(u16(payload, 0) == payload.size - 2 && String(payload, 2, payload.size - 2, Charsets.UTF_8) == "Halo robot, apa kabar?")
    check(P.chatSend("  ") == null)
    check(RequestPriority.mutating(P.KIND_CHAT_SEND) && !RequestPriority.mutating(P.KIND_CHAT_POLL))
    val poll = P.chatPoll(7L, 180)!!.payloadFor(1L)!!
    check(poll.size == 6 && (poll[0].toInt() and 0xff) == 7 && u16(poll, 4) == 180)
    check(P.chatPoll(0L, 641) == null && P.chatPoll(-1L, 0) == null)
    // Responses: a turn id, and a poll body whose lengths must add up exactly.
    check(P.parseChatTurn(byteArrayOf(5, 0, 0, 0)) == 5L && P.parseChatTurn(byteArrayOf(0, 0, 0, 0)) == null && P.parseChatTurn(byteArrayOf(1, 0, 0)) == null)
    fun body(turn: Int, state: Int, fail: Int, total: Int, offset: Int, chunk: String): ByteArray {
        val c = chunk.toByteArray(Charsets.UTF_8)
        val b = ByteArray(11 + c.size)
        b[0] = turn.toByte(); b[4] = state.toByte(); b[5] = fail.toByte()
        b[6] = total.toByte(); b[7] = (total shr 8).toByte(); b[8] = offset.toByte(); b[9] = (offset shr 8).toByte(); b[10] = c.size.toByte()
        c.copyInto(b, 11)
        return b
    }
    val ok = P.parseChatPoll(body(3, 3, 0, 10, 0, "Halo"))!!
    check(ok.turn == 3L && ok.state == 3 && ok.total == 10 && ok.offset == 0 && String(ok.chunk) == "Halo")
    check(P.parseChatPoll(body(3, 3, 0, 10, 0, "Halo").copyOf(14)) == null)               // truncated chunk
    check(P.parseChatPoll(body(3, 3, 0, 3, 0, "Halo")) == null)                           // chunk beyond total
    check(P.parseChatPoll(body(3, 6, 0, 4, 0, "Halo")) == null && P.parseChatPoll(body(3, 5, 9, 4, 0, "Halo")) == null)  // unknown state / reason
    check(P.parseChatPoll(body(3, 3, 0, 641, 0, "Halo")) == null)                         // reply cap
    // Status 0x7d responses accept kinds up to 26.
    val raw = ByteArray(6); raw[4] = 26
    check(P.parseResponse(raw) != null && P.parseResponse(ByteArray(6).also { it[4] = 27 }) == null)
}

private class FakePort(var clock: Long) : ChatPort {
    val sent = ArrayList<String>()
    var sendResult: Res<Long> = Res.Ok(11L)
    val polls = ArrayList<Pair<Long, Int>>()
    var pollScript: (Long, Int) -> Res<P.ChatPoll> = { _, _ -> Res.Err("x") }
    override fun send(text: String, done: Done<Long>) { sent.add(text); done(sendResult) }
    override fun poll(turn: Long, offset: Int, done: Done<P.ChatPoll>) { polls.add(turn to offset); done(pollScript(turn, offset)) }
}

private fun chatBody(turn: Long, state: Int, fail: Int, total: Int, offset: Int, chunk: String) =
    Res.Ok(P.ChatPoll(turn, state, fail, total, offset, chunk.toByteArray(Charsets.UTF_8)))

private fun verifyChatController() {
    // Happy path: queued, then a reply read in two chunks, then done.
    var port = FakePort(0L)
    var chat = ChatController(port, { port.clock })
    var changes = 0
    chat.onChange = { changes++ }
    check(!chat.send("   ") && port.sent.isEmpty())
    check(chat.send("Halo  robot") && port.sent == listOf("Halo robot") && chat.busy)
    check(!chat.send("lagi") && port.sent.size == 1)                                       // one turn at a time
    check(chat.messages.size == 2 && !chat.messages[0].fromRobot && chat.messages[1].fromRobot && chat.messages[1].status == ChatMessage.Status.WAITING)
    port.pollScript = { t, o -> if (o == 0) chatBody(t, 4, 0, 12, 0, "Halo, ") else chatBody(t, 4, 0, 12, o, "dunia!") }
    chat.tick()
    check(chat.messages[1].text == "Halo, " && chat.messages[1].status == ChatMessage.Status.ANSWERING && chat.busy)
    chat.tick()
    check(port.polls == listOf(11L to 0, 11L to 6) && chat.messages[1].text == "Halo, dunia!" && chat.messages[1].status == ChatMessage.Status.DONE && !chat.busy)
    check(changes > 0)
    chat.tick(); check(port.polls.size == 2)                                                // nothing running: no polling
    // A reply split in the middle of a multi-byte character reassembles correctly.
    port = FakePort(0L); chat = ChatController(port, { port.clock })
    check(chat.send("halo"))
    val bytes = "kopi é".toByteArray(Charsets.UTF_8)
    port.pollScript = { t, o ->
        if (o == 0) Res.Ok(P.ChatPoll(t, 4, 0, bytes.size, 0, bytes.copyOfRange(0, 6)))      // cuts "é" after its first byte
        else Res.Ok(P.ChatPoll(t, 4, 0, bytes.size, o, bytes.copyOfRange(o, bytes.size)))
    }
    chat.tick(); chat.tick()
    check(chat.messages[1].text == "kopi é" && chat.messages[1].status == ChatMessage.Status.DONE)
    // Send refused by the robot: the line is marked failed with the reason, and a new line may be sent.
    port = FakePort(0L); chat = ChatController(port, { port.clock })
    port.sendResult = Res.Err("Robot belum terhubung ke Gemini")
    check(chat.send("halo") && chat.messages.size == 1 && chat.messages[0].status == ChatMessage.Status.FAILED && chat.messages[0].error!!.contains("Gemini") && !chat.busy)
    port.sendResult = Res.Ok(12L); check(chat.send("lagi") && chat.busy)
    // Robot-side failure: the reason is shown and the turn ends.
    port.pollScript = { t, _ -> chatBody(t, 5, 3, 0, 0, "") }
    chat.tick()
    check(chat.messages.last().status == ChatMessage.Status.FAILED && chat.messages.last().error == ChatController.failText(3) && !chat.busy)
    // The robot forgot the turn (idle): fail instead of waiting forever.
    check(chat.send("lagi dan lagi")); port.pollScript = { t, _ -> chatBody(t, 0, 0, 0, 0, "") }
    chat.tick(); check(chat.messages.last().status == ChatMessage.Status.FAILED && !chat.busy)
    // Poll errors: tolerated a few times, then the turn fails; success resets the counter.
    check(chat.send("cek")); var n = 0
    port.pollScript = { t, o -> if (++n <= 4) Res.Err("Tidak ada respons dari robot") else chatBody(t, 4, 0, 2, o, "ok") }
    repeat(4) { chat.tick() }; check(chat.busy)
    chat.tick(); check(chat.messages.last().text == "ok" && !chat.busy)
    check(chat.send("cek lagi")); port.pollScript = { _, _ -> Res.Err("Tidak ada respons dari robot") }
    repeat(5) { chat.tick() }
    check(chat.messages.last().status == ChatMessage.Status.FAILED && !chat.busy)
    // Deadline: no terminal state within the limit fails the turn without another poll.
    port = FakePort(0L); chat = ChatController(port, { port.clock }, 1000L)
    check(chat.send("lambat")); port.pollScript = { t, o -> chatBody(t, 4, 0, 50, o, "") }
    chat.tick(); check(chat.busy)
    port.clock = 1500L; val before = port.polls.size; chat.tick()
    check(!chat.busy && chat.messages.last().status == ChatMessage.Status.FAILED && port.polls.size == before)
    // A Gemini turn (queued/thinking/replying): the app stops after the first poll; the answer is on the robot, not in the app.
    port = FakePort(0L); chat = ChatController(port, { port.clock })
    check(chat.send("ceritakan sesuatu")); port.pollScript = { t, _ -> chatBody(t, 2, 0, 0, 0, "") }
    chat.tick()
    check(!chat.busy && chat.messages.size == 1 && chat.messages[0].handedToRobot && chat.messages[0].status == ChatMessage.Status.DONE)
    chat.tick(); check(port.polls.size == 1)
    // clear() wipes the history and abandons a running turn; a late poll answer is ignored.
    port = FakePort(0L); chat = ChatController(port, { port.clock })
    check(chat.send("rahasia"))
    chat.clear(); check(chat.messages.isEmpty() && !chat.busy)
    check(chat.send("baru") && chat.messages.size == 2)
}
