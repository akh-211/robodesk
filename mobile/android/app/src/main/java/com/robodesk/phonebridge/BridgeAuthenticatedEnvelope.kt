package com.robodesk.phonebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/** Canonical v2 authenticated payload format shared with the C3 gateway. */
object BridgeAuthenticatedEnvelope {
    const val MAX_PAYLOAD = 500
    const val MAC_SIZE = 32
    const val HEADER_SIZE = 22
    const val MAX_ENVELOPE = HEADER_SIZE + MAX_PAYLOAD + MAC_SIZE

    enum class Direction(val wire: Int) {
        PHONE_TO_ROBOT(1), ROBOT_TO_PHONE(2)
    }

    data class Message(
        val direction: Direction,
        val kind: Int,
        val sessionId: Long,
        val counter: Long,
        val ageMs: Long,
        val payload: ByteArray
    )

    /** Creates one direction-scoped sender. A counter is consumed before each packet is returned. */
    class SessionSender(key: ByteArray, private val sessionId: Long, private val direction: Direction) {
        private val key = key.copyOf()
        private var counter = 0L
        private var closed = false

        init { require(validKey(key) && sessionId in 1..UINT32_MAX) }

        @Synchronized
        fun create(kind: Int, ageMs: Long, payload: ByteArray): ByteArray? {
            if (closed || counter >= UINT32_MAX || kind !in 1..255 || ageMs !in 0..UINT32_MAX || payload.size > MAX_PAYLOAD) return null
            counter += 1
            return encode(this.key, Message(direction, kind, sessionId, counter, ageMs, payload.copyOf()))
        }

        @Synchronized
        fun close() {
            key.fill(0)
            closed = true
        }
    }

    /** Owns replay state for one authenticated session and rejects expired/unknown message kinds. */
    class SessionReceiver(
        key: ByteArray,
        private val sessionId: Long,
        private val direction: Direction,
        maxAgeMsByKind: Map<Int, Long>
    ) {
        private val key = key.copyOf()
        private val ageLimits = maxAgeMsByKind.toMap()
        private var lastCounter = 0L
        private var closed = false

        init {
            require(validKey(key) && sessionId in 1..UINT32_MAX)
            require(ageLimits.isNotEmpty() && ageLimits.all { (kind, age) -> kind in 1..255 && age in 0..UINT32_MAX })
        }

        @Synchronized
        fun accept(bytes: ByteArray): Message? {
            if (closed) return null
            val message = decode(key, bytes, direction, sessionId, lastCounter, ageLimits) ?: return null
            // Reserve before the caller can dispatch. Synchronized accept prevents concurrent replay races.
            lastCounter = message.counter
            return message
        }

        @Synchronized
        fun close() {
            key.fill(0)
            lastCounter = UINT32_MAX
            closed = true
        }
    }

    private fun encode(key: ByteArray, message: Message): ByteArray? {
        if (!validKey(key) || !validFields(message)) return null
        val prefix = ByteBuffer.allocate(HEADER_SIZE + message.payload.size).order(ByteOrder.LITTLE_ENDIAN)
            .put(MAGIC)
            .put(VERSION)
            .put(message.direction.wire.toByte())
            .put(message.kind.toByte())
            .put(0) // reserved, must remain zero for canonical encoding
            .putInt(message.sessionId.toInt())
            .putInt(message.counter.toInt())
            .putInt(message.ageMs.toInt())
            .putShort(message.payload.size.toShort())
            .put(message.payload)
            .array()
        val tag = hmac(key, DOMAIN + prefix)
        return prefix + tag
    }

    /** Verify tag, session, freshness and counter before returning payload to the session receiver. */
    private fun decode(
        key: ByteArray,
        bytes: ByteArray,
        expectedDirection: Direction,
        expectedSessionId: Long,
        lastAcceptedCounter: Long,
        ageLimits: Map<Int, Long>
    ): Message? {
        if (!validKey(key) || bytes.size < HEADER_SIZE + MAC_SIZE || bytes.size > MAX_ENVELOPE) return null
        val input = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val magic = ByteArray(MAGIC.size)
        input.get(magic)
        val version = input.get()
        val directionWire = input.get().toInt() and 0xff
        val kind = input.get().toInt() and 0xff
        val reserved = input.get().toInt() and 0xff
        val sessionId = input.int.toLong() and UINT32_MAX
        val counter = input.int.toLong() and UINT32_MAX
        val ageMs = input.int.toLong() and UINT32_MAX
        val payloadLength = input.short.toInt() and 0xffff
        if (!magic.contentEquals(MAGIC) || version != VERSION || reserved != 0 ||
            directionWire != expectedDirection.wire || sessionId != expectedSessionId ||
            kind == 0 || counter == 0L || counter <= lastAcceptedCounter || payloadLength > MAX_PAYLOAD ||
            ageLimits[kind]?.let { ageMs > it } != false ||
            bytes.size != HEADER_SIZE + payloadLength + MAC_SIZE
        ) return null

        val prefixLength = HEADER_SIZE + payloadLength
        val expectedTag = hmac(key, DOMAIN + bytes.copyOfRange(0, prefixLength))
        val receivedTag = bytes.copyOfRange(prefixLength, bytes.size)
        if (!java.security.MessageDigest.isEqual(expectedTag, receivedTag)) return null
        val payload = ByteArray(payloadLength)
        input.position(HEADER_SIZE)
        input.get(payload)
        return Message(expectedDirection, kind, sessionId, counter, ageMs, payload)
    }

    private fun validKey(key: ByteArray) = key.size == 32
    private fun validFields(message: Message) =
        message.kind in 1..255 && message.sessionId in 1..UINT32_MAX &&
            message.counter in 1..UINT32_MAX && message.ageMs in 0..UINT32_MAX &&
            message.payload.size <= MAX_PAYLOAD

    private fun hmac(key: ByteArray, value: ByteArray): ByteArray = Mac.getInstance("HmacSHA256")
        .run { init(SecretKeySpec(key, "HmacSHA256")); doFinal(value) }

    private val MAGIC = byteArrayOf('R'.code.toByte(), 'D'.code.toByte(), 'B'.code.toByte(), '2'.code.toByte())
    private val VERSION: Byte = 2
    private val DOMAIN = "RoboDesk-Bridge-Envelope-v2\u0000".toByteArray(Charsets.US_ASCII)
    private const val UINT32_MAX = 0xffff_ffffL
}
