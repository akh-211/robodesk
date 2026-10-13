package com.robodesk.phonebridge.ui

import android.Manifest
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.GeminiKeyHelp
import com.robodesk.phonebridge.LinkPhase
import com.robodesk.phonebridge.RobotScanner
import com.robodesk.phonebridge.ScannedRobot
import com.robodesk.phonebridge.TrustedRobotStore

/** Onboarding progress survives rotation (process-wide, like [RoboApp]). */
private object Onb {
    var step by mutableIntStateOf(0)
    var robots by mutableStateOf<List<ScannedRobot>>(emptyList())
    var selected by mutableStateOf<String?>(null)
    var scanError by mutableStateOf<String?>(null)
    var permTick by mutableIntStateOf(0)
}

private fun nearbyPermissions(): Array<String> =
    if (Build.VERSION.SDK_INT >= 31) arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)

@Composable
fun OnboardingScreen() {
    val app = RoboApp
    // A stored robot with a ready session means we re-entered mid-way: show "Terhubung".
    LaunchedEffect(Unit) { if (Onb.step == 0 && app.ready && app.hasRobot) Onb.step = 3 }
    Column(Modifier.fillMaxSize().padding(horizontal = 20.dp, vertical = 16.dp)) {
        Row(Modifier.fillMaxWidth().padding(top = 6.dp, bottom = 12.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            repeat(6) { i -> Box(Modifier.weight(1f).height(5.dp).clip(RoundedCornerShape(5.dp)).background(if (i <= Onb.step) RoboColors.Accent else RoboColors.Line)) }
        }
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState())) {
            when (Onb.step) {
                0 -> StepPermissions()
                1 -> StepScan()
                2 -> StepPairing()
                3 -> StepConnected()
                4 -> StepWifi()
                else -> StepGemini()
            }
        }
    }
}

@Composable
private fun Title(text: String, sub: String) {
    Text(text, color = RoboColors.Text, fontSize = 22.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 14.dp, bottom = 6.dp))
    Text(sub, color = RoboColors.Mute, fontSize = 14.sp)
}

@Composable
private fun Buttons(content: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit) =
    Row(Modifier.fillMaxWidth().padding(top = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp), content = content)

// ----------------------------------------------------------------------- 1 izin

@Composable
private fun StepPermissions() {
    val context = LocalContext.current
    val app = RoboApp
    @Suppress("UNUSED_VARIABLE") val recompute = Onb.permTick + app.resumeTick
    val nearby = nearbyPermissions().all { context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }
    val notif = Build.VERSION.SDK_INT < 33 || context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED
    val listener = Settings.Secure.getString(context.contentResolver, "enabled_notification_listeners")?.contains(context.packageName) == true
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { Onb.permTick++ }
    Text("📡", fontSize = 42.sp)
    Title("Hubungkan RoboDesk", "Aplikasi ini mengatur semua setelan robot lewat Bluetooth. Izin yang dibutuhkan:")
    Gap(14)
    RoboCard {
        RoboRow("Perangkat sekitar", "Mencari dan terhubung ke robot") { Tag(if (nearby) "diizinkan" else "diperlukan", if (nearby) TagKind.OK else TagKind.WARN) }
        RoboRow("Notifikasi", "Layanan koneksi latar belakang") { Tag(if (notif) "diizinkan" else "disarankan", if (notif) TagKind.OK else TagKind.WARN) }
        RoboRow("Akses notifikasi HP", "Meneruskan notifikasi terpilih ke robot", last = true) {
            if (listener) Tag("aktif", TagKind.OK) else {
                Tag("opsional", TagKind.WARN)
                RoboButton("Atur", { context.startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }, kind = ButtonKind.SECONDARY)
            }
        }
    }
    Buttons {
        RoboButton(if (nearby) "Mulai mencari" else "Izinkan & mulai mencari", {
            if (!nearby || !notif) {
                val wanted = nearbyPermissions().toMutableList()
                if (Build.VERSION.SDK_INT >= 33) wanted += Manifest.permission.POST_NOTIFICATIONS
                launcher.launch(wanted.toTypedArray())
            }
            if (nearby) Onb.step = 1
        }, Modifier.weight(1f))
    }
}

// ----------------------------------------------------------------------- 2 pindai

@Composable
private fun StepScan() {
    val context = LocalContext.current
    val scanner = remember { RobotScanner(context) }
    DisposableEffect(Unit) {
        Onb.scanError = null
        scanner.start({ Onb.robots = it; if (Onb.selected == null) it.firstOrNull { r -> r.pairingOpen == true }?.let { r -> Onb.selected = r.address } }, { Onb.scanError = it })
        onDispose { scanner.stop() }
    }
    Title("Memindai…", "Dekatkan HP ke robot dan pastikan robot menyala.")
    Gap(14)
    Onb.scanError?.let { Text(it, color = RoboColors.Bad, fontSize = 13.sp) }
    if (Onb.robots.isEmpty() && Onb.scanError == null) Text("Belum ada robot ditemukan…", color = RoboColors.Mute, fontSize = 13.sp)
    Onb.robots.forEach { r ->
        val sel = r.address == Onb.selected
        val shape = RoundedCornerShape(14.dp)
        Row(Modifier.fillMaxWidth().padding(bottom = 8.dp).clip(shape).background(RoboColors.Card).border(1.dp, if (sel) RoboColors.Accent else RoboColors.Line, shape)
            .clickable { Onb.selected = r.address }.padding(12.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("🤖", fontSize = 24.sp)
            Column(Modifier.weight(1f)) {
                Text(r.name, color = RoboColors.Text, fontWeight = FontWeight.SemiBold)
                Text(if (r.pairingOpen == true) "pairing terbuka" else if (r.bonded) "sudah dipasangkan" else "belum dipasangkan", color = if (r.pairingOpen == true) RoboColors.Accent else RoboColors.Mute, fontSize = 12.sp)
            }
            Text("${r.rssi} dBm", color = RoboColors.Mute, fontSize = 13.sp)
        }
    }
    if (Onb.robots.firstOrNull { it.address == Onb.selected }?.bonded == true) {
        Text("Robot ini masih tersimpan di Bluetooth HP. Bond lama membuat pairing gagal: buka Setelan Bluetooth, pilih Lupakan/Batalkan pasangan untuk RoboDesk (dan entri lama seperti keyboard), lalu ulangi.", color = RoboColors.Warn, fontSize = 13.sp)
        val btContext = LocalContext.current
        RoboButton("Buka Setelan Bluetooth", {
            try { btContext.startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) { }
        }, Modifier.padding(top = 8.dp), ButtonKind.SECONDARY)
    }
    Buttons {
        RoboButton("Kembali", { Onb.step = 0 }, Modifier.weight(1f), ButtonKind.SECONDARY)
        RoboButton("Pilih robot ini", {
            val address = Onb.selected ?: return@RoboButton
            // No createBond() here: the robot only accepts a bond from a connection made while its pairing window is open.
            // The next step's search calls createBond() once the robot is seen, then connects GATT over that link.
            // A bond left on the phone by an earlier attempt holds the robot's IRK while the robot advertises its public address:
            // the phone's controller then never matches the robot and every connect ends in GATT 147. Onboarding always pairs anew.
            if (Onb.robots.firstOrNull { it.address == address }?.bonded == true) TrustedRobotStore.removeBond(context, address)
            if (RoboApp.selectRobot(address)) Onb.step = 2
        }, Modifier.weight(1f), enabled = Onb.selected != null)
    }
}

// ----------------------------------------------------------------------- 3 jendela pairing

@Composable
private fun PairStepRow(index: Int, title: String, hint: String, state: Int, pulse: Float) {
    Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.Top) {
        val ring = when (state) { 2 -> RoboColors.Accent; 1 -> RoboColors.Accent.copy(alpha = pulse); else -> RoboColors.Line }
        Box(Modifier.size(28.dp).clip(CircleShape).background(if (state == 2) RoboColors.Accent else RoboColors.Card).border(2.dp, ring, CircleShape), contentAlignment = Alignment.Center) {
            Text(if (state == 2) "✓" else "$index", color = if (state == 2) RoboColors.Panel else if (state == 1) RoboColors.Accent else RoboColors.Mute, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }
        Column(Modifier.weight(1f)) {
            Text(title, color = if (state == 0) RoboColors.Mute else RoboColors.Text, fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
            if (state == 1) Text(hint, color = RoboColors.Mute, fontSize = 13.sp, modifier = Modifier.padding(top = 2.dp))
        }
    }
}

@Composable
private fun StepPairing() {
    val app = RoboApp
    val link = app.link
    val phase = link.phase
    val done = phase == LinkPhase.READY
    DisposableEffect(Unit) { app.enterPairingMode(true); onDispose { app.enterPairingMode(false) } }
    LaunchedEffect(phase) { if (phase == LinkPhase.READY) Onb.step = 3 }
    val pulse by rememberInfiniteTransition(label = "ring").animateFloat(0.35f, 1f, infiniteRepeatable(tween(800), RepeatMode.Reverse), label = "alpha")
    // Invisible developer switch: long-press the title to show the connection log (for support only).
    var showLog by remember { mutableStateOf(false) }
    Box(Modifier.pointerInput(Unit) { detectTapGestures(onLongPress = { showLog = !showLog }) }) {
        Title("Hubungkan robot", "Ikuti tiga langkah ini. Jendela pairing di robot terbuka selama 60 detik.")
    }
    Gap(16)
    val active = app.searching || done
    val step = if (done) 4 else link.pairStep.coerceIn(1, 3)
    fun state(i: Int) = if (!active) 0 else if (i < step) 2 else if (i == step) 1 else 0
    RoboCard {
        PairStepRow(1, "Buka pairing di robot", "Tahan sensor kepala robot sekitar 5 detik sampai layarnya menampilkan PAIRING TERBUKA.", state(1), pulse)
        PairStepRow(2, "Masukkan kode di HP", "Kode 6 angka muncul di layar robot. Ketik kode itu saat HP meminta pairing Bluetooth.", state(2), pulse)
        PairStepRow(3, "Menyiapkan koneksi aman", "Robot menyetujui HP ini otomatis. Mohon tunggu sebentar.", state(3), pulse)
    }
    Gap(12)
    val detail = link.detail
    val failed = phase == LinkPhase.ERROR || (detail != null && link.pairStep <= 1 && !done && detail.contains("gagal", true))
    Text(when {
        done -> "Terhubung ke robot."
        phase == LinkPhase.ERROR -> detail ?: link.cause ?: "Terjadi kesalahan saat menyambung."
        !app.searching -> "Pairing belum selesai dalam 2 menit. Tahan sensor kepala robot lagi, lalu tekan Coba lagi."
        else -> detail ?: "Menunggu robot membuka jendela pairing…"
    }, color = if (failed || phase == LinkPhase.ERROR) RoboColors.Bad else RoboColors.Mute, fontSize = 13.sp, modifier = Modifier.fillMaxWidth())
    if (showLog && app.diag.isNotEmpty()) {
        Gap(10)
        Text(app.diag, color = RoboColors.Mute, fontSize = 11.sp, lineHeight = 14.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.fillMaxWidth())
    }
    Buttons {
        RoboButton("Batal", { Onb.step = 1 }, Modifier.weight(1f), ButtonKind.SECONDARY)
        if (done) RoboButton("Lanjut", { Onb.step = 3 }, Modifier.weight(1f))
        else RoboButton("Coba lagi", { app.retryPairingSearch() }, Modifier.weight(1f), enabled = !app.searching || phase == LinkPhase.ERROR)
    }
}

// ----------------------------------------------------------------------- 4 terhubung

@Composable
private fun StepConnected() {
    val context = LocalContext.current
    val app = RoboApp
    val bonded = try {
        TrustedRobotStore.address(context)?.let { context.getSystemService(BluetoothManager::class.java)?.adapter?.getRemoteDevice(it)?.bondState == BluetoothDevice.BOND_BONDED } == true
    } catch (_: SecurityException) { false }
    val session = app.ready
    Text(if (bonded && session) "✅" else "⏳", fontSize = 42.sp, modifier = Modifier.padding(top = 30.dp))
    Title(if (bonded && session) "Terhubung" else "Menunggu verifikasi", "Bond aman dibuat dan sesi terautentikasi dimulai. Robot siap diatur.")
    Gap(14)
    RoboCard {
        RoboRow("Bond LESC") { Tag(if (bonded) "aktif" else "belum", if (bonded) TagKind.OK else TagKind.WARN) }
        RoboRow("Sesi HMAC", last = true) { Tag(if (session) "aktif" else "belum", if (session) TagKind.OK else TagKind.WARN) }
    }
    Buttons { RoboButton("Lanjut: atur Wi-Fi", { Onb.step = 4 }, Modifier.weight(1f), enabled = bonded && session) }
}

// ----------------------------------------------------------------------- 5 Wi-Fi

@Composable
private fun StepWifi() {
    val app = RoboApp
    var ssid by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    Title("Wi-Fi robot", "Robot butuh Wi-Fi untuk Gemini, jam, dan update. Sandi dikirim terenkripsi dan tidak bisa dibaca kembali.")
    Gap(14)
    RoboField("SSID (maks 32)") { TextFieldRobo(ssid, { ssid = it }, maxLength = 32) }
    RoboField("Sandi (maks 64, kosong = jaringan terbuka)") { PasswordField(password, { password = it }, maxLength = 64) }
    Buttons {
        RoboButton("Lewati", { Onb.step = 5 }, Modifier.weight(1f), ButtonKind.SECONDARY)
        RoboButton("Kirim", {
            val s = ssid; val p = password
            ssid = ""; password = ""
            app.call("Wi-Fi terkirim", { d -> app.repo.sendWifi(0, s, p, false, d) }, onOk = { Onb.step = 5 })
        }, Modifier.weight(1f), enabled = ssid.isNotEmpty() && app.ready)
    }
}

// ----------------------------------------------------------------------- 6 Gemini

@Composable
private fun StepGemini() {
    val app = RoboApp
    var key by remember { mutableStateOf("") }
    Title("Kunci API Gemini", "Dibutuhkan agar robot bisa bercakap. Boleh dilewati dan diatur nanti di tab Koneksi.")
    Gap(14)
    val ctx = LocalContext.current
    Note(GeminiKeyHelp.STEPS) // PD-4.16
    RoboButton("Buka halaman kunci", { runCatching { ctx.startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse(GeminiKeyHelp.KEY_PAGE))) } }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY)
    Gap(10)
    RoboField("Kunci") { PasswordField(key, { key = GeminiKeyHelp.normalize(it) }, placeholder = "AIza…", maxLength = 96) }
    val hint = if (key.isEmpty()) null else GeminiKeyHelp.check(key)
    if (hint != null) Note(hint.removePrefix("!"))
    Buttons {
        RoboButton("Lewati", { Onb.step = 0; app.finishOnboarding() }, Modifier.weight(1f), ButtonKind.SECONDARY)
        RoboButton("Kirim & selesai", {
            val k = key
            key = ""
            app.call("Kunci terkirim", { d -> app.repo.sendGeminiKey(k, d) }, onOk = { Onb.step = 0; app.finishOnboarding() })
        }, Modifier.weight(1f), enabled = key.isNotEmpty() && app.ready && hint?.startsWith("!") != true)
    }
}
