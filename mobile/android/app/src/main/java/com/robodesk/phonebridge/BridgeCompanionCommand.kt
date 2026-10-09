package com.robodesk.phonebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Exact bounded command schema paired with PhoneCompanionCommand.h. */
object BridgeCompanionCommand {
    val activities = listOf(
        "curious_look", "expression_practice", "rhythm_play", "daydream",
        "stretch_reset", "rest", "quiet_company", "pixel_doodle", "watch_room",
        "touch_play", "rhythm_improv", "focus_company", "calm_breathing"
    )

    fun intensity(level: Int): ByteArray? = if (level in 0..2) byteArrayOf(1, level.toByte()) else null
    fun startActivity(index: Int): ByteArray? = if (index in activities.indices) byteArrayOf(2, (index + 1).toByte()) else null
    fun pauseActivity() = byteArrayOf(3)
    fun resumeActivity() = byteArrayOf(4)
    fun cancelActivity() = byteArrayOf(5)
    fun quietEnabled(enabled: Boolean) = byteArrayOf(6, (if (enabled) 1 else 0).toByte())

    fun quietHours(enabled: Boolean, startMin: Int, endMin: Int): ByteArray? {
        if (startMin !in 0..1439 || endMin !in 0..1439) return null
        return ByteBuffer.allocate(6).order(ByteOrder.LITTLE_ENDIAN)
            .put(6.toByte()).put((if (enabled) 1 else 0).toByte()).putShort(startMin.toShort()).putShort(endMin.toShort()).array()
    }
}
