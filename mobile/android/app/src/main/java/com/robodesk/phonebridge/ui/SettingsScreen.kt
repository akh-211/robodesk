package com.robodesk.phonebridge.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
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
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.robodesk.phonebridge.PersonalityPresets
import com.robodesk.phonebridge.RobotSettings
import com.robodesk.phonebridge.SettingsUnits

/** Segmented control (`.seg`): accent fill on the selected option. */
@Composable
fun Segmented(options: List<String>, selected: Int, onSelect: (Int) -> Unit, enabled: Boolean = true) {
    val shape = RoundedCornerShape(12.dp)
    Row(Modifier.fillMaxWidth().alpha(if (enabled) 1f else 0.4f).clip(shape).background(RoboColors.Input).border(1.dp, RoboColors.Line, shape).padding(3.dp),
        horizontalArrangement = Arrangement.spacedBy(3.dp)) {
        options.forEachIndexed { i, label ->
            val on = i == selected
            Box(Modifier.weight(1f).clip(RoundedCornerShape(9.dp)).background(if (on) RoboColors.Accent else RoboColors.Input)
                .clickable(enabled = enabled) { onSelect(i) }.padding(vertical = 9.dp, horizontal = 2.dp), contentAlignment = Alignment.Center) {
                Text(label, color = if (on) RoboColors.BtnPrimaryText else RoboColors.Mute, fontSize = 12.sp, maxLines = 1)
            }
        }
    }
}

/** Row with a value on the right and a full-width slider below (mockup volume / percent rows). */
@Composable
private fun FullSlider(label: String, sub: String, valueText: String, value: Int, range: IntRange, enabled: Boolean, onChange: (Int) -> Unit) {
    RoboRow(label, sub, last = true) { ValueText(valueText) }
    Slider(value = value.toFloat(), onValueChange = { onChange(it.toInt()) }, valueRange = range.first.toFloat()..range.last.toFloat(), enabled = enabled,
        modifier = Modifier.fillMaxWidth(), colors = SliderDefaults.colors(thumbColor = RoboColors.Accent, activeTrackColor = RoboColors.Accent, inactiveTrackColor = RoboColors.Line))
}

@Composable
private fun ErrorText(errors: Map<String, String>, key: String) {
    errors[key]?.let { Text(it, color = RoboColors.Bad, fontSize = 12.sp, modifier = Modifier.padding(bottom = 6.dp)) }
}

@Composable
fun SettingsScreen() {
    val app = RoboApp
    val d = app.draft
    ScreenColumn {
        if (d == null) {
            RoboCard {
                Text(if (app.settingsLoading) "Memuat setelan…" else app.settingsError ?: "Setelan belum dimuat", color = RoboColors.Mute)
                Gap(8)
                RoboButton("Muat ulang", { app.loadSettings() }, enabled = app.ready && !app.settingsLoading)
            }
        } else SettingsGroups(d)
    }
}

@Composable
private fun ColumnScope.SettingsGroups(d: RobotSettings) {
    val app = RoboApp
    val w = app.canWrite
    val errors = d.validate()
    @Composable fun Sw(key: String, label: String) = SwitchRow(label, d.bool(key), { v -> app.toggle(key, v) }, key, enabled = w)
    @Composable fun Percent(key: String, label: String) =
        FullSlider(label, key, SettingsUnits.formatPercent(d.int(key)), d.int(key), 0..200, w) { v -> app.edit { it.setInt(key, v) } }
    @Composable fun TextSetting(key: String, label: String, max: Int) {
        RoboField("$label ($key · maks $max)") {
            val shown = remember(key, app.loadGen) { mutableStateOf(d.string(key)) }
            TextFieldRobo(shown.value, { v -> shown.value = v; app.edit { it.setString(key, v) } }, enabled = w, maxLength = max)
        }
        ErrorText(errors, key)
    }
    @Composable fun Scaled(label: String, key: String, format: (Int) -> String, parse: (String) -> Int?, modifier: Modifier = Modifier) {
        Column(modifier) {
            RoboField(label) {
                val text = remember(key, app.loadGen) { mutableStateOf(format(d.int(key))) }
                TextFieldRobo(text.value, { v -> text.value = v; parse(v)?.let { n -> app.edit { it.setInt(key, n) } } }, enabled = w, keyboardType = KeyboardType.Decimal)
            }
            ErrorText(errors, key)
        }
    }
    @Composable fun IntField(label: String, key: String, modifier: Modifier = Modifier) = Scaled(label, key, { it.toString() }, { it.trim().toIntOrNull() }, modifier)
    @Composable fun Time(label: String, key: String, modifier: Modifier = Modifier) = Column(modifier) {
        RoboField(label) { TimeField(d.int(key), { v -> app.edit { it.setInt(key, v) } }, enabled = w) }
    }

    AccordionCard("Suara & bicara", initiallyOpen = true) {
        Sw("speechEnabled", "Bicara aktif")
        if (d.has("voiceReplyMode")) SelectRow("Jawaban dari suara", listOf("Suara", "Teks di layar", "Suara + teks"), d.int("voiceReplyMode").coerceIn(0, 2),
            { i -> app.setNow("voiceReplyMode", i) }, "Saat Anda bicara ke robot", enabled = w)
        if (d.has("chatReplyMode")) SelectRow("Jawaban dari chat", listOf("Suara", "Teks di layar", "Suara + teks"), d.int("chatReplyMode").coerceIn(0, 2),
            { i -> app.setNow("chatReplyMode", i) }, "Saat Anda mengirim chat dari app", enabled = w)
        FullSlider("Volume speaker", "speakerGainMilli · 50–1000", SettingsUnits.formatGain(d.int("speakerGainMilli")), d.int("speakerGainMilli"), 50..1000, w) { v -> app.edit { it.setInt("speakerGainMilli", v) } }
        Gap(8)
        RoboField("Mode input (inputMode)") {
            val modes = listOf("Kata bangun" to 2, "Sentuh: ketuk untuk bicara" to 1, "Selalu mendengarkan (lama)" to 0)
            SelectBox(modes.map { it.first }, modes.indexOfFirst { it.second == d.int("inputMode") }.coerceAtLeast(0), { i -> app.edit { it.setInt("inputMode", modes[i].second) } }, enabled = w)
        }
        TextSetting("geminiVoice", "Suara Gemini", 31)
        val style = d.string("speechStyle")
        RoboField("Gaya bicara (speechStyle · maks 240) ${style.length}/240") {
            val shown = remember("speechStyle", app.loadGen) { mutableStateOf(style) }
            TextFieldRobo(shown.value, { v -> shown.value = v; app.edit { it.setString("speechStyle", v) } }, enabled = w, maxLength = 240, singleLine = false)
        }
        ErrorText(errors, "speechStyle")
    }
    AccordionCard("Karakter & suara karakter") {
        if (d.has("personalityPreset")) RoboField("Kepribadian (personalityPreset)") {
            Segmented(PersonalityPresets.LABELS, d.int("personalityPreset", 1), { v -> app.edit { PersonalityPresets.apply(it, v) } }, w)
        }
        if (d.has("ownerBirthMonth") && d.has("ownerBirthDay")) RoboField("Ulang tahun pemilik (disimpan di robot)") {
            val months = listOf("–") + (1..12).map { it.toString() }
            val days = listOf("–") + (1..31).map { it.toString() }
            SelectBox(months, d.int("ownerBirthMonth").coerceIn(0, 12), { i -> app.edit { it.setInt("ownerBirthMonth", i) } }, enabled = w)
            SelectBox(days, d.int("ownerBirthDay").coerceIn(0, 31), { i -> app.edit { it.setInt("ownerBirthDay", i) } }, enabled = w)
        }
        ErrorText(errors, "ownerBirthDay")
        RoboField("Intensitas karakter (characterIntensity)") {
            Segmented(listOf("Halus", "Aktif tapi tenang", "Ekspresif"), d.int("characterIntensity"), { v -> app.edit { it.setInt("characterIntensity", v) } }, w)
        }
        Sw("characterSfx", "SFX karakter lokal")
        Percent("sfxIntensityX100", "Intensitas SFX")
        Gap(8)
        RoboField("Frekuensi SFX (sonicFrequency)") {
            Segmented(listOf("Rendah", "Normal", "Ekspresif"), d.int("sonicFrequency"), { v -> app.edit { it.setInt("sonicFrequency", v) } }, w)
        }
        Sw("wakeSfx", "Suara konfirmasi kata bangun")
        Sw("touchSfx", "Suara sentuh & sayang")
        Sw("motionSfx", "Suara gerak, angkat, goyang")
        Sw("notificationSfx", "Suara notifikasi & status")
        Percent("quietGainX100", "Volume saat jam tenang")
    }
    AccordionCard("Wajah (LivingEyes)") {
        Sw("faceLifeEnabled", "Wajah hidup (perhatian, gerak mikro)")
        Sw("facePupils", "Pupil")
        Sw("faceBrows", "Alis tersedia")
        Sw("faceAutoBrows", "Alis hanya saat ekspresif")
        Sw("faceLashes", "Bulu mata tersedia")
        Sw("faceAutoLashes", "Bulu mata hanya saat lembut/sosial")
        Sw("facePupilLessActing", "Akting bentuk mata kuat saat tanpa pupil")
        Percent("faceGazeReachX100", "Jangkauan pandangan")
        Percent("faceMicroX100", "Gerak mikro")
        Percent("faceSilhouetteX100", "Siluet wajah")
        Gap(8)
        RoboField("Mulut (mouthMode)") { Segmented(listOf("Otomatis", "Sembunyi", "Selalu"), d.int("mouthMode"), { v -> app.edit { it.setInt("mouthMode", v) } }, w) }
    }
    AccordionCard("AI & memori") {
        TextSetting("robotName", "Nama robot", 31)
        OwnerNameField(w)
        TextSetting("geminiModel", "Model Gemini", 47)
        TextSetting("summaryModel", "Model ringkasan", 47)
        Sw("memoryEnabled", "Memori aktif")
        Sw("autoMemory", "Simpan memori otomatis")
        Sw("proactiveVisual", "Reaksi visual proaktif")
        Sw("proactiveVoice", "Isyarat suara proaktif (tidak membuka mikrofon)")
        Sw("interactionMetrics", "Diagnostik interaksi lokal (tanpa transkrip)")
        Gap(8)
        IntField("Batas tugas latar harian (backgroundDailyLimit 0–12)", "backgroundDailyLimit")
    }
    AccordionCard("Jadwal tidur & briefing") { // PD-2.5: quiet hours = the robot's bedtime
        Sw("quietHoursEnabled", "Jadwal tidur (jam tenang)")
        ButtonRow { Time("Tidur (quietStartMin)", "quietStartMin", Modifier.weight(1f)); Time("Bangun (quietEndMin)", "quietEndMin", Modifier.weight(1f)) }
        Sw("dailyBriefingEnabled", "Briefing harian")
        Time("Waktu briefing (dailyBriefingMinute)", "dailyBriefingMinute")
        RoboField("Zona waktu (timezoneOffsetMin)") {
            Column {
                val offset = d.int("timezoneOffsetMin")
                val idx = SettingsUnits.zoneIndex(offset)
                var custom by remember(app.loadGen) { mutableStateOf(idx == SettingsUnits.ZONES.size) }
                val labels = SettingsUnits.ZONES.map { it.label } + "Kustom (−12:00…+14:00)"
                SelectBox(labels, if (custom) SettingsUnits.ZONES.size else idx, { i ->
                    if (i < SettingsUnits.ZONES.size) { custom = false; app.edit { it.setInt("timezoneOffsetMin", SettingsUnits.ZONES[i].offsetMin) } } else custom = true
                }, enabled = w)
                if (custom) {
                    Gap(6)
                    val text = remember(app.loadGen) { mutableStateOf(SettingsUnits.formatOffset(offset)) }
                    TextFieldRobo(text.value, { v -> text.value = v; SettingsUnits.parseOffset(v)?.let { n -> app.edit { it.setInt("timezoneOffsetMin", n) } } }, enabled = w, placeholder = "+05:30")
                }
            }
        }
        ErrorText(errors, "timezoneOffsetMin")
    }
    AccordionCard("Pomodoro & kenyamanan") {
        ButtonRow {
            IntField("Fokus mnt (pomodoroFocusMinutes)", "pomodoroFocusMinutes", Modifier.weight(1f))
            IntField("Istirahat (pomodoroBreakMinutes)", "pomodoroBreakMinutes", Modifier.weight(1f))
            IntField("Panjang (pomodoroLongBreakMinutes)", "pomodoroLongBreakMinutes", Modifier.weight(1f))
        }
        Sw("comfortAlertsEnabled", "Peringatan kenyamanan ruangan")
        ButtonRow {
            Scaled("Suhu min °C (comfortMinTempX10)", "comfortMinTempX10", SettingsUnits::formatX10, SettingsUnits::parseX10, Modifier.weight(1f))
            Scaled("Suhu maks °C (comfortMaxTempX10)", "comfortMaxTempX10", SettingsUnits::formatX10, SettingsUnits::parseX10, Modifier.weight(1f))
        }
        ButtonRow {
            Scaled("Lembap min % (comfortMinHumidityX10)", "comfortMinHumidityX10", SettingsUnits::formatX10, SettingsUnits::parseX10, Modifier.weight(1f))
            Scaled("Lembap maks % (comfortMaxHumidityX10)", "comfortMaxHumidityX10", SettingsUnits::formatX10, SettingsUnits::parseX10, Modifier.weight(1f))
        }
        Note("Min harus lebih kecil dari maks. Nilai dikirim ×10.")
    }
    AccordionCard("Notifikasi HP") { NotifyControls(d) }
    AccordionCard("Mikrofon & VAD (lanjutan)") {
        ButtonRow {
            Scaled("Ambang mulai ×noise (vadStartX100)", "vadStartX100", SettingsUnits::formatX100, SettingsUnits::parseX100, Modifier.weight(1f))
            Scaled("Ambang akhir ×noise (vadEndX100)", "vadEndX100", SettingsUnits::formatX100, SettingsUnits::parseX100, Modifier.weight(1f))
        }
        ButtonRow {
            IntField("Hening akhir giliran ms (vadEndMs)", "vadEndMs", Modifier.weight(1f))
            IntField("Jeda setelah bicara ms (postSpeakGuardMs)", "postSpeakGuardMs", Modifier.weight(1f))
        }
        IntField("Tunggu lanjutan setelah kata bangun ms (wakeFollowupMs)", "wakeFollowupMs")
    }
}

/** Owner preferred name = memory fact `owner.name` (category profile), not a settings key. */
@Composable
private fun OwnerNameField(w: Boolean) {
    val app = RoboApp
    LaunchedEffect(app.ready) { app.loadMemory() }
    val current = app.facts.firstOrNull { it.key == "owner.name" }?.value.orEmpty()
    var text by remember(current) { mutableStateOf(current) }
    RoboField("Nama panggilan pemilik (memori owner.name)") {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f)) { TextFieldRobo(text, { text = it }, enabled = w, maxLength = 159) }
            RoboButton("Simpan", {
                app.call("Nama pemilik disimpan", { d -> app.repo.addMemory(1, "owner.name", text.trim(), d) }, onOk = { app.loadMemory() })
            }, kind = ButtonKind.SECONDARY, enabled = w && text.isNotBlank() && text.trim() != current)
        }
    }
}
