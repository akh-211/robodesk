package com.robodesk.phonebridge

import java.util.Locale
import kotlin.math.roundToInt

data class NavigationInstruction(val state: String, val turn: String, val distanceMeters: Int, val road: String, val eta: String) {
    fun wire(): ByteArray = "NAV1\n$state\n$turn\n${if(distanceMeters<0)"?" else distanceMeters}\n${utf8Field(road,48)}\n${utf8Field(eta,24)}".toByteArray(Charsets.UTF_8)
}

fun utf8Field(value: String, maxBytes: Int): String {
    val clean = value.map { if (it.code < 32 || it.code == 127) ' ' else it }.joinToString("")
    var end = clean.length
    while (end > 0 && clean.substring(0,end).toByteArray(Charsets.UTF_8).size > maxBytes) end--
    if (end > 0 && end < clean.length && Character.isHighSurrogate(clean[end-1])) end--
    return clean.substring(0,end)
}

// Notification text is not a stable Maps API. Unknown maneuvers remain unknown.
private const val UNITS = "kilometers?|kilometres?|km|meters?|metres?|miles?|mi|feet|ft|yards?|yd|m"
private val DISTANCE_ONLY = Regex("^\\s*\\d+(?:[.,]\\d+)?\\s*($UNITS)\\s*$", RegexOption.IGNORE_CASE)
private fun unitMeters(unit: String): Double = when (unit) {
    "km", "kilometer", "kilometers", "kilometre", "kilometres" -> 1000.0
    "mi", "mile", "miles" -> 1609.344
    "ft", "feet" -> 0.3048
    "yd", "yard", "yards" -> 0.9144
    else -> 1.0
}

object MapsNavigationParser {
    fun parse(title: String, body: String, subtext: String, navigationCategory: Boolean): NavigationInstruction? {
        val text = "$title $body $subtext".lowercase(Locale.ROOT)
        val state = when {
            Regex("\\b(arrived|you have arrived|tiba di tujuan|sampai di tujuan)\\b").containsMatchIn(text) -> "arrived"
            Regex("\\b(rerouting|recalculating|menghitung ulang|mencari rute)\\b").containsMatchIn(text) -> "rerouting"
            else -> "active"
        }
        val turn = when {
            Regex("\\b(u.turn|putar balik|putar arah)\\b").containsMatchIn(text) -> "uturn"
            Regex("\\b(slight left|keep left|serong kiri|ambil kiri)\\b").containsMatchIn(text) -> "slight_left"
            Regex("\\b(slight right|keep right|serong kanan|ambil kanan)\\b").containsMatchIn(text) -> "slight_right"
            Regex("\\b(turn left|belok kiri)\\b").containsMatchIn(text) -> "left"
            Regex("\\b(turn right|belok kanan)\\b").containsMatchIn(text) -> "right"
            Regex("\\b(roundabout|bundaran)\\b").containsMatchIn(text) -> "roundabout"
            Regex("\\b(continue straight|head straight|go straight|terus lurus|lurus)\\b").containsMatchIn(text) -> "straight"
            Regex("\\b(exit|ramp|fork|take the|jalan keluar|pintu keluar|percabangan|persimpangan|ambil)\\b[^|]*\\b(left|kiri)\\b").containsMatchIn(text) -> "slight_left"
            Regex("\\b(exit|ramp|fork|take the|jalan keluar|pintu keluar|percabangan|persimpangan|ambil)\\b[^|]*\\b(right|kanan)\\b").containsMatchIn(text) -> "slight_right"
            Regex("\\b(merge|bergabung|masuk ke jalan)\\b").containsMatchIn(text) -> "straight"
            else -> "unknown"
        }
        if (!navigationCategory && state == "active" && turn == "unknown") return null
        val distance = Regex("(?<![\\d.,])(\\d+(?:[.,]\\d+)?)\\s*($UNITS)\\b").find("$title $body".lowercase(Locale.ROOT))
        val meters = distance?.let { ((it.groupValues[1].replace(',','.').toDoubleOrNull() ?: 0.0) * unitMeters(it.groupValues[2])).roundToInt().coerceIn(0,1000000) } ?: -1
        val etaText = "$subtext $body".lowercase(Locale.ROOT)
        val duration = Regex("\\b\\d+\\s*(?:h|hr|hour|jam)\\s*\\d*\\s*(?:min|minute|menit)?\\b|\\b\\d+\\s*(?:min|minute|menit)\\b").find(etaText)?.value.orEmpty()
        val clock = Regex("\\b\\d{1,2}[:.]\\d{2}(?:\\s?[ap]m)?\\b").find(etaText)?.value.orEmpty()
        val eta = listOf(duration, clock).filter { it.isNotEmpty() }.joinToString(" ")
        val road = if (title.isNotBlank() && !DISTANCE_ONLY.matches(title)) title else body.ifBlank { title }
        return NavigationInstruction(state,turn,meters,road,eta)
    }
}
