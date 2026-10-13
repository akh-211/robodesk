package com.robodesk.phonebridge.ui

import com.robodesk.phonebridge.PrivacyState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Build
import androidx.compose.material.icons.outlined.DateRange
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material.icons.outlined.Lock
import androidx.compose.material.icons.outlined.Notifications
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material.icons.outlined.Send
import androidx.compose.material.icons.outlined.Share
import androidx.compose.material.icons.outlined.Star
import androidx.compose.material.icons.outlined.Warning
import androidx.compose.material3.Icon
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.ColumnScope
import com.robodesk.phonebridge.R
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.BridgeSettingsProtocol as P
import com.robodesk.phonebridge.LiveAvailability
import com.robodesk.phonebridge.OtaResult
import com.robodesk.phonebridge.PhoneBridgeStatus
import com.robodesk.phonebridge.Res
import kotlinx.coroutines.delay

/** PD-4.2: every sub-screen, reachable from any tab (back returns to the tab or the parent hub). */
@Composable
fun SubScreen(sub: String) {
    when (sub) {
        "act" -> ActivityScreen()
        "int" -> IntegrationsScreen()
        "ir" -> IrScreen()
        "snd" -> SoundScreen()
        "rem" -> ReminderScreen()
        "net" -> NetScreen()
        "help" -> MenuScreen(helpMenu())
        "priv" -> PrivacyScreen()
        "diag" -> DiagnosticsScreen()
        "upd" -> UpdateScreen()
        "dng" -> DangerScreen()
        else -> AboutScreen()
    }
}

class MenuItem(val key: String, val icon: ImageVector, val title: String, val subtitle: String)

/** Tab "Main & Alat". */
@Composable
fun PlayScreen() = MenuScreen(listOf(
    MenuItem("act", Icons.Outlined.Star, stringResource(R.string.sub_activities), stringResource(R.string.play_activities_sub)),
    MenuItem("rem", Icons.Outlined.DateRange, stringResource(R.string.sub_reminders), stringResource(R.string.play_reminders_sub)),
    MenuItem("ir", Icons.Outlined.Send, stringResource(R.string.sub_ir), stringResource(R.string.play_ir_sub)),
    MenuItem("int", Icons.Outlined.Share, stringResource(R.string.sub_integrations), stringResource(R.string.play_integrations_sub)),
    MenuItem("snd", Icons.Outlined.Notifications, stringResource(R.string.sub_sound), stringResource(R.string.play_sound_sub))))

@Composable
private fun helpMenu() = listOf(
    MenuItem("diag", Icons.Outlined.Build, stringResource(R.string.sub_diagnostics), stringResource(R.string.help_diag_sub)),
    MenuItem("abt", Icons.Outlined.Info, stringResource(R.string.sub_about), ""))

/** Settings hub rows above the editable groups. */
@Composable
fun settingsMenu(installed: String, latest: String?) = listOf(
    MenuItem("net", Icons.Outlined.Lock, stringResource(R.string.sub_network), stringResource(R.string.settings_network_sub)),
    MenuItem("upd", Icons.Outlined.Refresh, stringResource(R.string.sub_update), stringResource(R.string.settings_update_sub, installed) + (latest?.let { " · " + stringResource(R.string.settings_update_available, it) } ?: "")),
    MenuItem("priv", Icons.Outlined.Lock, stringResource(R.string.sub_privacy), stringResource(R.string.settings_privacy_sub)),
    MenuItem("help", Icons.Outlined.Info, stringResource(R.string.sub_help), stringResource(R.string.settings_help_sub)),
    MenuItem("dng", Icons.Outlined.Warning, stringResource(R.string.sub_danger), stringResource(R.string.settings_danger_sub)))

@Composable
private fun MenuScreen(items: List<MenuItem>) = ScreenColumn { MenuCard(items) }

/** PD-4.14 Setelan > Privasi: the same mic/cloud wording as the Robot tab, consent and full data wipe. */
@Composable
private fun PrivacyScreen() {
    val app = RoboApp
    val d = app.draft
    val w = app.canWrite
    LaunchedEffect(app.ready) { if (app.ready && d == null) app.loadSettings(); app.refreshStatus() }
    val consent = d?.let { it.int("cloudConsent") == 1 }
    ScreenColumn {
        RoboCard(stringResource(R.string.privacy_now)) {
            InfoRow(stringResource(R.string.privacy_mic), PrivacyState.mic(d?.bool("privacyMicMuted") == true))
            InfoRow(stringResource(R.string.privacy_cloud), PrivacyState.cloud(app.status?.geminiKey, consent), last = true)
        }
        RoboCard {
            SwitchRow(stringResource(R.string.privacy_mute), d?.bool("privacyMicMuted") == true, { v -> app.toggle("privacyMicMuted", v) }, enabled = w && d != null)
            SwitchRow(stringResource(R.string.privacy_metrics), d?.bool("interactionMetrics") == true, { v -> app.toggle("interactionMetrics", v) }, enabled = w && d != null)
            SwitchRow(stringResource(R.string.privacy_consent), consent == true, { v -> if (v) app.sheet = SheetKind.CLOUD_CONSENT else app.setNow("cloudConsent", 0) }, enabled = w && d != null, last = true)
        }
        Note(PrivacyState.CONSENT_TEXT)
        Gap(12)
        RoboButton(stringResource(R.string.privacy_wipe), { app.sheet = SheetKind.WIPE_DATA }, Modifier.fillMaxWidth(), ButtonKind.DANGER, w)
    }
}

@Composable
fun ColumnScope.MenuCard(items: List<MenuItem>) {
    val app = RoboApp
    run {
        val shape = RoundedCornerShape(16.dp)
        Column(Modifier.fillMaxWidth().clip(shape).background(RoboColors.Card).border(1.dp, RoboColors.Line, shape)) {
            items.forEachIndexed { i, item ->
                Row(Modifier.fillMaxWidth().heightIn(min = 56.dp).clickable { app.openSub(item.key) }.padding(14.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Icon(item.icon, contentDescription = null, tint = RoboColors.Mute, modifier = Modifier.width(28.dp))
                    Column(Modifier.weight(1f)) {
                        Text(item.title, color = RoboColors.Text, fontSize = 15.sp)
                        if (item.subtitle.isNotEmpty()) Text(item.subtitle, color = RoboColors.Mute, fontSize = 12.5.sp)
                    }
                    Text("›", color = RoboColors.Mute, fontSize = 18.sp)
                }
                if (i < items.lastIndex) Box(Modifier.fillMaxWidth().height(1.dp).background(RoboColors.Line))
            }
        }
    }
}

// ===================================================================== A14 Aktivitas

private val ACTIVITIES = listOf("Curious look · lirik penasaran", "Expression practice · latihan ekspresi", "Rhythm play · main ritme", "Daydream · melamun",
    "Stretch reset · peregangan", "Rest · istirahat", "Quiet company · menemani tenang", "Pixel doodle · coret piksel", "Watch room · mengamati ruangan",
    "Touch play · main sentuh", "Rhythm improv · improvisasi ritme", "Focus company · menemani fokus", "Calm breathing · napas tenang")

@Composable
private fun ActivityScreen() {
    val app = RoboApp
    val w = app.canWrite
    var chosen by remember { mutableStateOf(0) }
    val a = LiveAvailability(app.status?.s)
    fun act(sub: Int, done: String) = app.call(done, { d -> app.repo.activity(sub, if (sub == 1) chosen + 1 else 0, d) }, onOk = { app.refreshStatus() })
    ScreenColumn {
        RoboCard("Aktivitas robot") {
            SelectBox(ACTIVITIES, chosen, { chosen = it }, enabled = w)
            Note(when { a.activityRunning -> "Aktivitas berjalan"; a.activityPaused -> "Aktivitas dijeda"; else -> "Tidak ada aktivitas aktif" })
            Gap(8)
            ButtonRow {
                RoboButton("Mulai", { act(1, "Aktivitas dimulai") }, Modifier.weight(1f), enabled = w)
                RoboButton("Jeda", { act(2, "Aktivitas dijeda") }, Modifier.weight(1f), ButtonKind.SECONDARY, w && a.activityRunning)
                RoboButton("Lanjut", { act(3, "Aktivitas dilanjutkan") }, Modifier.weight(1f), ButtonKind.SECONDARY, w && a.activityPaused)
            }
            Gap(8)
            ButtonRow {
                RoboButton("Lewati", { act(5, "Aktivitas dilewati") }, Modifier.weight(1f), ButtonKind.SECONDARY, w && a.activityAny)
                RoboButton("★ Favorit", { act(6, "Ditandai favorit") }, Modifier.weight(1f), ButtonKind.SECONDARY, w && a.activityAny)
                RoboButton("Batalkan", { act(4, "Aktivitas dibatalkan") }, Modifier.weight(1f), ButtonKind.DANGER, w && a.activityAny)
            }
            Gap(8)
            RoboButton("Hapus riwayat aktivitas", { act(7, "Riwayat aktivitas dihapus") }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY, w)
        }
        RoboCard("Permainan sentuh") {
            Note("Saat robot mengajak bermain, tombol aktif.")
            Gap(8)
            ButtonRow {
                RoboButton("Terima", { app.call("Permainan diterima", { d -> app.repo.game(1, d) }, onOk = { app.refreshStatus() }) }, Modifier.weight(1f), enabled = w && a.gameAccept)
                RoboButton("Batalkan", { app.call("Permainan dibatalkan", { d -> app.repo.game(2, d) }, onOk = { app.refreshStatus() }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w && a.gameCancel)
            }
        }
        RoboCard("Pembelajaran preferensi") {
            val on = app.status?.s?.learningEnabled == true
            SwitchRow("Robot belajar dari kebiasaan", on, { v ->
                app.call("Pembelajaran ${if (v) "aktif" else "nonaktif"}", { d -> app.repo.learning(if (v) 1 else 0, d) }, onOk = { app.refreshStatus() })
            }, "learning_enable", enabled = w && app.status != null, last = true)
            Gap(10)
            RoboButton("Atur ulang pembelajaran", { app.sheet = SheetKind.RESET_LEARNING }, Modifier.fillMaxWidth(), ButtonKind.DANGER, w)
        }
    }
}

// ===================================================================== A15 Integrasi

@Composable
private fun IntegrationsScreen() {
    val app = RoboApp
    val w = app.canWrite
    var loaded by remember { mutableStateOf(false) }
    var base by remember { mutableStateOf("") }
    var tokenSet by remember { mutableStateOf(false) }
    var calendarSet by remember { mutableStateOf(false) }
    var token by remember { mutableStateOf("") }
    var calendar by remember { mutableStateOf("") }
    var clearCalendar by remember { mutableStateOf(false) }
    val entities = remember { mutableStateOf(listOf("", "", "")) }
    var weather by remember { mutableStateOf(false) }
    var lat by remember { mutableStateOf("0") }
    var lon by remember { mutableStateOf("0") }
    var providers by remember { mutableStateOf<P.IntegrationsStatus?>(null) }

    fun reload() {
        app.call<P.IntegrationsConfig>(start = { d -> app.repo.loadIntegrations(d) }, onOk = { c ->
            base = c.haBase; tokenSet = c.tokenConfigured; calendarSet = c.calendarConfigured; weather = c.weatherEnabled
            lat = c.latitude.toString(); lon = c.longitude.toString()
            entities.value = List(3) { c.entities.getOrElse(it) { "" } }; loaded = true; clearCalendar = false
        })
        app.call<P.IntegrationsStatus>(start = { d -> app.repo.integrationsStatus(d) }, onOk = { providers = it })
    }
    LaunchedEffect(app.ready) { if (app.ready) reload() }

    ScreenColumn {
        RoboCard("Home Assistant") {
            RoboField("Alamat dasar (maks ${P.INT_BASE_MAX})") { TextFieldRobo(base, { base = it.trim() }, placeholder = "https://ha.local:8123", enabled = w, maxLength = P.INT_BASE_MAX) }
            RoboField("Token akses" + if (tokenSet) " (terpasang)" else "") {
                PasswordField(token, { token = it }, placeholder = "Kosong = pertahankan · tulis-saja (maks ${P.INT_TOKEN_MAX})", enabled = w, maxLength = P.INT_TOKEN_MAX)
            }
            RoboField("Entitas (maks 3 · ${P.INT_ENTITY_MAX} karakter)") {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    for (i in 0..2) TextFieldRobo(entities.value[i], { v -> entities.value = entities.value.toMutableList().also { it[i] = v.trim() } }, placeholder = "entity_id", enabled = w, maxLength = P.INT_ENTITY_MAX)
                }
            }
        }
        RoboCard("Cuaca") {
            SwitchRow("Cuaca aktif", weather, { weather = it }, enabled = w, last = true)
            Gap(10)
            ButtonRow {
                Column(Modifier.weight(1f)) { RoboField("Lintang") { TextFieldRobo(lat, { lat = it }, enabled = w, keyboardType = KeyboardType.Decimal) } }
                Column(Modifier.weight(1f)) { RoboField("Bujur") { TextFieldRobo(lon, { lon = it }, enabled = w, keyboardType = KeyboardType.Decimal) } }
            }
        }
        RoboCard("Kalender") {
            RoboField("URL ICS https (maks ${P.INT_CALENDAR_MAX})" + if (calendarSet && !clearCalendar) " (terpasang)" else "") {
                PasswordField(calendar, { calendar = it }, placeholder = "https://…/basic.ics", enabled = w, maxLength = P.INT_CALENDAR_MAX)
            }
            RoboButton(if (clearCalendar) "Kalender dihapus saat Simpan" else "Hapus kalender", { clearCalendar = true; calendar = ""; app.say("Kalender dihapus saat Simpan") },
                Modifier.fillMaxWidth(), ButtonKind.DANGER, w && (calendarSet || calendar.isNotEmpty()))
        }
        RoboCard("Status penyedia") {
            val p = providers
            if (p == null) Note(if (loaded) "Belum ada data." else "Memuat…") else {
                InfoRow("Cuaca", if (!p.weatherConfigured) "tidak diatur" else if (p.weatherValid) p.weather.ifEmpty { "ok" } else "belum ada data")
                p.entities.forEach { e -> InfoRow(entities.value.getOrNull(e.slot)?.ifEmpty { "Entitas ${e.slot + 1}" } ?: "Entitas", if (e.valid) e.state else "belum ada data") }
                InfoRow("Kalender", if (!p.calendarConfigured) "tidak diatur" else if (p.calendarValid) p.calendar.ifEmpty { "ok" } else "belum ada data")
                InfoRow("Terakhir diperbarui", if (p.weatherAgeMs in 1 until 0xFFFFFFFFL) "${p.weatherAgeMs / 60000} mnt lalu" else "-", last = true)
                if (p.busy) Note("Robot sedang memproses permintaan penyedia.")
            }
            Gap(12)
            ButtonRow {
                RoboButton("Simpan", {
                    val la = lat.replace(',', '.').toFloatOrNull(); val lo = lon.replace(',', '.').toFloatOrNull()
                    if (la == null || lo == null) { app.say("Lintang/bujur tidak valid"); return@RoboButton }
                    val t = token; val c = calendar
                    token = ""; calendar = "" // write-only: never kept after sending
                    app.call("Tersimpan di C3 · menyegarkan…", { d ->
                        app.repo.saveIntegrations(weather, la, lo, base, entities.value.filter { it.isNotBlank() }, t, c, clearCalendar, d)
                    }, onOk = { app.call<Unit>(start = { d -> app.repo.refreshIntegrations(d) }); reload() })
                }, Modifier.weight(1f), enabled = w)
                RoboButton("Segarkan", { app.call("Segarkan diantrekan", { d -> app.repo.refreshIntegrations(d) }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
                RoboButton("Hapus semua", { app.sheet = SheetKind.CLEAR_INTEGRATIONS }, Modifier.weight(1f), ButtonKind.DANGER, w)
            }
            Note("Permintaan penyedia berjalan satu per satu; jika sibuk, coba lagi sebentar. Token dan URL kalender tidak pernah ditampilkan ulang.")
        }
    }
}

// ===================================================================== A16 IR

@Composable
private fun IrScreen() {
    val app = RoboApp
    val w = app.canWrite
    var hex by remember { mutableStateOf("") }
    var profiles by remember { mutableStateOf<List<String>>(emptyList()) }
    var picked by remember { mutableStateOf(0) }
    var name by remember { mutableStateOf("last") }
    var countdown by remember { mutableStateOf(0) }
    var note by remember { mutableStateOf("Arahkan remote ke sensor robot lalu tekan tombol saat hitung mundur berjalan.") }
    fun reload() = app.call<List<String>>(start = { d -> app.repo.irProfiles(d) }, onOk = { profiles = it; picked = picked.coerceIn(0, (it.size - 1).coerceAtLeast(0)) })
    LaunchedEffect(app.ready) { if (app.ready) reload() }
    LaunchedEffect(countdown) {
        if (countdown > 0) {
            delay(1000)
            if (countdown == 1) {
                app.refreshStatus { v ->
                    note = if (v.s.irLearnState == 2) "Selesai · profil disimpan sebagai \"$name\"" else "Tidak ada sinyal diterima · coba lagi"
                    reload()
                }
            }
            countdown -= 1
            if (countdown > 0) note = "Mendengarkan remote… $countdown dtk"
        }
    }
    ScreenColumn {
        RoboCard("Kirim kode NEC") {
            RoboField("Kode hex (8 digit)") { TextFieldRobo(hex, { hex = it.uppercase().filter { c -> c in '0'..'9' || c in 'A'..'F' } }, placeholder = "20DF10EF", enabled = w, maxLength = 8) }
            RoboButton("Kirim NEC", { app.call("NEC dikirim", { d -> app.repo.irSend(hex, d) }) }, Modifier.fillMaxWidth(), enabled = w && hex.length == 8)
        }
        RoboCard("Profil terpelajar (maks 4)") {
            RoboField("Profil tersimpan") {
                if (profiles.isEmpty()) Note("Belum ada profil.") else SelectBox(profiles, picked, { picked = it; name = profiles[it] }, enabled = w)
            }
            RoboField("Nama profil (maks 15 · huruf, angka, spasi, - _)") { TextFieldRobo(name, { name = it }, enabled = w, maxLength = 15) }
            ButtonRow {
                RoboButton("Pelajari 10 detik", {
                    app.call("Mendengarkan remote…", { d -> app.repo.irLearn(name, d) }, onOk = { countdown = 10; note = "Mendengarkan remote… 10 dtk" })
                }, Modifier.weight(1f), enabled = w && countdown == 0 && profiles.size < 4 || (w && countdown == 0 && name in profiles))
                RoboButton("Putar profil", { app.call("Profil diputar", { d -> app.repo.irReplay(profiles.getOrElse(picked) { name }, d) }) }, Modifier.weight(1f), ButtonKind.SECONDARY, w && profiles.isNotEmpty())
            }
            Note(note)
        }
    }
}

// ===================================================================== A17 Tes suara

@Composable
private fun SoundScreen() {
    val app = RoboApp
    ScreenColumn {
        RoboCard("Tes suara karakter") {
            P.SOUND_CUES.chunked(3).forEachIndexed { row, cues ->
                Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    cues.forEachIndexed { col, cue ->
                        RoboButton(cue, { app.call("Suara: $cue", { d -> app.repo.soundTest(row * 3 + col, d) }) }, Modifier.weight(1f), ButtonKind.SECONDARY, app.canWrite)
                    }
                }
            }
            Note("Memutar suara di speaker robot dengan volume setelan saat ini.")
        }
    }
}

// ===================================================================== A9 Diagnostik, Update, Bahaya, Tentang

@Composable
private fun DiagnosticsScreen() {
    val app = RoboApp
    val v = app.status
    val s = v?.s
    LaunchedEffect(app.ready) { app.refreshStatus() }
    fun kib(x: Long) = "${x / 1024}"
    val local by produceState(PhoneBridgeStatus.state) { while (true) { value = PhoneBridgeStatus.state; delay(1000) } }
    ScreenColumn {
        RoboCard("Diagnostik") {
            InfoRow("Link retry / CRC / putus", if (s == null) "-" else "${s.linkRetries} / ${s.linkCrcErrors} / ${s.linkDisconnects}")
            InfoRow("Umur frame terakhir", if (s == null) "-" else "${s.linkAgeMs} ms")
            InfoRow("Heap C3 min / blok terbesar", if (s == null) "-" else "${kib(s.c3HeapMin)} / ${kib(s.c3LargestBlock)} KiB")
            InfoRow("Heap S3 min", if (s == null || !s.s3Valid) "-" else "${kib(s.s3HeapMin)} KiB")
            InfoRow("BLE", if (s == null) "-" else listOf(if (s.bleFlags and 1 != 0) "siap" else "belum siap", if (s.bleFlags and 2 != 0) "terhubung" else "tidak terhubung", if (s.bleFlags and 8 != 0) "terpasang" else "belum terpasang").joinToString(" · "), last = true)
            Gap(10)
            RoboButton("Segarkan", { app.refreshStatus(); app.say("Status diperbarui") }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY, app.ready)
        }
        RoboCard("Sambungan HP") {
            InfoRow("Fase", app.link.phase.name.lowercase() + if (app.link.attempt > 0) " (percobaan ${app.link.attempt})" else "")
            InfoRow("Sinyal", local.rssiDbm?.let { "$it dBm" } ?: "tidak tersedia", sublabel = "MTU ${local.mtu}")
            InfoRow("Sambung ulang", local.reconnectCount.toString())
            InfoRow("Antrean notifikasi", "${local.queuedNotifications} · tertua ${local.queueOldestAgeMs / 1000}s · dibuang ${local.droppedCount}", last = true)
            Gap(10)
            RoboButton("Hubungkan ulang sekarang", {
                val retry = PhoneBridgeStatus.retryConnection
                if (retry != null) retry() else app.say("Aktifkan akses notifikasi untuk menjalankan layanan companion.")
            }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY)
            Note("Pesan ke robot diautentikasi dan dilindungi dari pemutaran ulang. Data kepercayaan dienkripsi Android Keystore dan tidak ikut backup.")
        }
    }
}

@Composable
private fun UpdateScreen() {
    val app = RoboApp
    val v = app.status
    var elapsed by remember { mutableStateOf(0) }
    LaunchedEffect(app.otaStartedAt) {
        elapsed = 0
        while (app.otaStartedAt != 0L && app.otaResult == null) { delay(1000); elapsed++ }
    }
    LaunchedEffect(app.ready) { app.refreshStatus() }
    ScreenColumn {
        val result = app.otaResult
        when {
            result != null -> RoboCard("Hasil update") {
                val tag = when (result.kind) { OtaResult.Kind.DONE -> "Selesai" to TagKind.OK; OtaResult.Kind.UP_TO_DATE -> "Sudah terbaru" to TagKind.OK; OtaResult.Kind.FAILED -> "Gagal" to TagKind.WARN; else -> "-" to TagKind.NEUTRAL }
                RoboRow("Status") { Tag(tag.first, tag.second) }
                InfoRow("Keterangan", result.message)
                InfoRow("Terpasang sekarang", v?.c3Version ?: "-", last = true)
                Gap(10)
                RoboButton("Tutup", { app.dismissOta() }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY)
            }
            app.otaStartedAt != 0L -> RoboCard("Robot sedang memperbarui") {
                Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator(color = RoboColors.Accent, modifier = Modifier.padding(16.dp))
                    val stage = v?.let { app.repo.otaResult(it.s) }?.takeIf { it.kind == OtaResult.Kind.RUNNING }?.message
                    Text(stage ?: if (app.ready) "Mengunduh & memasang…" else "Robot menyala ulang · menyambung ulang…", color = RoboColors.Text, fontWeight = FontWeight.SemiBold)
                    ValueText("$elapsed dtk")
                }
                Note("Koneksi BLE terputus selama update — ini normal. Jangan matikan robot. Aplikasi menyambung ulang otomatis lalu menampilkan hasil. Tidak ada persen langsung (BLE mati selama update).")
            }
            else -> RoboCard("Pembaruan firmware (OTA bertanda tangan)") {
                InfoRow("Terpasang", v?.let { "C3 ${it.c3Version} / S3 ${it.s3Version}" } ?: "-")
                RoboRow("Tersedia", "dicek HP dari GitHub Releases", last = true) {
                    Text(app.latestVersion ?: "-", color = if (app.latestNewer) RoboColors.Accent else RoboColors.Mute, fontSize = 13.sp)
                }
                Gap(8)
                RoboButton("Cek versi terbaru", { app.checkLatest() }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY)
                Gap(8)
                RoboButton("Mulai update", { app.sheet = SheetKind.OTA }, Modifier.fillMaxWidth(), enabled = app.canWrite)
                Note("Robot mengunduh lewat Wi-Fi-nya sendiri dan memverifikasi tanda tangan. Selama update koneksi BLE putus (normal); hasil dibaca setelah robot menyala ulang. Info versi di HP hanya informasi.")
            }
        }
    }
}

@Composable
private fun DangerScreen() {
    val app = RoboApp
    ScreenColumn {
        RoboCard("Zona bahaya") {
            ButtonRow {
                RoboButton("Reboot", { app.sheetArg = 0; app.sheet = SheetKind.REBOOT }, Modifier.weight(1f), ButtonKind.SECONDARY, app.canWrite)
                RoboButton("Reset pabrik", { app.sheet = SheetKind.WIPE }, Modifier.weight(1f), ButtonKind.DANGER, app.canWrite)
            }
            Note("Reset pabrik menghapus setelan, bond, dan rahasia; setelahnya aplikasi kembali ke onboarding.")
        }
    }
}

@Composable
private fun AboutScreen() {
    val context = LocalContext.current
    val version = remember { try { context.packageManager.getPackageInfo(context.packageName, 0).versionName } catch (_: Exception) { "?" } }
    ScreenColumn {
        RoboCard("Tentang") {
            InfoRow("Aplikasi", "Bridge $version")
            InfoRow("Protokol BLE", "Settings v1 · schema 3")
            InfoRow("Dashboard web", "tidak dipakai", last = true)
        }
    }
}
