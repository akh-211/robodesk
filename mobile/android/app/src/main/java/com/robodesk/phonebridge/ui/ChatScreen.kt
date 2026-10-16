package com.robodesk.phonebridge.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.ChatMessage

private val REPLY_MODES = listOf("Suara", "Teks di layar robot", "Suara + teks")

/** Quick commands the robot runs itself (also without Gemini / Wi-Fi). Label shown, text sent. */
private val QUICK_COMMANDS = listOf("Status" to "/status", "Timer 5 mnt" to "/timer 5", "Mulai fokus" to "/fokus", "Jeda fokus" to "jeda fokus",
    "Jeda inisiatif" to "jeda inisiatif", "Lanjut inisiatif" to "lanjut inisiatif", "Senang" to "/senang", "Sedih" to "/sedih", "Malu" to "/malu", "Kaget" to "/kaget", "Bantuan" to "/help")

/** The robot asked "Maksud kamu ...? Balas \"ya\"" (offline near-miss suggestion): offer a one-tap answer. */
private fun asksConfirm(m: ChatMessage?): Boolean = m != null && m.fromRobot && m.status == ChatMessage.Status.DONE && m.text.contains("Balas \"ya\"")

/** Chat with the robot: the text goes to Gemini on the robot; the answer comes back here and on the robot as voice, screen text or both. */
@Composable
fun ChatScreen() {
    val app = RoboApp
    app.chatVersion // recompose when the history changes
    val chat = app.chat
    var draft by remember { mutableStateOf("") }
    val scroll = rememberScrollState()
    LaunchedEffect(app.chatVersion) { scroll.animateScrollTo(scroll.maxValue) }
    Column(Modifier.fillMaxSize().imePadding().padding(horizontal = 16.dp, vertical = 8.dp)) {
        val d = app.draft
        if (d != null && d.has("chatReplyMode")) {
            SelectRow("Jawaban robot", REPLY_MODES, d.int("chatReplyMode").coerceIn(0, 2), { i -> app.setNow("chatReplyMode", i) },
                "Teks tampil di layar robot, suara lewat speaker", enabled = app.canWrite, last = true)
        }
        Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(scroll).padding(vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            if (chat.messages.isEmpty()) {
                Text("Tulis pesan untuk robot. Jawaban AI muncul di layar robot atau lewat suara, tidak ditampilkan di sini. Perintah seperti /status, /timer 5 atau /fokus dijalankan robot sendiri (juga tanpa internet) dan jawabannya tampil di sini. Riwayat chat hanya ada selama app terbuka dan tidak disimpan.",
                    color = RoboColors.Mute, fontSize = 13.sp, modifier = Modifier.padding(top = 12.dp))
            }
            chat.messages.forEach { Bubble(it.fromRobot, it.text, it.status, it.error, it.handedToRobot) }
        }
        val canSend = app.ready && !chat.busy
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            if (asksConfirm(chat.messages.lastOrNull())) QuickChip("Ya", canSend, accent = true) { chat.send("ya") }
            QUICK_COMMANDS.forEach { (label, text) -> QuickChip(label, canSend) { chat.send(text) } }
        }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f)) {
                TextFieldRobo(draft, { draft = it }, placeholder = if (app.ready) "Tulis pesan…" else "Robot belum terhubung", enabled = app.ready && !chat.busy, maxLength = 280, singleLine = false)
            }
            RoboButton("Kirim", { if (chat.send(draft)) draft = "" else app.say("Tulis pesan 1-280 karakter") }, enabled = app.ready && !chat.busy && draft.isNotBlank())
        }
    }
}

@Composable
private fun QuickChip(label: String, enabled: Boolean, accent: Boolean = false, onClick: () -> Unit) {
    Text(label, color = if (accent) RoboColors.BtnPrimaryText else RoboColors.Text, fontSize = 13.sp,
        modifier = Modifier.alpha(if (enabled) 1f else 0.4f).clip(RoundedCornerShape(50)).background(if (accent) RoboColors.Accent else RoboColors.BtnSecondary)
            .clickable(enabled = enabled, onClick = onClick).padding(horizontal = 12.dp, vertical = 7.dp))
}

@Composable
private fun Bubble(fromRobot: Boolean, msgText: String, status: ChatMessage.Status, error: String?, handedToRobot: Boolean) {
    val mine = !fromRobot
    val shape = RoundedCornerShape(14.dp)
    val text = when {
        status == ChatMessage.Status.WAITING -> "Robot berpikir…"
        status == ChatMessage.Status.ANSWERING -> msgText + " ▍"
        else -> msgText
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = if (mine) Arrangement.End else Arrangement.Start) {
        Column(Modifier.fillMaxWidth(0.86f)) {
            Box(Modifier.align(if (mine) Alignment.End else Alignment.Start).clip(shape).background(if (mine) RoboColors.Accent.copy(alpha = 0.22f) else RoboColors.Card).padding(horizontal = 12.dp, vertical = 9.dp)) {
                Text(text, color = if (status == ChatMessage.Status.WAITING) RoboColors.Mute else RoboColors.Text, fontSize = 15.sp)
            }
            val note = when {
                status == ChatMessage.Status.SENDING -> "mengirim…"
                status == ChatMessage.Status.FAILED -> error ?: "gagal"
                handedToRobot -> "terkirim · jawaban di layar/suara robot"
                else -> null
            }
            if (note != null) Text(note, color = if (status == ChatMessage.Status.FAILED) RoboColors.Bad else RoboColors.Mute, fontSize = 11.5.sp,
                modifier = Modifier.align(if (mine) Alignment.End else Alignment.Start).padding(top = 2.dp, start = 4.dp, end = 4.dp))
        }
    }
}
