package com.robodesk.phonebridge.ui

import com.robodesk.phonebridge.PrivacyState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.draw.scale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.BridgeSettingsProtocol as P
import com.robodesk.phonebridge.LiveAvailability
import kotlinx.coroutines.delay

private val EXPRESSION_LABELS = listOf("happy · senang", "curious · penasaran", "shy · malu", "love · sayang", "sad · sedih", "surprised · kaget",
    "thinking · berpikir", "agree · setuju", "disagree · tidak setuju", "wink · kedip", "laugh · tertawa", "sleepy · ngantuk")

@Composable
fun Banner(text: String, action: String, onAction: () -> Unit) {
    val shape = RoundedCornerShape(14.dp)
    Row(Modifier.fillMaxWidth().clip(shape).background(RoboColors.SaveBarBg).padding(12.dp), horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(text, color = RoboColors.Warn, fontSize = 13.sp, modifier = Modifier.weight(1f))
        RoboButton(action, onAction)
    }
}

@Composable
private fun BlinkingFace(asleep: Boolean = false) {
    var closed by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { while (true) { delay(3800); closed = true; delay(160); closed = false } }
    val scaleY by animateFloatAsState(if (closed || asleep) 0.1f else 1f, label = "blink") // mirrors the robot: closed while it sleeps
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically, modifier = Modifier.height(44.dp)) {
        repeat(2) { Box(Modifier.width(14.dp).height(32.dp).scale(1f, scaleY).clip(RoundedCornerShape(99.dp)).background(RoboColors.Accent)) }
    }
}

@Composable
fun HomeScreen() {
    val app = RoboApp
    val st = app.status
    val draft = app.draft
    val w = app.canWrite && draft != null
    ScreenColumn {
        if (draft != null && app.repo.isIphoneRobot(draft))
            Banner("Robot ini terdaftar untuk iPhone; lakukan reset pabrik (sentuh/serial) agar bisa dipakai dengan Android", "Lihat") { app.openSub("dng") }
        if (st != null && !st.wifiSlotConfigured) Banner("Wi-Fi robot belum diatur. Robot butuh Wi-Fi untuk Gemini, jam, dan update.", "Atur") { app.openSub("net") }
        else if (st != null && st.geminiKey && draft != null && draft.int("cloudConsent") != 1) Banner("AI cloud mati sampai Anda menyetujui pengiriman suara ke Gemini.", "Tinjau") { app.sheet = SheetKind.CLOUD_CONSENT }
        else if (st != null && !st.geminiKey) Banner("Kunci API Gemini belum dipasang. Robot butuh kunci agar bisa bercakap.", "Atur") { app.openSub("net") }

        RoboCard {
            Row(horizontalArrangement = Arrangement.spacedBy(14.dp), verticalAlignment = Alignment.CenterVertically) {
                BlinkingFace(asleep = st?.s?.sleepState in 3..4)
                Column(Modifier.weight(1f)) {
                    Text(draft?.string("robotName")?.ifEmpty { "RoboDesk" } ?: "RoboDesk", color = RoboColors.Text, fontSize = 17.sp, fontWeight = FontWeight.Bold)
                    Text("RoboDesk · C3 ${st?.c3Version ?: "-"} / S3 ${st?.s3Version ?: "-"}", color = RoboColors.Mute, fontSize = 12.sp)
                    st?.moodText(java.util.Calendar.getInstance().get(java.util.Calendar.HOUR_OF_DAY))?.let { Text(it, color = RoboColors.Text, fontSize = 13.sp) } // PD-2.4 mood mirror
                    Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        if (app.ready) { Tag("Sesi HMAC aktif", TagKind.OK); Tag("Bond LESC", TagKind.OK) } else Tag("Belum ada sesi", TagKind.WARN)
                    }
                }
            }
        }
        RoboCard("Status robot") {
            InfoRow("Link C3 ↔ S3", st?.linkText ?: "-", if (st?.s.let { it != null && it.uartConnected }) RoboColors.Ok else RoboColors.Mute)
            InfoRow("Wi-Fi", st?.wifiText ?: "-")
            InfoRow("Kunci Gemini", if (st == null) "-" else if (st.geminiKey) "Terpasang" else "Belum ada", if (st?.geminiKey == true) RoboColors.Ok else RoboColors.Mute)
            InfoRow("Privasi", PrivacyState.mic(draft?.bool("privacyMicMuted") == true) + " · " + PrivacyState.cloud(st?.geminiKey, draft?.let { it.int("cloudConsent") == 1 })) // PD-4.14
            InfoRow("Waktu aktif", st?.uptimeText ?: "-")
            InfoRow("Heap bebas C3 / S3", st?.heapText ?: "-", last = true)
        }
        RoboCard("Aksi cepat") {
            SwitchRow("Matikan mikrofon", draft?.bool("privacyMicMuted") == true, { v -> app.toggle("privacyMicMuted", v) }, "privacyMicMuted", enabled = w)
            SwitchRow("Suara utama", draft?.bool("masterSound") == true, { v -> app.toggle("masterSound", v) }, "masterSound", enabled = w, last = true)
            Gap(12)
            ButtonRow {
                RoboButton("Reboot…", { app.sheetArg = 0; app.sheet = SheetKind.REBOOT }, Modifier.weight(1f), ButtonKind.SECONDARY, enabled = app.canWrite)
                RoboButton("Cek update", { app.openSub("upd") }, Modifier.weight(1f), ButtonKind.SECONDARY)
            }
            Gap(8)
            RoboButton("Chat dengan robot", { app.openTab(1) }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY)
        }
        LiveControls(st, app.canWrite)
    }
}

@Composable
private fun LiveControls(st: com.robodesk.phonebridge.StatusView?, w: Boolean) {
    val app = RoboApp
    var expr by remember { mutableStateOf(0) }
    val a = LiveAvailability(st?.s)
    RoboCard("Kontrol langsung") {
        RoboField("Ekspresi (expression)") {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                SelectBox(EXPRESSION_LABELS, expr, { expr = it }, Modifier.weight(2f), enabled = w)
                RoboButton("Tampilkan", { app.call("Ekspresi ditampilkan di robot", { d -> app.repo.expression(expr, d) }) }, Modifier.weight(1f), enabled = w)
            }
        }
        ButtonRow {
            RoboButton("Ajak bicara", { app.call("Robot mulai mendengarkan (talk)", { d -> app.repo.talk(d) }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
            RoboButton("Jeda robot", { app.call("Robot dijeda", { d -> app.repo.pause(d) }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
            RoboButton("Lanjutkan", { app.call("Robot dilanjutkan", { d -> app.repo.resume(d) }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
        }
        ButtonRow { // PD-2.5 / PD-3.3
            RoboButton("Tidurkan", { app.call("Robot ditidurkan", { d -> app.repo.rest(true, d) }, onOk = { app.refreshStatus() }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
            RoboButton("Bangunkan", { app.call("Robot dibangunkan", { d -> app.repo.rest(false, d) }, onOk = { app.refreshStatus() }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
            RoboButton("Elus", { app.call("Robot dielus", { d -> app.repo.remotePet(d) }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
        }
        Gap(12)
        RoboField("Pomodoro · ${st?.pomodoroText ?: "-"}") {
            fun pom(sub: Int, text: String) = app.call("Pomodoro: $text", { d -> app.repo.pomodoro(sub, d) }, onOk = { app.refreshStatus() })
            ButtonRow {
                RoboButton("Mulai", { pom(1, "mulai") }, Modifier.weight(1f), enabled = w && a.pomodoroStart)
                RoboButton("Jeda", { pom(2, "dijeda") }, Modifier.weight(1f), ButtonKind.SECONDARY, w && a.pomodoroPause)
                RoboButton("Lanjut", { pom(3, "lanjut") }, Modifier.weight(1f), ButtonKind.SECONDARY, w && a.pomodoroResume)
                RoboButton("Henti", { pom(4, "berhenti") }, Modifier.weight(1f), ButtonKind.DANGER, w && a.pomodoroStop)
            }
        }
    }
}
