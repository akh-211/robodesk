package com.robodesk.phonebridge.ui

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.provider.Settings
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CheckboxDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.AppContentMode
import com.robodesk.phonebridge.Allowlist
import com.robodesk.phonebridge.NotificationPrivacy
import com.robodesk.phonebridge.PhoneBridgeStatus
import com.robodesk.phonebridge.RobotSettings

class InstalledApp(val packageName: String, val label: String)

private fun installedApps(context: Context): List<InstalledApp> {
    val pm = context.packageManager
    val launcher = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
    return pm.queryIntentActivities(launcher, PackageManager.MATCH_DEFAULT_ONLY)
        .map { it.activityInfo.applicationInfo }
        .distinctBy { it.packageName }
        .filter { it.packageName != context.packageName }
        .map { InstalledApp(it.packageName, pm.getApplicationLabel(it).toString()) }
        .sortedBy { it.label.lowercase() }
}

private val CONTENT_MODES = listOf("Mati", "Nama app", "Judul", "Judul + cuplikan")

@Composable
private fun Check(label: String, checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = RoboColors.Mute, fontSize = 12.sp)
        Checkbox(checked, onChange, enabled = enabled, colors = CheckboxDefaults.colors(checkedColor = RoboColors.Accent, checkmarkColor = RoboColors.BtnPrimaryText, uncheckedColor = RoboColors.Mute))
    }
}

/**
 * "Notifikasi HP": Maps switch, global sharing pause, listener access, the two robot allowlists (Tampil / Baca, 192 B each)
 * and the per-app content mode kept on the phone. The phone can only narrow what the robot allows.
 */
@Composable
fun ColumnScope.NotifyControls(d: RobotSettings) {
    val app = RoboApp
    val context = LocalContext.current
    val w = app.canWrite
    val apps = remember { installedApps(context) }
    val prefs = context.getSharedPreferences("bridge", Context.MODE_PRIVATE)
    var paused by remember { mutableStateOf(prefs.getBoolean("paused", false)) }
    val listenerOn = Settings.Secure.getString(context.contentResolver, "enabled_notification_listeners")?.contains(context.packageName) == true
    val state = Allowlist.from(d)
    val installedPackages = apps.map { it.packageName }.toSet()
    val kept = (state.show + state.speak).filter { it !in installedPackages }

    SwitchRow("Navigasi Maps di layar robot", d.bool("navigationEnabled"), { v ->
        app.toggle("navigationEnabled", v) { saved -> prefs.edit().putBoolean("navigation", saved).apply() }
    }, "navigationEnabled", enabled = w)
    SwitchRow("Jeda semua berbagi", paused, { checked ->
        paused = checked
        prefs.edit().putBoolean("paused", checked).commit()
        PhoneBridgeStatus.pauseSharing?.invoke(checked)
        PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(sharingPaused = checked)
        app.say(if (checked) "Berbagi dijeda. Antrean dihapus." else "Berbagi dilanjutkan; pengaturan tiap aplikasi berlaku.")
    }, "Antrean dihapus, robot tidak menerima apa pun", enabled = true)
    RoboRow("Akses notifikasi HP", if (listenerOn) "Aktif" else "Mati: tanpa ini tidak ada notifikasi yang bisa dikirim") {
        if (!listenerOn) RoboButton("Aktifkan", { RoboApp.sheet = SheetKind.NOTIF_ACCESS }, kind = ButtonKind.SECONDARY) // PD-4.8 disclosure first
        else Tag("aktif", TagKind.OK)
    }
    RoboRow("Kapasitas daftar robot", "notificationAllowlist / SpeechAllowlist, maks 192 B per daftar") {
        Tag("Tampil ${state.label()} · Baca ${state.speak.size} app · ${state.speakBytes()}/${Allowlist.DISPLAY_LIMIT} B",
            if (state.showBytes() > 150 || state.speakBytes() > 150) TagKind.WARN else TagKind.NEUTRAL)
    }
    Note("Per aplikasi: Tampil di layar robot (notificationAllowlist) dan Baca lewat suara (notificationSpeechAllowlist). Daftar dari aplikasi terpasang di HP; isi disaring privasi di HP. Bila daftar penuh, penambahan ditolak (\"${Allowlist.FULL}\").")
    Gap(6)
    for (app0 in apps) {
        val blocked = Allowlist.privacyBlocked(app0.packageName)
        val showOn = app0.packageName in state.show
        if (blocked && !showOn) continue
        Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(app0.label, color = RoboColors.Text, fontSize = 14.sp, maxLines = 1, modifier = Modifier.weight(1f))
                Check("Tampil", showOn, w) { on ->
                    val msg = state.setShow(app0.packageName, on)
                    if (msg != null) app.say(msg) else {
                        app.edit { state.applyTo(it) }
                        if (on && NotificationPrivacy.mode(context, app0.packageName) == AppContentMode.OFF) setLocalMode(context, app0.packageName, AppContentMode.APP_ONLY)
                    }
                }
                Check("Baca", app0.packageName in state.speak, w) { on ->
                    val msg = state.setSpeak(app0.packageName, on)
                    if (msg != null) app.say(msg) else app.edit { state.applyTo(it) }
                }
            }
            if (showOn) {
                val current = NotificationPrivacy.mode(context, app0.packageName)
                var mode by remember(app0.packageName, current) { mutableStateOf(current.ordinal) }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Isi yang dikirim", color = RoboColors.Mute, fontSize = 11.5.sp, modifier = Modifier.weight(1f))
                    SelectBox(CONTENT_MODES, mode, { i -> mode = i; setLocalMode(context, app0.packageName, AppContentMode.entries[i]) }, Modifier.width(160.dp))
                }
            }
        }
    }
    RoboRow("Bank / dompet digital") { Tag("diblokir privasi", TagKind.WARN) }
    if (kept.isNotEmpty()) Note("Dipertahankan (tidak terpasang di HP): ${kept.size} paket")
    Note("Saat HP terkunci, isi otomatis dikurangi jadi nama aplikasi saja. Penyaringan berbasis pola bisa meleset; pilih Nama app untuk aplikasi pribadi.")
}

private fun setLocalMode(context: Context, pkg: String, mode: AppContentMode) {
    val changed = NotificationPrivacy.mode(context, pkg) != mode
    NotificationPrivacy.setMode(context, pkg, mode)
    if (changed) PhoneBridgeStatus.appModeChanged?.invoke(pkg, mode)
}
