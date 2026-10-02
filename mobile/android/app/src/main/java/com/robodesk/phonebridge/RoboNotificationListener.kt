package com.robodesk.phonebridge

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanSettings
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import java.nio.charset.StandardCharsets
import java.util.UUID

class RoboNotificationListener : NotificationListenerService() {
    companion object {
        private val SERVICE = UUID.fromString("93de0001-2c7d-4a52-9f1c-6f4b4f424c45")
        private val RX = UUID.fromString("93de0002-2c7d-4a52-9f1c-6f4b4f424c45")
        private val ALLOWLIST = UUID.fromString("93de0003-2c7d-4a52-9f1c-6f4b4f424c45")
    }
    private var gatt: BluetoothGatt? = null
    private var characteristic: BluetoothGattCharacteristic? = null
    @Volatile private var allowedPackages: Set<String> = emptySet()
    private val mainHandler=Handler(Looper.getMainLooper())
    @Volatile private var inFlight:ByteArray?=null
    private val pending = ArrayDeque<ByteArray>()
    private var scanning = false

    override fun onListenerConnected() { super.onListenerConnected(); ensureConnected() }
    override fun onListenerDisconnected() { disconnect(); super.onListenerDisconnected() }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (sbn.isOngoing || sbn.packageName == packageName || !allowedPackages.contains(sbn.packageName)) return
        val extras = sbn.notification.extras ?: return
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString()?.take(70).orEmpty()
        val body = (extras.getCharSequence(Notification.EXTRA_BIG_TEXT) ?: extras.getCharSequence(Notification.EXTRA_TEXT))?.toString()?.take(140).orEmpty()
        if (title.isBlank() && body.isBlank()) return
        val label = try { packageManager.getApplicationLabel(packageManager.getApplicationInfo(sbn.packageName, 0)).toString() } catch (_: Exception) { sbn.packageName }
        val value = "${utf8Prefix(sbn.packageName,63)}\n${utf8Prefix(label,39)}\n${utf8Prefix(title,79)}\n${utf8Prefix(body,180)}".toByteArray(StandardCharsets.UTF_8)
        synchronized(pending) { if (pending.size >= 16) pending.removeFirst(); pending.addLast(value) }
        ensureConnected(); sendNext()
    }

    private fun utf8Prefix(value:String,maxBytes:Int):String {
        var end=value.length
        while(end>0&&value.substring(0,end).toByteArray(StandardCharsets.UTF_8).size>maxBytes)end--
        if(end>0&&end<value.length&&Character.isHighSurrogate(value[end-1]))end--
        return value.substring(0,end)
    }

    @SuppressLint("MissingPermission")
    private fun ensureConnected() {
        if (checkSelfPermission(if (Build.VERSION.SDK_INT >= 31) "android.permission.BLUETOOTH_CONNECT" else Manifest.permission.BLUETOOTH) != PackageManager.PERMISSION_GRANTED) return
        if (gatt != null || scanning) return
        val adapter = getSystemService(BluetoothManager::class.java)?.adapter ?: return
        if (!adapter.isEnabled) return
        val scanner = adapter.bluetoothLeScanner ?: return
        scanning = true
        scanner.startScan(listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()), ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_POWER).build(), object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: android.bluetooth.le.ScanResult) {
                scanner.stopScan(this); scanning = false
                gatt = result.device.connectGatt(this@RoboNotificationListener, false, callback, BluetoothDevice.TRANSPORT_LE)
            }
            override fun onScanFailed(errorCode: Int) { scanning = false }
        })
    }

    private val callback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, state: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS || state == BluetoothProfile.STATE_DISCONNECTED) { disconnect();mainHandler.postDelayed({ensureConnected()},2000);return }
            if (state == BluetoothProfile.STATE_CONNECTED) g.requestMtu(512)
        }
        @SuppressLint("MissingPermission")
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) { g.discoverServices() }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            characteristic = g.getService(SERVICE)?.getCharacteristic(RX)
            val list=g.getService(SERVICE)?.getCharacteristic(ALLOWLIST)
            if(list!=null)g.readCharacteristic(list)
        }
        @SuppressLint("MissingPermission")
        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            if(c.uuid==ALLOWLIST&&status==BluetoothGatt.GATT_SUCCESS){
                allowedPackages=c.value?.toString(StandardCharsets.UTF_8)?.split(Regex("[\\r\\n;\\s]+"))?.filter{it.isNotBlank()}?.toSet()?: emptySet()
                sendNext()
            }
        }
        @SuppressLint("MissingPermission")
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            synchronized(pending){val sent=inFlight;if(status==BluetoothGatt.GATT_SUCCESS&&sent!=null&&pending.firstOrNull()===sent)pending.removeFirst();inFlight=null}
            if(status==BluetoothGatt.GATT_SUCCESS)sendNext() else g.disconnect()
        }
    }

    @SuppressLint("MissingPermission")
    private fun sendNext() {
        val g = gatt ?: return
        val c = characteristic ?: return
        synchronized(pending){if(inFlight!=null)return}
        var bytes: ByteArray? = null
        while (bytes==null) {
            val candidate=synchronized(pending) { if (pending.isEmpty()) null else pending.first() } ?: return
            val pkg=candidate.toString(StandardCharsets.UTF_8).substringBefore('\n')
            if(allowedPackages.contains(pkg))bytes=candidate else synchronized(pending){if(pending.firstOrNull()===candidate)pending.removeFirst()}
        }
        val payload=bytes ?: return
        inFlight=payload
        if (Build.VERSION.SDK_INT >= 33) {
            val result = g.writeCharacteristic(c, payload, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
            if (result != android.bluetooth.BluetoothStatusCodes.SUCCESS) {
                inFlight = null
                g.disconnect()
            }
        }
        else { c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT; c.value = payload; if(!g.writeCharacteristic(c)){inFlight=null;g.disconnect()} }
    }

    @SuppressLint("MissingPermission")
    private fun disconnect() {
        synchronized(pending) { inFlight = null }
        gatt?.close(); gatt = null; characteristic = null; scanning = false
        allowedPackages=emptySet()
    }
}
