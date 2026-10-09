package com.robodesk.phonebridge

internal fun bridgeGattFramingTest() {
    val message = ByteArray(554) { (it * 37).toByte() }
    val frames = BridgeGattFraming.fragments(message, mtu = 23, messageId = 19)!!
    check(frames.size > 40)
    check(frames.all { it.size <= 20 })
    val receiver = BridgeGattFraming.Reassembler()
    var result: ByteArray? = null
    frames.forEachIndexed { index, frame ->
        val received = receiver.accept(frame, index * 10L)
        if (index == frames.lastIndex) result = received else check(received == null)
    }
    check(result!!.contentEquals(message))
    check(BridgeGattFraming.fragments(ByteArray(601), 23, 1) == null)
    check(BridgeGattFraming.fragments(message, 23, 0) == null)

    val pair = BridgeGattFraming.fragments(message, 23, 20)!!
    check(receiver.accept(pair.first(), 100L) == null)
    check(receiver.accept(pair[2], 110L) == null)
    check(receiver.accept(pair[1], 120L) == null) // Invalid order resets the frame.
    check(receiver.accept(pair.last(), 130L) == null)

    val timeout = BridgeGattFraming.Reassembler()
    check(timeout.accept(pair.first(), 1_000L) == null)
    check(timeout.accept(pair[1], 1_000L + BridgeGattFraming.TIMEOUT_MS + 1L) == null)
    println("PASS: GATT framing handles MTU 23, bounds, order and timeout")
}
