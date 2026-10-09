package com.robodesk.phonebridge

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.nio.charset.StandardCharsets
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Stores the bridge PSK encrypted at rest; the AES key never leaves Android Keystore. */
class BridgeCredentialStore(context: Context) {
    sealed class ReadResult {
        data object Missing : ReadResult()
        data class Available(val robotId: String, val key: ByteArray, val pending: Boolean) : ReadResult()
        data object RePairRequired : ReadResult()
    }

    private val app = context.applicationContext
    private val prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    @Synchronized
    fun savePending(robotId: String, bridgeKey: ByteArray): Boolean = saveRecord(robotId, bridgeKey, PENDING)

    @Synchronized
    fun markActive(robotId: String): Boolean {
        val current = read()
        if (current !is ReadResult.Available || current.robotId != robotId) return false
        if (!current.pending) { current.key.fill(0); return true }
        val ok = saveRecord(robotId, current.key, ACTIVE)
        current.key.fill(0)
        return ok
    }

    @Synchronized
    private fun saveRecord(robotId: String, bridgeKey: ByteArray, state: String): Boolean {
        if (robotId.isBlank() || robotId.length > 64 || bridgeKey.size != 32) return false
        if (state != PENDING && state != ACTIVE) return false
        return try {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, keyForWrite())
            cipher.updateAAD(aad(robotId, state))
            val encrypted = cipher.doFinal(bridgeKey)
            val record = listOf(
                VERSION,
                state,
                encode(robotId.toByteArray(StandardCharsets.UTF_8)),
                encode(cipher.iv),
                encode(encrypted)
            ).joinToString("\n")
            prefs.edit().putString(RECORD, record).commit()
        } catch (_: Exception) {
            false
        }
    }

    @Synchronized
    fun read(): ReadResult {
        val record = prefs.getString(RECORD, null) ?: return ReadResult.Missing
        return try {
            val fields = record.split('\n')
            if (fields.size != 5 || fields[0] != VERSION || (fields[1] != PENDING && fields[1] != ACTIVE)) return ReadResult.RePairRequired
            val state = fields[1]
            val robotId = String(decode(fields[2]), StandardCharsets.UTF_8)
            if (robotId.isBlank() || robotId.length > 64) return ReadResult.RePairRequired
            val iv = decode(fields[3])
            val ciphertext = decode(fields[4])
            if (iv.size != 12 || ciphertext.size != 48) return ReadResult.RePairRequired
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, keyForRead(), GCMParameterSpec(128, iv))
            cipher.updateAAD(aad(robotId, state))
            val key = cipher.doFinal(ciphertext)
            if (key.size != 32) ReadResult.RePairRequired else ReadResult.Available(robotId, key, state == PENDING)
        } catch (_: Exception) {
            ReadResult.RePairRequired
        }
    }

    @Synchronized
    fun clear(): Boolean {
        val removed = prefs.edit().remove(RECORD).commit() && prefs.getString(RECORD, null) == null
        try {
            val store = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
            store.deleteEntry(KEY_ALIAS)
        } catch (_: Exception) {
            return false
        }
        return removed
    }

    private fun keyForWrite(): SecretKey {
        val store = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        (store.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
            ).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true)
                .build()
        )
        return generator.generateKey()
    }

    private fun keyForRead(): SecretKey {
        val store = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        return store.getKey(KEY_ALIAS, null) as? SecretKey ?: error("Credential key is missing")
    }

    private fun aad(robotId: String, state: String) = "RoboDesk-Bridge-Credential-v2\u0000${app.packageName}\u0000$robotId\u0000$state"
        .toByteArray(StandardCharsets.UTF_8)

    private fun encode(bytes: ByteArray) = Base64.encodeToString(bytes, Base64.NO_WRAP)
    private fun decode(value: String) = Base64.decode(value, Base64.NO_WRAP)

    private companion object {
        const val ANDROID_KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "com.robodesk.bridge.credential.aes.v1"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val PREFS = "bridge_secrets"
        const val RECORD = "credential_v1"
        const val VERSION = "v2"
        const val PENDING = "pending"
        const val ACTIVE = "active"
    }
}
