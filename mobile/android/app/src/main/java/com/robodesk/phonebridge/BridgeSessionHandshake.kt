package com.robodesk.phonebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/** Canonical v2 control handshake. BLE MITM pairing protects first-time PSK delivery. */
object BridgeSessionHandshake {
    const val VERSION = 2
    const val HELLO = 0x01
    const val FINISH = 0x02
    const val CHALLENGE = 0x81
    const val ENROLLMENT_CHALLENGE = 0x82
    const val ACCEPTED = 0x83
    const val ROBOT_ID_SIZE = 16
    const val NONCE_SIZE = 32
    const val KEY_SIZE = 32
    private const val SESSION_SIZE = 4
    private const val PROOF_SIZE = 32

    data class Challenge(
        val robotId: ByteArray,
        val sessionId: Long,
        val phoneNonce: ByteArray,
        val robotNonce: ByteArray,
        val key: ByteArray,
        val enrollment: Boolean
    )

    fun hello(phoneNonce: ByteArray): ByteArray? =
        if (phoneNonce.size != NONCE_SIZE) null else byteArrayOf(VERSION.toByte(), HELLO.toByte()) + phoneNonce

    /** Validates identity, transcript proof and optional newly provisioned key. */
    fun parseChallenge(
        frame: ByteArray,
        expectedRobotId: ByteArray,
        phoneNonce: ByteArray,
        existingKey: ByteArray?
    ): Challenge? {
        if (expectedRobotId.size != ROBOT_ID_SIZE || phoneNonce.size != NONCE_SIZE || frame.size < 2 ||
            (frame[0].toInt() and 0xff) != VERSION) return null
        val op = frame[1].toInt() and 0xff
        val enrollment = op == ENROLLMENT_CHALLENGE
        if (!enrollment && op != CHALLENGE) return null
        val expectedSize = 2 + ROBOT_ID_SIZE + SESSION_SIZE + NONCE_SIZE +
            (if (enrollment) KEY_SIZE else 0) + PROOF_SIZE
        if (frame.size != expectedSize) return null
        var offset = 2
        val robotId = frame.copyOfRange(offset, offset + ROBOT_ID_SIZE); offset += ROBOT_ID_SIZE
        if (!MessageDigest.isEqual(expectedRobotId, robotId)) return null
        val sessionId = ByteBuffer.wrap(frame, offset, SESSION_SIZE).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and UINT32_MAX
        offset += SESSION_SIZE
        if (sessionId == 0L) return null
        val robotNonce = frame.copyOfRange(offset, offset + NONCE_SIZE); offset += NONCE_SIZE
        val key = if (enrollment) frame.copyOfRange(offset, offset + KEY_SIZE).also { offset += KEY_SIZE }
            else existingKey?.copyOf() ?: return null
        if (key.size != KEY_SIZE) return null
        val transcript = transcript(robotId, phoneNonce, robotNonce, sessionId)
        val expectedProof = hmac(key, SERVER_DOMAIN + transcript)
        val suppliedProof = frame.copyOfRange(offset, offset + PROOF_SIZE)
        if (!MessageDigest.isEqual(expectedProof, suppliedProof)) {
            key.fill(0)
            return null
        }
        return Challenge(robotId, sessionId, phoneNonce.copyOf(), robotNonce, key, enrollment)
    }

    fun finish(challenge: Challenge): ByteArray {
        val proof = hmac(challenge.key, CLIENT_DOMAIN + transcript(challenge))
        return byteArrayOf(VERSION.toByte(), FINISH.toByte()) + littleEndian(challenge.sessionId) + proof
    }

    fun verifyAccepted(challenge: Challenge, frame: ByteArray): Boolean {
        if (frame.size != 2 + SESSION_SIZE + PROOF_SIZE ||
            (frame[0].toInt() and 0xff) != VERSION || (frame[1].toInt() and 0xff) != ACCEPTED) return false
        val sessionId = ByteBuffer.wrap(frame, 2, SESSION_SIZE).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and UINT32_MAX
        if (sessionId != challenge.sessionId) return false
        val expected = hmac(challenge.key, ACK_DOMAIN + transcript(challenge))
        return MessageDigest.isEqual(expected, frame.copyOfRange(2 + SESSION_SIZE, frame.size))
    }

    private fun transcript(challenge: Challenge) = transcript(
        challenge.robotId, challenge.phoneNonce, challenge.robotNonce, challenge.sessionId
    )

    private fun transcript(robotId: ByteArray, phoneNonce: ByteArray, robotNonce: ByteArray, sessionId: Long) =
        robotId + phoneNonce + robotNonce + littleEndian(sessionId)

    private fun littleEndian(value: Long) = ByteBuffer.allocate(SESSION_SIZE)
        .order(ByteOrder.LITTLE_ENDIAN).putInt(value.toInt()).array()

    private fun hmac(key: ByteArray, value: ByteArray): ByteArray = Mac.getInstance("HmacSHA256")
        .run { init(SecretKeySpec(key, "HmacSHA256")); doFinal(value) }

    private val SERVER_DOMAIN = "RoboDesk-Bridge-ServerProof-v2\u0000".toByteArray(Charsets.US_ASCII)
    private val CLIENT_DOMAIN = "RoboDesk-Bridge-ClientProof-v2\u0000".toByteArray(Charsets.US_ASCII)
    private val ACK_DOMAIN = "RoboDesk-Bridge-SessionAck-v2\u0000".toByteArray(Charsets.US_ASCII)
    private const val UINT32_MAX = 0xffff_ffffL
}
