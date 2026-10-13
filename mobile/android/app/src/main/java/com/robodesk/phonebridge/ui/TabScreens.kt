package com.robodesk.phonebridge.ui

import android.app.DatePickerDialog
import android.app.TimePickerDialog
import android.os.Build
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.BridgeSettingsProtocol as P
import com.robodesk.phonebridge.GeminiKeyHelp
import com.robodesk.phonebridge.MoodDiary
import com.robodesk.phonebridge.PhoneBridgeStatus
import com.robodesk.phonebridge.Res
import java.util.Calendar
import java.util.Date

// ===================================================================== Koneksi

@Composable
fun NetScreen() {
    val app = RoboApp
    val st = app.status
    val w = app.canWrite
    LaunchedEffect(app.ready) { app.refreshStatus() }
    ScreenColumn {
        RoboCard("Wi-Fi robot (tulis-saja)") {
            Note("SSID & sandi tidak pernah dibaca balik dari robot. Tersimpan di NVS C3. Berlaku pada koneksi ulang berikutnya.")
            val names = listOf("Slot 1 · utama", "Slot 2 · cadangan", "Slot 3 · cadangan")
            for (i in 0..2) {
                val set = st?.slotConfigured(i + 1) == true
                RoboRow(names[i], if (st == null) "-" else if (set) "Terpasang" else "Kosong", last = i == 2) {
                    if (st?.s?.wifiActiveSlot == i + 1 && st.wifiConnected) Tag("terhubung", TagKind.OK)
                    RoboButton(if (set) "Ubah" else "Atur", { app.sheetArg = i; app.sheet = SheetKind.WIFI }, kind = ButtonKind.SECONDARY, enabled = w)
                }
            }
            Note("Isi SSID dan sandi manual di lembar \"Ubah\". Android tidak mengizinkan aplikasi membaca sandi Wi-Fi tersimpan.")
        }
        GeminiCard(st?.geminiKey, w)
        RoboCard("Pairing & keamanan") {
            RoboRow("HP tepercaya", "${Build.MODEL} · " + if (app.ready) "bond LESC aktif" else "belum ada sesi") { Tag(if (app.ready) "ini" else "-", if (app.ready) TagKind.OK else TagKind.NEUTRAL) }
            RoboRow("Jendela pairing", "Tahan sensor kepala 5 dtk di robot", last = true) {
                Tag(if (st?.pairingWindowOpen == true) "terbuka" else "tertutup", if (st?.pairingWindowOpen == true) TagKind.WARN else TagKind.NEUTRAL)
            }
            Gap(12)
            ButtonRow { RoboButton("Lupakan robot", { app.sheet = SheetKind.FORGET }, Modifier.weight(1f), ButtonKind.SECONDARY) }
            Note("PIN admin tidak dikirim lewat BLE; bond + sesi HMAC menggantikannya.")
        }
    }
}

@Composable
private fun ColumnScope.GeminiCard(installed: Boolean?, w: Boolean) {
    val app = RoboApp
    var key by remember { mutableStateOf("") }
    RoboCard("Kunci API Gemini") {
        RoboRow("Status", last = true) { Tag(if (installed == null) "-" else if (installed) "terpasang" else "belum ada", if (installed == true) TagKind.OK else TagKind.NEUTRAL) }
        Gap(10)
        val ctx = LocalContext.current
        Note(GeminiKeyHelp.STEPS) // PD-4.16
        RoboButton("Buka halaman kunci", { runCatching { ctx.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(GeminiKeyHelp.KEY_PAGE))) } }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY)
        Gap(10)
        RoboField("Kunci baru") { PasswordField(key, { key = GeminiKeyHelp.normalize(it) }, placeholder = "AIza…", enabled = w, maxLength = 96) }
        val hint = if (key.isEmpty()) null else GeminiKeyHelp.check(key)
        if (hint != null) Note(hint.removePrefix("!"))
        ButtonRow {
            RoboButton("Kirim kunci", {
                val value = key
                key = "" // never kept after sending
                app.call("Kunci dikirim lewat BLE terenkripsi", { d -> app.repo.sendGeminiKey(value, d) }, onOk = { app.refreshStatus() })
            }, Modifier.weight(1f), enabled = w && key.isNotEmpty() && hint?.startsWith("!") != true)
            RoboButton("Hapus kunci", { app.call("Kunci dihapus dari robot", { d -> app.repo.clearGeminiKey(d) }, onOk = { app.refreshStatus() }) }, Modifier.weight(1f), ButtonKind.DANGER, w)
        }
        Note("Kunci diteruskan C3 → S3 hanya di RAM. Nilai tidak pernah ditampilkan ulang.")
    }
}

// ===================================================================== Memori

private val CATEGORIES = listOf("note · catatan", "profile · profil", "preference · preferensi", "routine · rutinitas", "place · tempat", "event · kejadian")

@Composable
fun MemoryScreen() {
    val app = RoboApp
    val w = app.canWrite
    var key by remember { mutableStateOf("") }
    var value by remember { mutableStateOf("") }
    var category by remember { mutableStateOf(0) }
    var rejected by remember { mutableStateOf(false) }
    LaunchedEffect(app.ready) { app.loadMemory() }
    ScreenColumn {
        RoboCard("Fakta yang diingat robot (${app.facts.size}/${P.MAX_FACTS})") {
            if (app.facts.isEmpty()) Note(if (app.memLoading) "Memuat…" else "Belum ada memori.")
            app.facts.forEachIndexed { i, f ->
                Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalAlignment = Alignment.Top) {
                    Column(Modifier.weight(1f)) {
                        Text(f.value, color = RoboColors.Text, fontSize = 14.sp) // plain data, never interpreted
                        Text("${f.key} · ${CATEGORIES.getOrElse(f.category) { "?" }.substringBefore(' ')} · id ${f.id}", color = RoboColors.Mute, fontSize = 11.5.sp)
                    }
                    Text("✕", color = RoboColors.Bad, fontSize = 16.sp, modifier = Modifier.clickable(enabled = w) {
                        app.call("Fakta dihapus", { d -> app.repo.deleteMemory(f.id, d) }, onOk = { app.loadMemory() })
                    }.padding(6.dp))
                }
            }
            Gap(12)
            RoboField("Kunci (maks ${P.MAX_FACT_KEY})") { TextFieldRobo(key, { key = it }, placeholder = "mis. minuman_favorit", enabled = w, maxLength = P.MAX_FACT_KEY) }
            RoboField("Nilai (maks ${P.MAX_FACT_VALUE})") { TextFieldRobo(value, { value = it }, placeholder = "mis. Kopi tanpa gula", enabled = w, maxLength = P.MAX_FACT_VALUE) }
            RoboField("Kategori") { SelectBox(CATEGORIES, category, { category = it }, enabled = w) }
            if (rejected) Text("Ditolak filter privasi robot (kredensial / kesehatan / deret angka panjang). Filter bersifat konservatif, bukan jaminan.", color = RoboColors.Bad, fontSize = 12.sp)
            Gap(8)
            ButtonRow {
                RoboButton("Tambah", {
                    val k = key.trim(); val v = value.trim()
                    val replacing = app.facts.any { it.key == k }
                    rejected = false
                    app.call(if (replacing) "Fakta dengan kunci sama diganti" else "Ditambahkan", { d -> app.repo.addMemory(category, k, v, d) },
                        onOk = { key = ""; value = ""; app.loadMemory() },
                        onErr = { e -> rejected = e.status == P.Status.CONTENT_REJECTED })
                }, Modifier.weight(1f), enabled = w && key.isNotBlank() && value.isNotBlank())
                RoboButton("Hapus semua", { app.sheet = SheetKind.CLEAR_MEMORY }, Modifier.weight(1f), ButtonKind.DANGER, w)
            }
            Gap(8)
            RoboButton("Hapus pengalaman (ringkasan percakapan)", { app.sheet = SheetKind.CLEAR_EXPERIENCES }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY, w)
        }
        Note("Robot adalah sumber utama; daftar ditarik ulang bila revisi berubah. Teks memori = data, bukan instruksi.")
        Gap(12)
        RoboCard("Diary mood") { // PD-3.4
            LaunchedEffect(Unit) { app.loadMoodDiary() }
            SwitchRow("Catat mood per jam di HP ini", app.moodDiaryOn, { app.setMoodDiary(it) }, sublabel = "Hanya mood & status tidur, tanpa percakapan; terenkripsi, 7 hari")
            val offset = java.util.TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 1000L
            val fmt = java.text.SimpleDateFormat("EEE d MMM", java.util.Locale("id"))
            MoodDiary.days(app.moodDiary, offset).forEach { (day, text) ->
                Note("${fmt.format(java.util.Date(day * 86400000L - offset * 1000L + 43200000L))} · $text")
            }
            if (app.moodDiaryOn && app.moodDiary.isEmpty()) Note("Belum ada catatan; mood dicatat saat status robot dibaca.")
            Gap(8)
            RoboButton("Hapus diary", { app.clearMoodDiary(); app.say("Diary mood dihapus") }, Modifier.fillMaxWidth(), ButtonKind.DANGER, app.moodDiary.isNotEmpty())
        }
    }
}

// ===================================================================== Pengingat

private val REMINDER_KINDS = listOf("Sekali", "Harian", "Timer")

private fun describe(r: P.Reminder): String = when (r.schedule) {
    1 -> "Sekali · " + android.text.format.DateFormat.format("yyyy-MM-dd HH:mm", Date(r.due * 1000))
    2 -> "Harian · %02d:%02d".format(r.minute / 60, r.minute % 60)
    else -> "Timer · sisa " + (if (r.due >= 3600) "%d j %02d m".format(r.due / 3600, r.due % 3600 / 60) else "%d m %02d d".format(r.due / 60, r.due % 60))
}

@Composable
fun ReminderScreen() {
    val app = RoboApp
    val w = app.canWrite
    val context = LocalContext.current
    var items by remember { mutableStateOf<List<P.Reminder>>(emptyList()) }
    var loaded by remember { mutableStateOf(false) }
    var kind by remember { mutableStateOf(0) }
    var text by remember { mutableStateOf("") }
    val tomorrow = remember { Calendar.getInstance().apply { add(Calendar.DAY_OF_YEAR, 1); set(Calendar.HOUR_OF_DAY, 7); set(Calendar.MINUTE, 30); set(Calendar.SECOND, 0) } }
    var onceAt by remember { mutableStateOf(tomorrow.timeInMillis) }
    var dailyMinute by remember { mutableStateOf(450) }
    var timerMinutes by remember { mutableStateOf("10") }

    fun reload() {
        app.call<P.ReminderDocument>(start = { d -> app.repo.loadReminders(d) }, onOk = { items = it.reminders; loaded = true })
    }
    LaunchedEffect(app.ready) { if (app.ready) reload() }

    ScreenColumn {
        RoboCard("Pengingat (${items.size}/${P.MAX_REMINDERS})") {
            if (items.isEmpty()) Note(if (loaded) "Belum ada pengingat." else "Memuat…")
            items.forEach { r ->
                Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalAlignment = Alignment.Top) {
                    Column(Modifier.weight(1f)) {
                        Text(r.text, color = RoboColors.Text, fontSize = 14.sp)
                        Text(describe(r), color = RoboColors.Mute, fontSize = 11.5.sp)
                    }
                    Text("✕", color = RoboColors.Bad, fontSize = 16.sp, modifier = Modifier.clickable(enabled = w) {
                        app.call("Pengingat dihapus", { d -> app.repo.deleteReminder(r.id, d) }, onOk = { reload() })
                    }.padding(6.dp))
                }
            }
        }
        RoboCard("Tambah pengingat") {
            RoboField("Jenis") { SelectBox(REMINDER_KINDS, kind, { kind = it }, enabled = w) }
            RoboField("Waktu / durasi") {
                when (kind) {
                    0 -> Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        val cal = Calendar.getInstance().apply { timeInMillis = onceAt }
                        RoboButton(android.text.format.DateFormat.format("yyyy-MM-dd", cal).toString(), {
                            DatePickerDialog(context, { _, y, m, d -> onceAt = Calendar.getInstance().apply { timeInMillis = onceAt; set(y, m, d) }.timeInMillis },
                                cal.get(Calendar.YEAR), cal.get(Calendar.MONTH), cal.get(Calendar.DAY_OF_MONTH)).show()
                        }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
                        RoboButton(android.text.format.DateFormat.format("HH:mm", cal).toString(), {
                            TimePickerDialog(context, { _, h, m -> onceAt = Calendar.getInstance().apply { timeInMillis = onceAt; set(Calendar.HOUR_OF_DAY, h); set(Calendar.MINUTE, m); set(Calendar.SECOND, 0) }.timeInMillis },
                                cal.get(Calendar.HOUR_OF_DAY), cal.get(Calendar.MINUTE), true).show()
                        }, Modifier.weight(1f), ButtonKind.SECONDARY, w)
                    }
                    1 -> RoboButton("%02d:%02d".format(dailyMinute / 60, dailyMinute % 60), {
                        TimePickerDialog(context, { _, h, m -> dailyMinute = h * 60 + m }, dailyMinute / 60, dailyMinute % 60, true).show()
                    }, Modifier.fillMaxWidth(), ButtonKind.SECONDARY, w)
                    else -> Column {
                        Segmented(listOf("5 mnt", "10 mnt", "30 mnt", "60 mnt"), listOf("5", "10", "30", "60").indexOf(timerMinutes), { timerMinutes = listOf("5", "10", "30", "60")[it] }, w)
                        Gap(6)
                        TextFieldRobo(timerMinutes, { timerMinutes = it.filter { c -> c.isDigit() }.take(4) }, placeholder = "menit (1–1440)", enabled = w, keyboardType = KeyboardType.Number)
                    }
                }
            }
            RoboField("Teks (1–111 byte)") { TextFieldRobo(text, { text = it }, placeholder = "mis. Minum obat", enabled = w, maxLength = P.MAX_REMINDER_TEXT) }
            RoboButton("Tambah", {
                val schedule = kind + 1
                val minute = if (kind == 1) dailyMinute else 0
                val value = when (kind) { 0 -> onceAt / 1000; 1 -> 0L; else -> (timerMinutes.toLongOrNull() ?: 0L) * 60 }
                app.call("Pengingat ditambahkan", { d -> app.repo.addReminder(schedule, minute, value, text.trim(), items.size, d) },
                    onOk = { text = ""; reload() })
            }, Modifier.fillMaxWidth(), enabled = w && text.isNotBlank())
            Note("Maksimal ${P.MAX_REMINDERS} pengingat. Robot butuh jam yang sudah sinkron (Wi-Fi) agar bisa membunyikannya.")
        }
    }
}
