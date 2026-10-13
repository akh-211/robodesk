package com.robodesk.phonebridge

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import java.util.UUID

/** One robot seen while scanning for the bridge service UUID `93de0001-...`. */
class ScannedRobot(val address: String, val name: String, val rssi: Int, val bonded: Boolean, val pairingOpen: Boolean? = null)

/** BLE scan for RoboDesk robots (onboarding step 2). Needs BLUETOOTH_SCAN (API 31+) or location (older). Not yet device-tested. */
@SuppressLint("MissingPermission")
class RobotScanner(private val context: Context) {
    private var callback: ScanCallback? = null
    private val seen = LinkedHashMap<String, ScannedRobot>()

    fun start(onUpdate: (List<ScannedRobot>) -> Unit, onFail: (String) -> Unit, mode: Int = ScanSettings.SCAN_MODE_LOW_LATENCY) {
        stop()
        val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter
        if (adapter == null || !adapter.isEnabled) { onFail("Bluetooth mati · nyalakan Bluetooth"); return }
        val scanner = adapter.bluetoothLeScanner ?: run { onFail("Pemindai Bluetooth tidak tersedia"); return }
        seen.clear()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val device = result.device
                val name = result.scanRecord?.deviceName ?: device.name ?: "RoboDesk"
                seen[device.address] = ScannedRobot(device.address, name.take(32), result.rssi, device.bondState == android.bluetooth.BluetoothDevice.BOND_BONDED,
                    PairingBeacon.parse(result.scanRecord?.getManufacturerSpecificData(PairingBeacon.COMPANY_ID)) ?: seen[device.address]?.pairingOpen)
                onUpdate(seen.values.sortedByDescending { it.rssi })
            }
            override fun onScanFailed(errorCode: Int) { onFail("Pemindaian gagal (kode $errorCode)") }
        }
        callback = cb
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(UUID.fromString("93de0001-2c7d-4a52-9f1c-6f4b4f424c45"))).build()
        try { scanner.startScan(listOf(filter), ScanSettings.Builder().setScanMode(mode).build(), cb) }
        catch (_: Exception) { callback = null; onFail("Izin pemindaian belum diberikan") }
    }

    fun stop() {
        val cb = callback ?: return
        callback = null
        try { context.getSystemService(BluetoothManager::class.java)?.adapter?.bluetoothLeScanner?.stopScan(cb) } catch (_: Exception) { }
    }
}
