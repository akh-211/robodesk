package com.robodesk.phonebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/** In-memory peripheral exercises the production Android protocol/framing across a fake GATT link. */
internal fun bridgeFakeGattFlowTest() {
    val key = ByteArray(32) { (it * 3 + 7).toByte() }
    val robotId = ByteArray(16) { (it + 17).toByte() }
    val phoneNonce = ByteArray(32) { (it + 51).toByte() }
    val robotNonce = ByteArray(32) { (it + 91).toByte() }
    val sessionId = 0x10293847L
    fun le(value: Long) = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(value.toInt()).array()
    fun mac(domain: String, bytes: ByteArray) = Mac.getInstance("HmacSHA256")
        .run { init(SecretKeySpec(key, "HmacSHA256")); doFinal((domain + "\u0000").toByteArray(Charsets.US_ASCII) + bytes) }
    fun transcript() = robotId + phoneNonce + robotNonce + le(sessionId)
    fun linkSend(message: ByteArray, id: Int, receive: BridgeGattFraming.Reassembler): ByteArray {
        var complete: ByteArray? = null
        val frames = BridgeGattFraming.fragments(message, 23, id)!!
        frames.forEachIndexed { index, frame ->
            val packet = receive.accept(frame, index * 15L)
            if (index == frames.lastIndex) complete = packet else check(packet == null)
        }
        return requireNotNull(complete)
    }

    val phoneControlRx = BridgeGattFraming.Reassembler()
    val robotControlRx = BridgeGattFraming.Reassembler()
    val hello = BridgeSessionHandshake.hello(phoneNonce)!!
    check(linkSend(hello, 1, robotControlRx).contentEquals(hello))
    val serverChallenge = byteArrayOf(2, BridgeSessionHandshake.ENROLLMENT_CHALLENGE.toByte()) +
        robotId + le(sessionId) + robotNonce + key + mac("RoboDesk-Bridge-ServerProof-v2", transcript())
    val challengeWire = linkSend(serverChallenge, 2, phoneControlRx)
    val challenge = requireNotNull(BridgeSessionHandshake.parseChallenge(challengeWire, robotId, phoneNonce, null))
    check(challenge.enrollment && challenge.key.contentEquals(key))

    val finish = BridgeSessionHandshake.finish(challenge)
    val receivedFinish = linkSend(finish, 3, robotControlRx)
    val expectedFinish = mac("RoboDesk-Bridge-ClientProof-v2", transcript())
    check(receivedFinish.size == 38 && receivedFinish.copyOfRange(6, 38).contentEquals(expectedFinish))
    val accepted = byteArrayOf(2, BridgeSessionHandshake.ACCEPTED.toByte()) + le(sessionId) +
        mac("RoboDesk-Bridge-SessionAck-v2", transcript())
    check(BridgeSessionHandshake.verifyAccepted(challenge, linkSend(accepted, 4, phoneControlRx)))

    val limits = mapOf(1 to 300_000L, 2 to 300_000L, 3 to 120_000L, 4 to 0L, 0x7f to 20_000L)
    val sender = BridgeAuthenticatedEnvelope.SessionSender(key, sessionId, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT)
    val receiver = BridgeAuthenticatedEnvelope.SessionReceiver(key, sessionId, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, limits)
    val notification = requireNotNull(sender.create(1, 350, "key\ncom.example\nExample\nTitle\nSafe".toByteArray()))
    val receivedNotification = linkSend(notification, 5, BridgeGattFraming.Reassembler())
    check(receiver.accept(receivedNotification)?.payload?.toString(Charsets.UTF_8)?.endsWith("Safe") == true)
    check(receiver.accept(receivedNotification) == null)
    val revoke = requireNotNull(sender.create(4, 0, byteArrayOf()))
    check(receiver.accept(linkSend(revoke, 6, BridgeGattFraming.Reassembler()))?.kind == 4)

    val robotAckSender = BridgeAuthenticatedEnvelope.SessionSender(key, sessionId, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE)
    val phoneAckRx = BridgeAuthenticatedEnvelope.SessionReceiver(key, sessionId, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE, limits)
    val acceptedPayload = byteArrayOf(1, 0, 0, 0, 0, 0)
    val ack = requireNotNull(robotAckSender.create(0x7f, 0, acceptedPayload))
    val ackMessage = phoneAckRx.accept(linkSend(ack, 7, BridgeGattFraming.Reassembler()))
    check(ackMessage?.payload?.contentEquals(acceptedPayload) == true)

    val changedId = robotId.copyOf().also { it[0] = (it[0].toInt() xor 1).toByte() }
    check(BridgeSessionHandshake.parseChallenge(serverChallenge, changedId, phoneNonce, null) == null)
    val badKey = BridgeAuthenticatedEnvelope.SessionReceiver(ByteArray(32), sessionId,
        BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, limits)
    check(badKey.accept(notification) == null)
    val corrupt = notification.copyOf().also { it[it.lastIndex] = (it.last().toInt() xor 1).toByte() }
    check(BridgeAuthenticatedEnvelope.SessionReceiver(key, sessionId,
        BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT, limits).accept(corrupt) == null)
    println("PASS: fake GATT enrollment, authenticated notification/revoke, ACK, replay and wrong identity/key")
}
