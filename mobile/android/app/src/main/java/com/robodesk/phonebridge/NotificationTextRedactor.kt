package com.robodesk.phonebridge

enum class AppContentMode { OFF, APP_ONLY, TITLE_ONLY, SNIPPET }

data class SafeNotificationText(val title: String, val snippet: String)

/** Pure policy code so redaction and UTF-8 bounds can be tested without an Android device. */
object NotificationTextRedactor {
    private val otpContext = Regex("(?i)\\b(?:otp|one[- ]time password|verification code|security code|kode verifikasi|kode otp|kode masuk|login code|sign[- ]?in code|passcode)\\b")
    private val secrets = listOf(
        Regex("(?i)\\bBearer\\s+[A-Za-z0-9._~+/=-]+"),
        Regex("(?i)\\b(?:api[_ -]?key|access[_ -]?token|refresh[_ -]?token|token|password)\\b\\s*[:=]\\s*[^\\s,;]+"),
        Regex("(?i)https?://\\S+[?&](?:token|key|code|auth|password)=\\S+"),
        Regex("(?i)\\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,}\\b"),
        Regex("(?<!\\w)(?:\\+?\\d[\\d ().-]{7,}\\d)(?!\\w)"),
        Regex("(?<!\\d)\\d{4,8}(?!\\d)")
    )

    fun sanitize(title: String, body: String, mode: AppContentMode): SafeNotificationText {
        if (mode == AppContentMode.OFF || mode == AppContentMode.APP_ONLY) return SafeNotificationText("", "")
        if (otpContext.containsMatchIn(title) || otpContext.containsMatchIn(body)) return SafeNotificationText("", "")
        val titleSafe = redact(title)
        if (mode == AppContentMode.TITLE_ONLY) return SafeNotificationText(utf8Field(titleSafe, 79), "")
        return SafeNotificationText(utf8Field(titleSafe, 79), utf8Field(redact(body), 180))
    }

    private fun redact(input: String): String {
        var safe = input
        for (pattern in secrets) safe = pattern.replace(safe, "[redacted]")
        return safe
    }

    private fun utf8Field(value: String, maxBytes: Int): String {
        val bytes = value.toByteArray(Charsets.UTF_8)
        var cut = minOf(bytes.size, maxBytes)
        if (cut < bytes.size) while (cut > 0 && (bytes[cut].toInt() and 0xC0) == 0x80) cut--
        return String(bytes, 0, cut, Charsets.UTF_8).map { if (it.code < 32 || it.code == 127) ' ' else it }.joinToString("")
    }
}
