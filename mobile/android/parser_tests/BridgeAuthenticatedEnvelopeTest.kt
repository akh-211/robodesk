package com.robodesk.phonebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

private const val TEST_KIND = 3
private const val TEST_SESSION = 0x78563412L

private fun testPacket(
    key: ByteArray,
    direction: Int = 1,
    kind: Int = TEST_KIND,
    session: Long = TEST_SESSION,
    counter: Long = 1,
    ageMs: Long = 0,
    payload: ByteArray = byteArrayOf(42)
): ByteArray {
    val prefix = ByteBuffer.allocate(22 + payload.size).order(ByteOrder.LITTLE_ENDIAN)
        .put(byteArrayOf(82, 68, 66, 50, 2, direction.toByte(), kind.toByte(), 0))
        .putInt(session.toInt()).putInt(counter.toInt()).putInt(ageMs.toInt())
        .putShort(payload.size.toShort()).put(payload).array()
    val mac = Mac.getInstance("HmacSHA256").apply {
        init(SecretKeySpec(key, "HmacSHA256"))
    }.doFinal("RoboDesk-Bridge-Envelope-v2\u0000".toByteArray(Charsets.US_ASCII) + prefix)
    return prefix + mac
}

private fun ByteArray.toHex() = joinToString("") { "%02x".format(it.toInt() and 0xff) }

fun bridgeAuthenticatedEnvelopeTest() {
    val key = ByteArray(32) { it.toByte() }
    val payload = "NAV\nleft\nMain Street".toByteArray(Charsets.UTF_8)
    val sender = BridgeAuthenticatedEnvelope.SessionSender(
        key, TEST_SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT
    )
    val encoded = requireNotNull(sender.create(TEST_KIND, 250, payload))
    check(encoded.toHex() == "52444232020103001234567801000000fa00000014004e41560a6c6566740a4d61696e2053747265657489ca1b0023daf933288b46256deff724ed85fbd7c0e9d9b55ff22e9b41dda76c")
    check(encoded.size == BridgeAuthenticatedEnvelope.HEADER_SIZE + payload.size + BridgeAuthenticatedEnvelope.MAC_SIZE)
    val receiver = BridgeAuthenticatedEnvelope.SessionReceiver(
        key, TEST_SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(TEST_KIND to 500L)
    )
    val decoded = requireNotNull(receiver.accept(encoded))
    check(decoded.kind == TEST_KIND && decoded.counter == 1L && decoded.ageMs == 250L)
    check(decoded.payload.contentEquals(payload))
    check(receiver.accept(encoded) == null) // same session counter cannot be accepted twice

    check(BridgeAuthenticatedEnvelope.SessionReceiver(
        key, TEST_SESSION, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE, mapOf(TEST_KIND to 500L)
    ).accept(encoded) == null)
    check(BridgeAuthenticatedEnvelope.SessionReceiver(
        key, TEST_SESSION + 1, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(TEST_KIND to 500L)
    ).accept(encoded) == null)
    check(BridgeAuthenticatedEnvelope.SessionReceiver(
        ByteArray(32) { 99 }, TEST_SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(TEST_KIND to 500L)
    ).accept(encoded) == null)

    // Each canonical header field and every tag byte are authenticated.
    val changedOffsets = (0..22).toList() + (encoded.size - 32 until encoded.size).toList()
    for (offset in changedOffsets) {
        val changed = encoded.copyOf().also { it[offset] = (it[offset].toInt() xor 1).toByte() }
        check(BridgeAuthenticatedEnvelope.SessionReceiver(
            key, TEST_SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(TEST_KIND to 500L)
        ).accept(changed) == null) { "accepted tampered byte at $offset" }
    }
    check(BridgeAuthenticatedEnvelope.SessionReceiver(
        key, TEST_SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(TEST_KIND to 500L)
    ).accept(encoded + byteArrayOf(0)) == null)

    // Well-formed but undefined kinds and over-age packets are rejected after valid MAC verification.
    check(BridgeAuthenticatedEnvelope.SessionReceiver(
        key, TEST_SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(TEST_KIND to 500L)
    ).accept(testPacket(key, kind = 0)) == null)
    check(BridgeAuthenticatedEnvelope.SessionReceiver(
        key, TEST_SESSION, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(TEST_KIND to 500L)
    ).accept(testPacket(key, ageMs = 501)) == null)

    val maxAge = 0xffff_ffffL
    val boundary = BridgeAuthenticatedEnvelope.SessionSender(
        key, maxAge, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT
    ).create(255, maxAge, ByteArray(500) { (it and 0xff).toByte() })
    val full = requireNotNull(boundary)
    check(full.size == BridgeAuthenticatedEnvelope.MAX_ENVELOPE)
    check(BridgeAuthenticatedEnvelope.SessionReceiver(
        key, maxAge, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(255 to maxAge)
    ).accept(full)!!.payload.size == 500)

    val closedSender = BridgeAuthenticatedEnvelope.SessionSender(key, 10, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT)
    closedSender.close()
    check(closedSender.create(TEST_KIND, 0, byteArrayOf()) == null)
    val closedReceiver = BridgeAuthenticatedEnvelope.SessionReceiver(
        key, 10, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, mapOf(TEST_KIND to 100L)
    )
    closedReceiver.close()
    check(closedReceiver.accept(testPacket(key, session = 10)) == null)
}
