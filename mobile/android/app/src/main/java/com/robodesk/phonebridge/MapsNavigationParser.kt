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
            else -> "unknown"
        }
        if (!navigationCategory && state == "active" && turn == "unknown") return null
        val distance = Regex("(?<![\\d.,])(\\d+(?:[.,]\\d+)?)\\s*(km|m|kilometer|meter)\\b").find("$title $body".lowercase(Locale.ROOT))
        val meters = distance?.let { ((it.groupValues[1].replace(',','.').toDoubleOrNull() ?: 0.0) * if(it.groupValues[2] == "km" || it.groupValues[2] == "kilometer") 1000 else 1).roundToInt().coerceIn(0,1000000) } ?: -1
        val eta = Regex("\\b\\d+\\s*(?:h|hr|hour|jam)\\s*\\d*\\s*(?:min|minute|menit)?\\b|\\b\\d+\\s*(?:min|minute|menit)\\b").find("$subtext $body".lowercase(Locale.ROOT))?.value.orEmpty()
        return NavigationInstruction(state,turn,meters,title.ifBlank { body },eta)
    }
}
