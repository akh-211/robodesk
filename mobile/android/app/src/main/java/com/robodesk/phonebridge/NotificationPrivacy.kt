package com.robodesk.phonebridge

import android.app.KeyguardManager
import android.content.Context
import java.util.Locale

/** Local controls can only narrow the robot dashboard allowlist. */
object NotificationPrivacy {
    private const val PREFS = "privacy_modes"
    private const val PREFIX = "mode."

    fun mode(context: Context, packageName: String): AppContentMode {
        val stored = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getInt(PREFIX + packageName, AppContentMode.OFF.ordinal)
        return AppContentMode.entries.getOrElse(stored) { AppContentMode.OFF }
    }

    fun setMode(context: Context, packageName: String, mode: AppContentMode) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putInt(PREFIX + packageName, mode.ordinal).apply()
    }

    fun effectiveMode(context: Context, packageName: String): AppContentMode {
        val local = mode(context, packageName)
        val locked = (context.getSystemService(Context.KEYGUARD_SERVICE) as? KeyguardManager)
            ?.isDeviceLocked == true
        return if (locked && local.ordinal > AppContentMode.APP_ONLY.ordinal) AppContentMode.APP_ONLY else local
    }

    fun sanitize(title: String, body: String, mode: AppContentMode): SafeNotificationText =
        NotificationTextRedactor.sanitize(title, body, mode)
}
