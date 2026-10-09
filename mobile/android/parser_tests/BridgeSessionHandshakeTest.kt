package com.robodesk.phonebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

internal fun bridgeSessionHandshakeTest() {
    val key = ByteArray(32) { it.toByte() }
    val robotId = ByteArray(16) { (it + 3).toByte() }
    val phoneNonce = ByteArray(32) { (it + 40).toByte() }
    val robotNonce = ByteArray(32) { (it + 80).toByte() }
    val sessionId = 0x78563412L
    val transcript = robotId + phoneNonce + robotNonce + ByteBuffer.allocate(4)
        .order(ByteOrder.LITTLE_ENDIAN).putInt(sessionId.toInt()).array()
    fun proof(domain: String) = Mac.getInstance("HmacSHA256")
        .run { init(SecretKeySpec(key, "HmacSHA256")); doFinal((domain + "\u0000").toByteArray(Charsets.US_ASCII) + transcript) }

    val activeChallenge = byteArrayOf(2, BridgeSessionHandshake.CHALLENGE.toByte()) + robotId +
        ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(sessionId.toInt()).array() + robotNonce +
        proof("RoboDesk-Bridge-ServerProof-v2")
    val parsed = requireNotNull(BridgeSessionHandshake.parseChallenge(activeChallenge, robotId, phoneNonce, key))
    check(!parsed.enrollment && parsed.sessionId == sessionId)
    val finish = BridgeSessionHandshake.finish(parsed)
    check(finish.size == 38 && (finish[1].toInt() and 0xff) == BridgeSessionHandshake.FINISH)
    val ack = byteArrayOf(2, BridgeSessionHandshake.ACCEPTED.toByte()) +
        ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(sessionId.toInt()).array() +
        proof("RoboDesk-Bridge-SessionAck-v2")
    check(BridgeSessionHandshake.verifyAccepted(parsed, ack))
    check(!BridgeSessionHandshake.verifyAccepted(parsed, ack.copyOf().also { it[it.lastIndex] = (it.last().toInt() xor 1).toByte() }))
    check(BridgeSessionHandshake.parseChallenge(activeChallenge, robotId, phoneNonce, ByteArray(32)) == null)
    check(BridgeSessionHandshake.parseChallenge(activeChallenge, ByteArray(16), phoneNonce, key) == null)

    val enrollmentChallenge = byteArrayOf(2, BridgeSessionHandshake.ENROLLMENT_CHALLENGE.toByte()) + robotId +
        ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(sessionId.toInt()).array() + robotNonce + key +
        proof("RoboDesk-Bridge-ServerProof-v2")
    val enrolled = requireNotNull(BridgeSessionHandshake.parseChallenge(enrollmentChallenge, robotId, phoneNonce, null))
    check(enrolled.enrollment && enrolled.key.contentEquals(key))
    val altered = enrollmentChallenge.copyOf().also { it[30] = (it[30].toInt() xor 1).toByte() }
    check(BridgeSessionHandshake.parseChallenge(altered, robotId, phoneNonce, null) == null)
    println("PASS: v2 bridge handshake binds robot identity, both nonces, session and provisioning key")
}
