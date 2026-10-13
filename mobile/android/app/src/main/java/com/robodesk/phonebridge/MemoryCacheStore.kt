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

/**
 * Local copy of the robot's fact list, AES-GCM encrypted with an Android Keystore key and bound to the
 * selected robot. Robot data is untrusted display text; a failed read simply means "no cache".
 */
object MemoryCacheStore {
    class Cached(val revision: Long, val facts: List<BridgeSettingsProtocol.Fact>)

    private const val ANDROID_KEYSTORE = "AndroidKeyStore"
    private const val KEY_ALIAS = "com.robodesk.bridge.memory.aes.v1"
    private const val TRANSFORMATION = "AES/GCM/NoPadding"
    private const val PREFS = "bridge_secrets"
    private const val RECORD = "memory_cache_v1"
    private const val VERSION = "m1"

    @Synchronized
    fun save(context: Context, revision: Long, facts: List<BridgeSettingsProtocol.Fact>): Boolean {
        val address = TrustedRobotStore.address(context) ?: return false
        val plain = BridgeSettingsProtocol.encodeMemoryDocument(revision, facts) ?: return false
        return try {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, keyForWrite())
            cipher.updateAAD(aad(context, address))
            val encrypted = cipher.doFinal(plain)
            val record = listOf(VERSION, encode(cipher.iv), encode(encrypted)).joinToString("\n")
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(RECORD, record).commit()
        } catch (_: Exception) {
            false
        } finally {
            plain.fill(0)
        }
    }

    @Synchronized
    fun read(context: Context): Cached? {
        val address = TrustedRobotStore.address(context) ?: return null
        val record = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(RECORD, null) ?: return null
        return try {
            val fields = record.split('\n')
            if (fields.size != 3 || fields[0] != VERSION) return null
            val iv = Base64.decode(fields[1], Base64.NO_WRAP)
            if (iv.size != 12) return null
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, keyForRead(), GCMParameterSpec(128, iv))
            cipher.updateAAD(aad(context, address))
            val plain = cipher.doFinal(Base64.decode(fields[2], Base64.NO_WRAP))
            val window = BridgeSettingsProtocol.parseMemoryWindow(plain)
            plain.fill(0)
            window?.let { Cached(it.revision, it.facts) }
        } catch (_: Exception) {
            null
        }
    }

    // PD-3.4 mood diary: same Keystore key and robot binding, its own record and AAD label; on/off is a local flag.
    private const val DIARY = "mood_diary_v1"
    private const val DIARY_ON = "mood_diary_on"

    fun diaryEnabled(context: Context) = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(DIARY_ON, false)

    fun setDiaryEnabled(context: Context, on: Boolean) {
        val edit = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(DIARY_ON, on)
        if (!on) edit.remove(DIARY)
        edit.commit()
    }

    @Synchronized
    fun saveDiary(context: Context, entries: List<MoodDiary.Entry>): Boolean {
        val address = TrustedRobotStore.address(context) ?: return false
        val plain = MoodDiary.encode(entries).toByteArray(StandardCharsets.UTF_8)
        return try {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, keyForWrite())
            cipher.updateAAD(diaryAad(context, address))
            val encrypted = cipher.doFinal(plain)
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(DIARY, listOf(VERSION, encode(cipher.iv), encode(encrypted)).joinToString("\n")).commit()
        } catch (_: Exception) {
            false
        } finally {
            plain.fill(0)
        }
    }

    @Synchronized
    fun readDiary(context: Context): List<MoodDiary.Entry> {
        val address = TrustedRobotStore.address(context) ?: return emptyList()
        val record = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(DIARY, null) ?: return emptyList()
        return try {
            val fields = record.split('\n')
            if (fields.size != 3 || fields[0] != VERSION) return emptyList()
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, keyForRead(), GCMParameterSpec(128, Base64.decode(fields[1], Base64.NO_WRAP)))
            cipher.updateAAD(diaryAad(context, address))
            MoodDiary.decode(String(cipher.doFinal(Base64.decode(fields[2], Base64.NO_WRAP)), StandardCharsets.UTF_8))
        } catch (_: Exception) {
            emptyList()
        }
    }

    @Synchronized
    fun clearDiary(context: Context) {
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(DIARY).commit()
    }

    private fun diaryAad(context: Context, address: String) =
        "RoboDesk-Mood-Diary-v1\u0000${context.applicationContext.packageName}\u0000$address".toByteArray(StandardCharsets.UTF_8)

    @Synchronized
    fun clear(context: Context) {
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(RECORD).commit()
    }

    private fun keyForWrite(): SecretKey {
        val store = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        (store.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true)
                .build()
        )
        return generator.generateKey()
    }

    private fun keyForRead(): SecretKey {
        val store = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        return store.getKey(KEY_ALIAS, null) as? SecretKey ?: error("Memory cache key is missing")
    }

    private fun aad(context: Context, address: String) =
        "RoboDesk-Memory-Cache-v1\u0000${context.applicationContext.packageName}\u0000$address".toByteArray(StandardCharsets.UTF_8)

    private fun encode(bytes: ByteArray) = Base64.encodeToString(bytes, Base64.NO_WRAP)
}
