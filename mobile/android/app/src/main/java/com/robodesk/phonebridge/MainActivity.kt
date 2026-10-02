package com.robodesk.phonebridge

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var status: TextView
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val layout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 48, 32, 32) }
        status = TextView(this).apply { text = "Connect RoboDesk and enable notification access. Use the robot dashboard allowlist to choose apps." }
        layout.addView(status)
        layout.addView(Button(this).apply { text = "Enable notification access"; setOnClickListener { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) } })
        layout.addView(Button(this).apply { text = "Enable Bluetooth permissions"; setOnClickListener { requestBluetoothPermissions() } })
        layout.addView(Button(this).apply { text = "Open Bluetooth settings to pair"; setOnClickListener { startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS)) } })
        setContentView(layout)
    }
    private fun requestBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= 31) requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT), 12)
        else status.text = if ((getSystemService(BluetoothManager::class.java)?.adapter?.isEnabled == true)) "Bluetooth is ready." else "Turn on Bluetooth in system settings."
    }
}
