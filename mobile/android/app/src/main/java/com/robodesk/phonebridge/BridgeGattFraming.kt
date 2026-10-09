package com.robodesk.phonebridge

/** Bounded application framing used when a bridge message exceeds one ATT value. */
object BridgeGattFraming {
    const val HEADER_SIZE = 8
    const val MAX_MESSAGE = 600
    const val TIMEOUT_MS = 5_000L
    private const val VERSION = 1

    fun fragments(message: ByteArray, mtu: Int, messageId: Int): List<ByteArray>? {
        if (message.isEmpty() || message.size > MAX_MESSAGE || mtu <= 3 + HEADER_SIZE ||
            messageId !in 1..0xffff) return null
        val chunkCapacity = mtu - 3 - HEADER_SIZE
        val result = ArrayList<ByteArray>((message.size + chunkCapacity - 1) / chunkCapacity)
        var offset = 0
        while (offset < message.size) {
            val count = minOf(chunkCapacity, message.size - offset)
            val frame = ByteArray(HEADER_SIZE + count)
            frame[0] = VERSION.toByte()
            frame[1] = ((if (offset == 0) 1 else 0) or
                (if (offset + count == message.size) 2 else 0)).toByte()
            put16(frame, 2, messageId)
            put16(frame, 4, message.size)
            put16(frame, 6, offset)
            message.copyInto(frame, HEADER_SIZE, offset, offset + count)
            result += frame
            offset += count
        }
        return result
    }

    class Reassembler {
        private val bytes = ByteArray(MAX_MESSAGE)
        private var active = false
        private var messageId = 0
        private var total = 0
        private var nextOffset = 0
        private var lastAt = 0L

        @Synchronized
        fun accept(frame: ByteArray, nowMs: Long): ByteArray? {
            if (active && nowMs - lastAt > TIMEOUT_MS) reset()
            if (frame.size <= HEADER_SIZE || (frame[0].toInt() and 0xff) != VERSION) return fail()
            val flags = frame[1].toInt() and 0xff
            val id = read16(frame, 2)
            val messageSize = read16(frame, 4)
            val offset = read16(frame, 6)
            val count = frame.size - HEADER_SIZE
            val first = (flags and 1) != 0
            val last = (flags and 2) != 0
            if ((flags and 0xfc) != 0 || id == 0 || messageSize == 0 || messageSize > MAX_MESSAGE ||
                offset > messageSize || count > messageSize - offset) return fail()
            if (first) {
                if (active || offset != 0) return fail()
                active = true
                messageId = id
                total = messageSize
                nextOffset = 0
            }
            if (!active || id != messageId || messageSize != total || offset != nextOffset) return fail()
            frame.copyInto(bytes, offset, HEADER_SIZE, frame.size)
            nextOffset += count
            lastAt = nowMs
            if (!last) {
                if (nextOffset == total) return fail()
                return null
            }
            if (nextOffset != total) return fail()
            val message = bytes.copyOf(total)
            reset()
            return message
        }

        @Synchronized
        fun reset() {
            bytes.fill(0)
            active = false
            messageId = 0
            total = 0
            nextOffset = 0
            lastAt = 0L
        }

        private fun fail(): ByteArray? { reset(); return null }
    }

    private fun put16(bytes: ByteArray, offset: Int, value: Int) {
        bytes[offset] = value.toByte()
        bytes[offset + 1] = (value ushr 8).toByte()
    }

    private fun read16(bytes: ByteArray, offset: Int) =
        (bytes[offset].toInt() and 0xff) or ((bytes[offset + 1].toInt() and 0xff) shl 8)
}
