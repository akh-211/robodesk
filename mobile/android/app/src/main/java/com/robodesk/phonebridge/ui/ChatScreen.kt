package com.robodesk.phonebridge.ui

import androidx.compose.foundation.background
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.ChatMessage

private val REPLY_MODES = listOf("Suara", "Teks di layar robot", "Suara + teks")

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
                Text("Tulis pesan untuk robot. Robot menjawab dengan AI sesuai pilihan di atas. Riwayat chat hanya ada selama app terbuka dan tidak disimpan.",
                    color = RoboColors.Mute, fontSize = 13.sp, modifier = Modifier.padding(top = 12.dp))
            }
            chat.messages.forEach { Bubble(it) }
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
private fun Bubble(m: ChatMessage) {
    val mine = !m.fromRobot
    val shape = RoundedCornerShape(14.dp)
    val text = when {
        m.status == ChatMessage.Status.WAITING -> "Robot berpikir…"
        m.status == ChatMessage.Status.ANSWERING -> m.text + " ▍"
        else -> m.text
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = if (mine) Arrangement.End else Arrangement.Start) {
        Column(Modifier.fillMaxWidth(0.86f)) {
            Box(Modifier.align(if (mine) Alignment.End else Alignment.Start).clip(shape).background(if (mine) RoboColors.Accent.copy(alpha = 0.22f) else RoboColors.Card).padding(horizontal = 12.dp, vertical = 9.dp)) {
                Text(text, color = if (m.status == ChatMessage.Status.WAITING) RoboColors.Mute else RoboColors.Text, fontSize = 15.sp)
            }
            val note = when {
                m.status == ChatMessage.Status.SENDING -> "mengirim…"
                m.status == ChatMessage.Status.FAILED -> m.error ?: "gagal"
                else -> null
            }
            if (note != null) Text(note, color = if (m.status == ChatMessage.Status.FAILED) RoboColors.Bad else RoboColors.Mute, fontSize = 11.5.sp,
                modifier = Modifier.align(if (mine) Alignment.End else Alignment.Start).padding(top = 2.dp, start = 4.dp, end = 4.dp))
        }
    }
}
