package com.robodesk.phonebridge.ui

import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.sp

@Composable
private fun SheetTitle(text: String) = Text(text, color = RoboColors.Text, fontSize = 17.sp, fontWeight = FontWeight.SemiBold)

@Composable
private fun ColumnScope.Confirm(title: String, body: String, action: String, danger: Boolean = true, enabled: Boolean = true, onConfirm: () -> Unit) {
    val app = RoboApp
    SheetTitle(title)
    Note(body)
    Gap(14)
    ButtonRow {
        RoboButton("Batal", { app.sheet = null }, Modifier.weight(1f), ButtonKind.SECONDARY)
        RoboButton(action, { app.sheet = null; onConfirm() }, Modifier.weight(1f), if (danger) ButtonKind.DANGER else ButtonKind.PRIMARY, enabled)
    }
}

/** The mockup's `openSheet` bodies, one per [SheetKind]. Secrets typed here are wiped (field cleared) right after sending. */
@Composable
fun ColumnScope.SheetContent() {
    val app = RoboApp
    val kind = app.sheet ?: return
    when (kind) {
        SheetKind.REBOOT -> {
            var s3 by remember { mutableStateOf(true) }
            var c3 by remember { mutableStateOf(false) }
            SheetTitle("Reboot robot")
            Note("Pilih target. S3 restart ±1 dtk, C3 ±1,5 dtk.")
            SwitchRow("Robot (S3)", s3, { s3 = it })
            SwitchRow("Gateway (C3) — BLE akan putus", c3, { c3 = it }, last = true)
            Gap(14)
            ButtonRow {
                RoboButton("Batal", { app.sheet = null }, Modifier.weight(1f), ButtonKind.SECONDARY)
                RoboButton("Reboot", { app.sheet = null; app.call("Reboot dijadwalkan", { d -> app.repo.reboot(s3, c3, d) }) }, Modifier.weight(1f), enabled = (s3 || c3) && app.canWrite)
            }
        }
        SheetKind.WIFI -> {
            val slot = app.sheetArg
            var ssid by remember { mutableStateOf("") }
            var password by remember { mutableStateOf("") }
            var show by remember { mutableStateOf(false) }
            var keep by remember { mutableStateOf(false) }
            SheetTitle("Wi-Fi slot ${slot + 1}")
            RoboField("SSID (maks 32)") { TextFieldRobo(ssid, { ssid = it }, maxLength = 32) }
            RoboField("Sandi (maks 64, kosong = jaringan terbuka)") {
                TextFieldRobo(password, { password = it }, enabled = !keep, maxLength = 64, visual = if (show) VisualTransformation.None else PasswordVisualTransformation())
            }
            SwitchRow("Tampilkan sandi", show, { show = it })
            SwitchRow("Pertahankan sandi lama", keep, { keep = it; if (it) password = "" }, last = true)
            Gap(14)
            ButtonRow {
                RoboButton("Kosongkan", { app.sheet = null; app.call("Slot dikosongkan", { d -> app.repo.clearWifi(slot, d) }, onOk = { app.refreshStatus() }) }, Modifier.weight(1f), ButtonKind.DANGER, app.canWrite)
                RoboButton("Kirim", {
                    val s = ssid; val p = password; val k = keep
                    ssid = ""; password = ""; app.sheet = null
                    app.call("Terkirim · reboot C3 untuk menerapkan", { d -> app.repo.sendWifi(slot, s, p, k, d) }, onOk = { app.refreshStatus() })
                }, Modifier.weight(1f), enabled = app.canWrite && ssid.isNotEmpty())
            }
        }
        SheetKind.CLEAR_MEMORY -> Confirm("Hapus semua memori?", "Semua fakta di robot dihapus. Tidak bisa dibatalkan.", "Hapus semua", enabled = app.canWrite) {
            app.call("Memori dihapus", { d -> app.repo.clearMemory(d) }, onOk = { app.clearMemoryCache() })
        }
        SheetKind.CLEAR_EXPERIENCES -> Confirm("Hapus pengalaman?", "Ringkasan percakapan yang disimpan robot dihapus. Fakta memori tidak ikut terhapus.", "Hapus", enabled = app.canWrite) {
            app.call("Pengalaman dihapus", { d -> app.repo.experiencesClear(d) })
        }
        SheetKind.OTA -> Confirm("Mulai update firmware?", "Robot mengunduh rilis terbaru lewat Wi-Fi dan memasangnya (S3 dulu, lalu C3). Koneksi BLE akan putus beberapa menit; jangan matikan robot.", "Mulai", danger = false, enabled = app.canWrite) {
            app.startOta()
        }
        SheetKind.FORGET -> Confirm("Lupakan robot ini?", "Robot mencabut kepercayaan pada HP ini lebih dulu, lalu bond dan kunci sesi di HP dihapus. Bila robot di luar jangkauan, HP tetap menghapus datanya tetapi robot masih mempercayai HP ini sampai dipasangkan ulang atau reset pabrik.", "Lupakan") {
            app.forgetRobot()
        }
        SheetKind.WIPE -> {
            var typed by remember { mutableStateOf("") }
            SheetTitle("Reset pabrik")
            Note("Menghapus setelan S3 & C3, kredensial HP, dan bond. Sekitar 5 dtk setelah perintah, robot reboot. Ketik WIPE untuk melanjutkan.")
            Gap(10)
            TextFieldRobo(typed, { typed = it }, placeholder = "WIPE", maxLength = 8)
            Gap(10)
            ButtonRow {
                RoboButton("Batal", { app.sheet = null }, Modifier.weight(1f), ButtonKind.SECONDARY)
                RoboButton("Reset", {
                    app.sheet = null
                    app.call("Reset pabrik dijadwalkan (5 dtk)", { d -> app.repo.factoryReset(typed, d) }, onOk = { app.afterFactoryReset() })
                }, Modifier.weight(1f), ButtonKind.DANGER, typed == "WIPE" && app.canWrite)
            }
        }
        SheetKind.CLEAR_INTEGRATIONS -> Confirm("Hapus semua integrasi?", "Alamat, token, entitas, lokasi cuaca, dan URL kalender dihapus dari robot.", "Hapus semua", enabled = app.canWrite) {
            app.call("Semua integrasi dihapus", { d -> app.repo.clearIntegrations(d) })
        }
        SheetKind.RESET_LEARNING -> Confirm("Atur ulang pembelajaran?", "Data preferensi yang dipelajari robot dihapus.", "Atur ulang", enabled = app.canWrite) {
            app.call("Pembelajaran diatur ulang", { d -> app.repo.learning(2, d) }, onOk = { app.refreshStatus() })
        }
    }
}
