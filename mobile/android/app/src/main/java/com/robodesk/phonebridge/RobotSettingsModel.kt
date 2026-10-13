package com.robodesk.phonebridge

/**
 * Pure model of the schema-1 settings document (RoboBehaviorWire.h). No Android or org.json dependency so it runs in host tests.
 * Unknown keys, key order and values the UI never edits are preserved byte-for-byte in meaning when the document is written back.
 */
object FlatJson {
    sealed class Value {
        class Str(val text: String) : Value()
        /** Numbers keep their raw token so an unedited value is never reformatted. */
        class Num(val raw: String) : Value()
        class Bool(val value: Boolean) : Value()
        /** null, nested object or array: kept as raw JSON text and written back untouched. */
        class Raw(val text: String) : Value()
    }

    /** Returns the top-level object as an ordered map, or null when [bytes] is not a JSON object. */
    fun parse(bytes: ByteArray): LinkedHashMap<String, Value>? {
        val text = try {
            Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(bytes)).toString()
        } catch (_: java.nio.charset.CharacterCodingException) { return null }
        return Parser(text).parseObject()
    }

    fun write(doc: Map<String, Value>): ByteArray {
        val sb = StringBuilder("{")
        var first = true
        for ((key, value) in doc) {
            if (!first) sb.append(',')
            first = false
            quote(sb, key).append(':')
            when (value) {
                is Value.Str -> quote(sb, value.text)
                is Value.Num -> sb.append(value.raw)
                is Value.Bool -> sb.append(if (value.value) "true" else "false")
                is Value.Raw -> sb.append(value.text)
            }
        }
        return sb.append('}').toString().toByteArray(Charsets.UTF_8)
    }

    private fun quote(sb: StringBuilder, text: String): StringBuilder {
        sb.append('"')
        for (ch in text) {
            when {
                ch == '"' -> sb.append("\\\"")
                ch == '\\' -> sb.append("\\\\")
                ch == '\n' -> sb.append("\\n")
                ch == '\r' -> sb.append("\\r")
                ch == '\t' -> sb.append("\\t")
                ch < ' ' || ch == ' ' || ch == ' ' -> sb.append("\\u%04x".format(ch.code))
                else -> sb.append(ch)
            }
        }
        return sb.append('"')
    }

    private class Parser(private val s: String) {
        var i = 0

        fun parseObject(): LinkedHashMap<String, Value>? {
            skip()
            if (!eat('{')) return null
            val out = LinkedHashMap<String, Value>()
            skip()
            if (eat('}')) return if (finished()) out else null
            while (true) {
                skip()
                val key = string() ?: return null
                skip()
                if (!eat(':')) return null
                skip()
                out[key] = value() ?: return null
                skip()
                if (eat(',')) continue
                if (eat('}')) break
                return null
            }
            return if (finished()) out else null
        }

        private fun finished(): Boolean { skip(); return i == s.length }
        private fun skip() { while (i < s.length && s[i] in " \t\r\n") i++ }
        private fun eat(c: Char): Boolean = if (i < s.length && s[i] == c) { i++; true } else false

        private fun value(): Value? {
            if (i >= s.length) return null
            return when (val c = s[i]) {
                '"' -> string()?.let { Value.Str(it) }
                't' -> literal("true")?.let { Value.Bool(true) }
                'f' -> literal("false")?.let { Value.Bool(false) }
                'n' -> literal("null")?.let { Value.Raw("null") }
                '{', '[' -> nested(c)
                else -> number()
            }
        }

        private fun literal(word: String): String? =
            if (s.startsWith(word, i)) { i += word.length; word } else null

        private fun number(): Value? {
            val start = i
            if (i < s.length && s[i] == '-') i++
            val digits = i
            while (i < s.length && (s[i].isDigit() || s[i] in ".eE+-")) i++
            if (i == digits) return null
            val raw = s.substring(start, i)
            return if (raw.toDoubleOrNull() == null) null else Value.Num(raw)
        }

        private fun nested(open: Char): Value? {
            val start = i
            var depth = 0
            while (i < s.length) {
                val c = s[i]
                if (c == '"') { string() ?: return null; continue }
                if (c == '{' || c == '[') depth++
                if (c == '}' || c == ']') { depth--; if (depth == 0) { i++; return Value.Raw(s.substring(start, i)) } }
                i++
            }
            return null
        }

        private fun string(): String? {
            if (!eat('"')) return null
            val sb = StringBuilder()
            while (i < s.length) {
                val c = s[i++]
                when {
                    c == '"' -> return sb.toString()
                    c == '\\' -> {
                        if (i >= s.length) return null
                        when (val e = s[i++]) {
                            '"', '\\', '/' -> sb.append(e)
                            'b' -> sb.append('\b')
                            'f' -> sb.append('\u000c')
                            'n' -> sb.append('\n')
                            'r' -> sb.append('\r')
                            't' -> sb.append('\t')
                            'u' -> {
                                if (i + 4 > s.length) return null
                                val code = s.substring(i, i + 4).toIntOrNull(16) ?: return null
                                sb.append(code.toChar()); i += 4
                            }
                            else -> return null
                        }
                    }
                    c < ' ' -> return null
                    else -> sb.append(c)
                }
            }
            return null
        }
    }
}

/** Editable view of one settings document. All values the UI does not touch stay exactly as the robot sent them. */
class RobotSettings private constructor(private val original: Map<String, FlatJson.Value>, private val doc: LinkedHashMap<String, FlatJson.Value>) {
    companion object {
        /** Keys the app never shows or edits: kept untouched on save (CLAUDE: phonePlatform is Forbidden over BLE). */
        val INTERNAL_KEYS = setOf("schema", "phonePlatform", "pomodoroWasActive", "pomodoroInterrupted")

        /** Every exported key that has a control in the app (A5 parity check). */
        val CONTROLLED_KEYS = listOf(
            "speechEnabled", "speakerGainMilli", "inputMode", "geminiVoice", "speechStyle",
            "characterIntensity", "characterSfx", "sfxIntensityX100", "sonicFrequency", "wakeSfx", "touchSfx", "motionSfx", "notificationSfx", "quietGainX100",
            "faceLifeEnabled", "facePupils", "faceBrows", "faceAutoBrows", "faceLashes", "faceAutoLashes", "facePupilLessActing",
            "faceGazeReachX100", "faceMicroX100", "faceSilhouetteX100", "mouthMode",
            "robotName", "geminiModel", "summaryModel", "memoryEnabled", "autoMemory", "proactiveVisual", "proactiveVoice", "interactionMetrics", "backgroundDailyLimit",
            "quietHoursEnabled", "quietStartMin", "quietEndMin", "dailyBriefingEnabled", "dailyBriefingMinute", "timezoneOffsetMin",
            "pomodoroFocusMinutes", "pomodoroBreakMinutes", "pomodoroLongBreakMinutes", "comfortAlertsEnabled",
            "comfortMinTempX10", "comfortMaxTempX10", "comfortMinHumidityX10", "comfortMaxHumidityX10",
            "navigationEnabled", "notificationAllowlist", "notificationSpeechAllowlist",
            "vadStartX100", "vadEndX100", "vadEndMs", "postSpeakGuardMs", "wakeFollowupMs",
            "masterSound", "privacyMicMuted", "voiceReplyMode", "chatReplyMode",
            "personalityPreset", "ownerBirthMonth", "ownerBirthDay"
        )

        fun parse(bytes: ByteArray): RobotSettings? {
            val parsed = FlatJson.parse(bytes) ?: return null
            if ((parsed["schema"] as? FlatJson.Value.Num)?.raw != "1") return null
            return RobotSettings(LinkedHashMap(parsed), LinkedHashMap(parsed))
        }
    }

    val keys: Set<String> get() = doc.keys

    fun has(key: String) = key in doc
    fun int(key: String, default: Int = 0): Int = when (val v = doc[key]) {
        is FlatJson.Value.Num -> v.raw.toDoubleOrNull()?.toInt() ?: default
        is FlatJson.Value.Bool -> if (v.value) 1 else 0
        else -> default
    }
    fun bool(key: String): Boolean = int(key) != 0
    fun string(key: String, default: String = ""): String = (doc[key] as? FlatJson.Value.Str)?.text ?: default

    /** Writes numbers (the firmware only accepts numeric 0/1 for flags). */
    fun setInt(key: String, value: Int) { doc[key] = FlatJson.Value.Num(value.toString()) }
    fun setBool(key: String, value: Boolean) = setInt(key, if (value) 1 else 0)
    fun setString(key: String, value: String) { doc[key] = FlatJson.Value.Str(value) }

    fun copy(): RobotSettings = RobotSettings(original, LinkedHashMap(doc))

    /** Takes [key]'s current value from [other] (used to apply or revert a single toggle without touching other unsaved edits). */
    fun copyKeyFrom(other: RobotSettings, key: String) { other.doc[key]?.let { doc[key] = it } }

    /** Keys whose value differs from what the robot sent. */
    fun changedKeys(): List<String> = doc.keys.filter { !sameValue(original[it], doc[it]) }
    val dirty: Boolean get() = changedKeys().isNotEmpty()

    fun toBytes(): ByteArray = FlatJson.write(doc)

    /** True when every key in [keys] has the same value in both documents (numbers compared numerically). */
    fun sameAs(other: RobotSettings, keys: Collection<String> = doc.keys): List<String> =
        keys.filter { !sameValue(doc[it], other.doc[it]) }

    private fun sameValue(a: FlatJson.Value?, b: FlatJson.Value?): Boolean = when {
        a is FlatJson.Value.Num && b is FlatJson.Value.Num -> a.raw.toDoubleOrNull() == b.raw.toDoubleOrNull()
        a is FlatJson.Value.Str && b is FlatJson.Value.Str -> a.text == b.text
        a is FlatJson.Value.Bool && b is FlatJson.Value.Bool -> a.value == b.value
        a is FlatJson.Value.Raw && b is FlatJson.Value.Raw -> a.text == b.text
        a == null && b == null -> true
        else -> false
    }

    /** Client-side bounds mirroring roboBehaviorImport; returns key -> Indonesian message. Empty = valid. */
    fun validate(): Map<String, String> {
        val e = LinkedHashMap<String, String>()
        fun range(key: String, lo: Int, hi: Int, label: String) {
            if (has(key) && int(key) !in lo..hi) e[key] = "$label harus $lo-$hi"
        }
        fun text(key: String, max: Int, label: String) {
            if (has(key) && string(key).toByteArray(Charsets.UTF_8).size > max) e[key] = "$label maks $max byte"
        }
        range("speakerGainMilli", 50, 1000, "Volume")
        range("inputMode", 0, 2, "Mode input")
        range("voiceReplyMode", 0, 2, "Jawaban dari suara")
        range("chatReplyMode", 0, 2, "Jawaban dari chat")
        range("timezoneOffsetMin", -720, 840, "Zona waktu")
        range("backgroundDailyLimit", 0, 12, "Batas tugas latar")
        range("characterIntensity", 0, 2, "Intensitas karakter")
        range("mouthMode", 0, 2, "Mulut")
        range("sonicFrequency", 0, 2, "Frekuensi SFX")
        range("personalityPreset", 0, 2, "Kepribadian")
        range("ownerBirthMonth", 0, 12, "Bulan lahir"); range("ownerBirthDay", 0, 31, "Tanggal lahir")
        if (has("ownerBirthMonth") && has("ownerBirthDay") && (int("ownerBirthMonth") == 0) != (int("ownerBirthDay") == 0)) e["ownerBirthDay"] = "Isi tanggal dan bulan lahir, atau kosongkan keduanya"
        for (k in listOf("sfxIntensityX100", "quietGainX100", "faceGazeReachX100", "faceMicroX100", "faceSilhouetteX100")) range(k, 0, 200, "Nilai persen")
        for (k in listOf("quietStartMin", "quietEndMin", "dailyBriefingMinute")) range(k, 0, 1439, "Waktu")
        range("pomodoroFocusMinutes", 1, 90, "Fokus"); range("pomodoroBreakMinutes", 1, 60, "Istirahat"); range("pomodoroLongBreakMinutes", 1, 90, "Istirahat panjang")
        range("comfortMinTempX10", -100, 600, "Suhu min"); range("comfortMaxTempX10", -100, 600, "Suhu maks")
        range("comfortMinHumidityX10", 0, 1000, "Lembap min"); range("comfortMaxHumidityX10", 0, 1000, "Lembap maks")
        for (k in listOf("vadStartX100", "vadEndX100", "vadEndMs", "postSpeakGuardMs", "wakeFollowupMs")) range(k, 0, 65535, "Nilai")
        if (has("comfortMinTempX10") && has("comfortMaxTempX10") && int("comfortMinTempX10") >= int("comfortMaxTempX10")) e["comfortMaxTempX10"] = "Suhu min harus lebih kecil dari maks"
        if (has("comfortMinHumidityX10") && has("comfortMaxHumidityX10") && int("comfortMinHumidityX10") >= int("comfortMaxHumidityX10")) e["comfortMaxHumidityX10"] = "Lembap min harus lebih kecil dari maks"
        text("geminiVoice", 31, "Suara Gemini"); text("robotName", 31, "Nama robot"); text("geminiModel", 47, "Model Gemini")
        text("summaryModel", 47, "Model ringkasan"); text("speechStyle", 240, "Gaya bicara")
        text("notificationAllowlist", Allowlist.MAX_BYTES, "Daftar Tampil"); text("notificationSpeechAllowlist", Allowlist.MAX_BYTES, "Daftar Baca")
        if (toBytes().size > BridgeSettingsProtocol.MAX_DOCUMENT) e["document"] = BridgeSettingsProtocol.Status.TOO_LARGE.label
        return e
    }
}

/** Display/edit conversions between firmware units and UI values (minutes, x10, x100, milli). */
object SettingsUnits {
    fun formatMinutes(minutes: Int): String = "%02d:%02d".format(minutes.coerceIn(0, 1439) / 60, minutes.coerceIn(0, 1439) % 60)

    fun parseMinutes(text: String): Int? {
        val parts = text.trim().split(":")
        if (parts.size != 2) return null
        val h = parts[0].toIntOrNull() ?: return null
        val m = parts[1].toIntOrNull() ?: return null
        return if (h in 0..23 && m in 0..59) h * 60 + m else null
    }

    /** speakerGainMilli 450 -> "0,45". */
    fun formatGain(milli: Int): String = fixed(milli, 1000, 2)

    /** x10 value 205 -> "20,5"; 200 -> "20". */
    fun formatX10(value: Int): String = fixed(value, 10, 1).let { if (it.endsWith(",0")) it.dropLast(2) else it }

    /** vad x100 value 350 -> "3,50". */
    fun formatX100(value: Int): String = fixed(value, 100, 2)

    fun parseX10(text: String): Int? = scaled(text, 10)
    fun parseX100(text: String): Int? = scaled(text, 100)

    fun formatPercent(x100: Int): String = "$x100%"

    private fun fixed(value: Int, scale: Int, decimals: Int): String =
        java.math.BigDecimal(value).divide(java.math.BigDecimal(scale)).setScale(decimals, java.math.RoundingMode.HALF_UP).toPlainString().replace('.', ',')

    /** Parses "3,50" / "3.5" / "20" into an integer scaled by [scale]; rejects more decimals than the scale allows. */
    private fun scaled(text: String, scale: Int): Int? {
        val t = text.trim().replace(',', '.')
        if (t.isEmpty() || !Regex("""-?\d+(\.\d+)?""").matches(t)) return null
        val decimals = if ('.' in t) t.length - t.indexOf('.') - 1 else 0
        val maxDecimals = if (scale == 10) 1 else 2
        if (decimals > maxDecimals) return null
        return java.math.BigDecimal(t).multiply(java.math.BigDecimal(scale)).toBigIntegerExact().toInt()
    }

    class Zone(val label: String, val offsetMin: Int)
    val ZONES = listOf(Zone("WIB (UTC+7)", 420), Zone("WITA (UTC+8)", 480), Zone("WIT (UTC+9)", 540))

    /** Index into [ZONES] or ZONES.size for "Kustom". */
    fun zoneIndex(offsetMin: Int): Int = ZONES.indexOfFirst { it.offsetMin == offsetMin }.let { if (it < 0) ZONES.size else it }

    /** -720..840 minutes -> "+07:00". */
    fun formatOffset(offsetMin: Int): String {
        val sign = if (offsetMin < 0) "-" else "+"
        val a = Math.abs(offsetMin)
        return "%s%02d:%02d".format(sign, a / 60, a % 60)
    }

    /** "+07:30", "-03:00", "7" or "+7" -> minutes within -720..840. */
    fun parseOffset(text: String): Int? {
        val m = Regex("""^([+-]?)(\d{1,2})(?::(\d{2}))?$""").matchEntire(text.trim()) ?: return null
        val minutes = m.groupValues[2].toInt() * 60 + (m.groupValues[3].ifEmpty { "0" }).toInt()
        val value = if (m.groupValues[1] == "-") -minutes else minutes
        return if (m.groupValues[3].let { it.isEmpty() || it.toInt() < 60 } && value in -720..840) value else null
    }
}

/** Notification allowlists: two strings of package names (one per line), each at most 191 bytes (char[192] on the robot). */
object Allowlist {
    const val MAX_BYTES = 191
    const val DISPLAY_LIMIT = 192

    fun parse(text: String): LinkedHashSet<String> = text.split(Regex("""[\r\n;\s]+""")).filter { it.isNotBlank() }.toCollection(LinkedHashSet())
    fun serialize(packages: Collection<String>): String = packages.joinToString("\n")
    fun size(packages: Collection<String>): Int = serialize(packages).toByteArray(Charsets.UTF_8).size

    /** Packages whose names suggest banking or digital wallets are never offered (shown as "diblokir privasi"). */
    private val BLOCK_WORDS = listOf("bank", "wallet", "gopay", "ovo", "dana", "shopeepay", "linkaja", "bca", "mandiri", "bri.", "bni", "cimb", "permata", "jago", "paypal", "finance", "mbanking", "ibanking")
    fun privacyBlocked(pkg: String): Boolean {
        val p = pkg.lowercase()
        return BLOCK_WORDS.any { it in p }
    }

    class State(show: Collection<String>, speak: Collection<String>) {
        val show = LinkedHashSet(show)
        val speak = LinkedHashSet(speak).also { it.retainAll(this.show) }

        fun showBytes() = size(show)
        fun speakBytes() = size(speak)

        /** Turning Tampil off also turns Baca off (a spoken app must also be displayed). Returns an error message or null. */
        fun setShow(pkg: String, on: Boolean): String? {
            if (!on) { show.remove(pkg); speak.remove(pkg); return null }
            if (privacyBlocked(pkg)) return "Aplikasi ini diblokir privasi"
            if (pkg in show) return null
            if (size(show + pkg) > MAX_BYTES) return FULL
            show.add(pkg); return null
        }

        fun setSpeak(pkg: String, on: Boolean): String? {
            if (!on) { speak.remove(pkg); return null }
            if (pkg !in show) { val err = setShow(pkg, true); if (err != null) return err }
            if (pkg in speak) return null
            if (size(speak + pkg) > MAX_BYTES) return FULL
            speak.add(pkg); return null
        }

        fun label(): String = "${show.size} app · ${showBytes()}/$DISPLAY_LIMIT B"

        fun applyTo(settings: RobotSettings) {
            settings.setString("notificationAllowlist", serialize(show))
            settings.setString("notificationSpeechAllowlist", serialize(speak))
        }
    }

    const val FULL = "Daftar robot penuh · hapus satu aplikasi"

    fun from(settings: RobotSettings) = State(parse(settings.string("notificationAllowlist")), parse(settings.string("notificationSpeechAllowlist")))
}

/** PD-2.3 personality presets (Kalem/Ceria/Usil). The robot maps the preset to face life and starting traits;
 *  the app also moves the two visible knobs it implies so the settings screen stays truthful. */
object PersonalityPresets {
    val LABELS = listOf("Kalem", "Ceria", "Usil")
    private val INTENSITY = intArrayOf(0, 1, 2)
    private val SONIC_FREQUENCY = intArrayOf(0, 1, 2)

    fun apply(settings: RobotSettings, preset: Int) {
        if (preset !in LABELS.indices) return
        settings.setInt("personalityPreset", preset)
        settings.setInt("characterIntensity", INTENSITY[preset])
        settings.setInt("sonicFrequency", SONIC_FREQUENCY[preset])
    }
}

/** Firmware manifest for the HP-side version check (informational only; the robot re-verifies signed images itself). */
object UpdateManifest {
    const val URL = "https://github.com/akh-211/robodesk/releases/latest/download/manifest-esp32c3.txt"
    const val MAX_BYTES = 8192

    /** Reads the `version=` line; returns the version string (digits/dots only, <= 24 chars) or null. */
    fun parseVersion(text: String): String? {
        if (text.length > MAX_BYTES) return null
        for (line in text.lineSequence()) {
            val trimmed = line.trim()
            if (trimmed.startsWith("version=")) {
                val v = trimmed.removePrefix("version=").trim()
                return if (v.isNotEmpty() && v.length <= 24 && v.all { it.isDigit() || it == '.' }) v else null
            }
        }
        return null
    }

    /** True when [available] is a newer release number than [installed] (installed 0 = unknown -> treat as newer). */
    fun isNewer(available: String, installed: Long): Boolean {
        val n = available.toLongOrNull() ?: return true // dotted text cannot be compared with the monotone release number
        return installed <= 0L || n > installed
    }
}
