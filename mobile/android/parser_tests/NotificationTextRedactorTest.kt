package com.robodesk.phonebridge

private fun hasOnlyPairedSurrogates(value: String): Boolean {
    var index = 0
    while (index < value.length) {
        val ch = value[index]
        if (Character.isHighSurrogate(ch)) {
            if (index + 1 >= value.length || !Character.isLowSurrogate(value[index + 1])) return false
            index += 2
        } else {
            if (Character.isLowSurrogate(ch)) return false
            index++
        }
    }
    return true
}

fun main() {
    check(NotificationTextRedactor.sanitize("title", "secret", AppContentMode.OFF) == SafeNotificationText("", ""))
    check(NotificationTextRedactor.sanitize("title", "secret", AppContentMode.APP_ONLY) == SafeNotificationText("", ""))

    val title = NotificationTextRedactor.sanitize("Mail from alice@example.com", "private body", AppContentMode.TITLE_ONLY)
    check("alice@example.com" !in title.title && title.snippet.isEmpty())

    val otp = NotificationTextRedactor.sanitize("Your code", "Your verification code is 482913", AppContentMode.SNIPPET)
    check(otp == SafeNotificationText("", ""))
    val otpInTitle = NotificationTextRedactor.sanitize("Security code 482913", "Your message", AppContentMode.SNIPPET)
    check(otpInTitle == SafeNotificationText("", ""))

    val redacted = NotificationTextRedactor.sanitize(
        "Bearer abc.def_123 and token: tok_456",
        "Call +1 (415) 555-0132 or open https://example.test/?auth=secret and email bob@example.org",
        AppContentMode.SNIPPET
    )
    for (secret in listOf("abc.def_123", "tok_456", "415", "auth=secret", "bob@example.org")) {
        check(secret !in redacted.title && secret !in redacted.snippet) { "unredacted secret: $secret" }
    }

    val bounded = NotificationTextRedactor.sanitize("🙂".repeat(80), "🚗".repeat(100), AppContentMode.SNIPPET)
    check(bounded.title.toByteArray(Charsets.UTF_8).size <= 79)
    check(bounded.snippet.toByteArray(Charsets.UTF_8).size <= 180)
    check(hasOnlyPairedSurrogates(bounded.title) && hasOnlyPairedSurrogates(bounded.snippet))

    val controls = NotificationTextRedactor.sanitize("line\nfeed", "safe", AppContentMode.SNIPPET)
    check('\n' !in controls.title)
    bridgeAuthenticatedEnvelopeTest()
    bridgeGattFramingTest()
    bridgeSessionHandshakeTest()
    bridgeFakeGattFlowTest()
    verifyBridgeCompanionCommands()
    println("PASS: local privacy modes, OTP blocking, secret redaction, UTF-8 bounds and control filtering")
    println("PASS: Android companion command IDs and bounded wire encoding")
    println("PASS: authenticated envelope canonical bounds, MAC, direction, session and replay counters")
}
