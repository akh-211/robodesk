package com.robodesk.phonebridge

import com.robodesk.phonebridge.BridgeSettingsProtocol as P

/** What the controller needs from the robot link: one send and one poll (see [RobotRepository.chatSend] / [RobotRepository.chatPoll]). */
interface ChatPort {
    fun send(text: String, done: Done<Long>)
    fun poll(turn: Long, offset: Int, done: Done<P.ChatPoll>)
}

class ChatMessage(val fromRobot: Boolean, text: String, status: Status) {
    enum class Status { SENDING, WAITING, ANSWERING, DONE, FAILED }

    var text: String = text
        internal set
    var status: Status = status
        internal set

    /** Indonesian reason when [status] is FAILED. */
    var error: String? = null
        internal set
}

/**
 * Chat session with the robot (BLE kinds 25/26). One turn at a time: [send] queues a line, then [tick] (about every 600 ms while
 * [busy]) reads the reply in chunks until the robot reports done or failed. The history lives only in this object (RAM): it is
 * never written to storage and [clear] wipes it. Pure logic so it runs in host tests.
 */
class ChatController(private val port: ChatPort, private val now: () -> Long, private val replyDeadlineMs: Long = REPLY_DEADLINE_MS) {
    private val items = ArrayList<ChatMessage>()
    private var turn = 0L
    private var received = java.io.ByteArrayOutputStream()
    private var polling = false
    private var sending = false
    private var errors = 0
    private var startedAt = 0L
    private var reply: ChatMessage? = null

    val messages: List<ChatMessage> get() = items
    /** True from the moment a line is sent until the reply is complete or failed: no new line may be sent. */
    val busy: Boolean get() = sending || turn != 0L

    /** Changes (new text/status) are announced here; the UI re-reads [messages]. */
    var onChange: () -> Unit = {}

    /** Returns false when a turn is still running or the line is empty/too long. */
    fun send(text: String): Boolean {
        if (busy) return false
        val line = P.chatLine(text) ?: return false
        sending = true
        errors = 0
        val mine = ChatMessage(false, line, ChatMessage.Status.SENDING)
        items.add(mine)
        onChange()
        port.send(line) { res ->
            sending = false
            when (res) {
                is Res.Ok -> {
                    mine.status = ChatMessage.Status.DONE
                    turn = res.value
                    received = java.io.ByteArrayOutputStream()
                    startedAt = now()
                    reply = ChatMessage(true, "", ChatMessage.Status.WAITING).also { items.add(it) }
                }
                is Res.Err -> { mine.status = ChatMessage.Status.FAILED; mine.error = res.message }
            }
            onChange()
        }
        return true
    }

    /** Asks the robot for the next piece of the reply. Does nothing when no turn is running or a poll is already in flight. */
    fun tick() {
        val t = turn
        val r = reply
        if (t == 0L || r == null || polling) return
        if (now() - startedAt > replyDeadlineMs) return fail(r, "Robot tidak menjawab tepat waktu")
        polling = true
        port.poll(t, received.size()) { res ->
            polling = false
            if (turn != t) return@poll
            when (res) {
                is Res.Err -> { if (++errors >= MAX_ERRORS) fail(r, res.message) }
                is Res.Ok -> { errors = 0; apply(r, res.value) }
            }
            onChange()
        }
    }

    private fun apply(r: ChatMessage, p: P.ChatPoll) {
        if (p.turn != turn || p.state == 0) return fail(r, "Jawaban robot tidak tersedia lagi")
        if (p.offset == received.size() && p.chunk.isNotEmpty()) received.write(p.chunk)
        val text = String(received.toByteArray(), Charsets.UTF_8)
        if (text.isNotEmpty()) { r.text = text; r.status = ChatMessage.Status.ANSWERING }
        when (p.state) {
            5 -> fail(r, failText(p.fail))
            4 -> if (received.size() >= p.total) { r.status = ChatMessage.Status.DONE; finish() }
        }
    }

    private fun fail(r: ChatMessage, why: String) {
        r.status = ChatMessage.Status.FAILED
        r.error = why
        finish()
        onChange()
    }

    private fun finish() { turn = 0L; reply = null }

    /** Wipes the history (leaving the chat, forgetting the robot). A running turn is abandoned. */
    fun clear() {
        items.clear()
        finish()
        sending = false
        received = java.io.ByteArrayOutputStream()
        onChange()
    }

    companion object {
        const val REPLY_DEADLINE_MS = 100_000L
        const val MAX_ERRORS = 5

        fun failText(code: Int): String = when (code) {
            1 -> "Robot gagal mengirim pesan ke Gemini"
            2 -> "Robot tidak menjawab tepat waktu"
            3 -> "Koneksi robot ke Gemini terputus"
            4 -> "Percakapan terputus sebelum ada jawaban"
            else -> "Robot gagal menjawab"
        }
    }
}
