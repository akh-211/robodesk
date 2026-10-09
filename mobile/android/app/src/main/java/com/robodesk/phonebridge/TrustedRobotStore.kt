package com.robodesk.phonebridge

import android.companion.CompanionDeviceManager
import android.content.Context
import android.os.Build

/** Local selection metadata only. This does not by itself authenticate a robot. */
object TrustedRobotStore {
    private const val PREFS = "trusted_robot"
    private const val ADDRESS = "address"
    private const val ASSOCIATION_ID = "association_id"

    fun address(context: Context): String? {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val stored = prefs.getString(ADDRESS, null)?.takeIf { it.isNotBlank() } ?: return null
        return try {
            val manager = context.getSystemService(Context.COMPANION_DEVICE_SERVICE) as? CompanionDeviceManager ?: return stored
            val id = prefs.getInt(ASSOCIATION_ID, -1)
            val current = if (Build.VERSION.SDK_INT >= 33 && id >= 0) currentAddressApi33(manager, id)
                else currentAddressLegacy(manager, stored)
            if (!current.isNullOrBlank() && current != stored) prefs.edit().putString(ADDRESS, current.uppercase()).commit()
            current?.takeIf { it.isNotBlank() } ?: stored
        } catch (_: Exception) {
            stored
        }
    }

    fun save(context: Context, address: String, associationId: Int? = null): Boolean {
        require(address.matches(Regex("(?i)^[0-9a-f]{2}(:[0-9a-f]{2}){5}$")))
        val prefs=context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val normalized=address.uppercase()
        val previous=prefs.getString(ADDRESS,null)
        if(!previous.isNullOrBlank()&&!previous.equals(normalized,true)&&!BridgeCredentialStore(context).clear())return false
        val saved=prefs.edit()
            .putString(ADDRESS, address.uppercase())
            .putInt(ASSOCIATION_ID, associationId ?: -1)
            .commit()
        return saved&&prefs.getString(ADDRESS,null)==normalized
    }

    fun associationId(context: Context): Int? = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        .getInt(ASSOCIATION_ID, -1).takeIf { it >= 0 }

    fun forget(context: Context): Boolean {
        val address = address(context)
        val associationId = associationId(context)
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val credentialCleared = BridgeCredentialStore(context).clear()
        val selectionCleared = prefs.edit().clear().commit() && prefs.all.isEmpty()
        val manager = context.getSystemService(Context.COMPANION_DEVICE_SERVICE) as? CompanionDeviceManager
        var associationCleared = manager!=null||(associationId==null&&address==null)
        try {
            if (associationId != null && android.os.Build.VERSION.SDK_INT >= 33) {
                if(manager!=null)manager.disassociate(associationId)
            } else if (address != null) {
                @Suppress("DEPRECATION")
                if(manager!=null)manager.disassociate(address)
            }
        } catch (_: Exception) {
            associationCleared = false
        }
        return credentialCleared && selectionCleared && associationCleared
    }

    @android.annotation.TargetApi(33)
    private fun currentAddressApi33(manager: CompanionDeviceManager, id: Int): String? =
        manager.myAssociations.firstOrNull { it.id == id }?.deviceMacAddress?.toString()

    @Suppress("DEPRECATION")
    private fun currentAddressLegacy(manager: CompanionDeviceManager, stored: String): String? {
        val addresses = manager.associations
        return when {
            stored in addresses -> stored
            addresses.size == 1 -> addresses.first()
            else -> null
        }
    }
}
