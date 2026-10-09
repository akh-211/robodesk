package com.robodesk.phonebridge

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanFilter
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Intent
import android.content.IntentSender
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.provider.Settings
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ArrayAdapter
import android.widget.AdapterView
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import android.app.TimePickerDialog
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import java.util.UUID

@Suppress("DEPRECATION")
class MainActivity : Activity() {
    companion object {
        private const val SELECT_DEVICE_REQUEST = 42
        private const val CONNECT_PERMISSION_REQUEST = 12
        private val SERVICE = UUID.fromString("93de0001-2c7d-4a52-9f1c-6f4b4f424c45")
    }

    private lateinit var status: TextView
    private lateinit var selectedRobot: TextView
    private lateinit var homeScreen: android.view.View
    private var commandStatusView: TextView? = null
    private var showingApps = false
    private var selectedTab = 0
    private val handler = Handler(Looper.getMainLooper())
    private val refresh = object : Runnable {
        override fun run() {
            status.text = PhoneBridgeStatus.text
            selectedRobot.text = if (TrustedRobotStore.address(this@MainActivity) == null) {
                "No robot selected"
            } else "One RoboDesk selected · address hidden"
            commandStatusView?.text = PhoneBridgeStatus.state.companionCommandStatus
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 40, 32, 32)
        }
        layout.addView(TextView(this).apply { text = "ROBO DESK · PHONE BRIDGE"; textSize = 22f })
        layout.addView(TextView(this).apply {
            text = "Your phone shares only the apps and content you choose with your RoboDesk."
            textSize = 15f
        })
        selectedRobot = TextView(this).apply { textSize = 17f; setPadding(0, 24, 0, 4) }
        layout.addView(selectedRobot)
        status = TextView(this).apply { text = PhoneBridgeStatus.text; setPadding(0, 8, 0, 12) }
        layout.addView(status)
        layout.addView(Button(this).apply {
            text = "Select RoboDesk"
            setOnClickListener { selectRobot() }
        })
        layout.addView(Button(this).apply {
            text = "Forget selected robot"
            setOnClickListener {
                val remotePending=PhoneBridgeStatus.requestAuthenticatedForget()
                val cleared=TrustedRobotStore.forget(this@MainActivity)
                PhoneBridgeStatus.localForgetVerified=cleared
                if(!remotePending)PhoneBridgeStatus.clearAfterForget()
                PhoneBridgeStatus.text = if(remotePending&&cleared) "Phone credential removed; waiting for the connected robot to confirm peer revocation." else if(cleared) "Robot credential and pending content cleared on this phone. If it was offline, revoke its peer from the robot dashboard too." else "Could not verify every local credential or association was removed. Check app settings and revoke the peer from the robot dashboard."
            }
        })
        layout.addView(TextView(this).apply {
            text = "Pairing uses Android's system dialog and a six digit code shown on the robot OLED. Approve the candidate in the robot dashboard. The selected robot address stays on this phone and is excluded from backup."
            setPadding(0, 16, 0, 8)
        })
        layout.addView(Switch(this).apply {
            text = "Forward Google Maps navigation"
            isChecked = getSharedPreferences("bridge", MODE_PRIVATE).getBoolean("navigation", false)
            setOnCheckedChangeListener { _, checked ->
                getSharedPreferences("bridge", MODE_PRIVATE).edit().putBoolean("navigation", checked).apply()
            }
        })
        layout.addView(Switch(this).apply {
            text = "Pause all sharing"
            isChecked = getSharedPreferences("bridge", MODE_PRIVATE).getBoolean("paused", false)
            minHeight = 52
            setOnCheckedChangeListener { _, checked ->
                getSharedPreferences("bridge", MODE_PRIVATE).edit().putBoolean("paused", checked).commit()
                PhoneBridgeStatus.pauseSharing?.invoke(checked)
                PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(sharingPaused = checked)
                PhoneBridgeStatus.text = if (checked) "Sharing paused. Queued phone content was cleared." else "Sharing resumed; selected app settings apply."
            }
        })
        layout.addView(Button(this).apply {
            text = "Choose app privacy"
            setOnClickListener { showApplicationModes() }
        })
        layout.addView(TextView(this).apply {
            text = "Notification sharing requires both the robot dashboard allowlist and a local app mode. The phone verifies the selected robot with a paired key and session challenge; each update has an HMAC and replay counter. BLE still requires Android's authenticated pairing. Redaction is pattern-based and may miss sensitive text."
            setPadding(0, 16, 0, 16)
        })
        layout.addView(Button(this).apply {
            text = "Enable notification access"
            setOnClickListener { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
        })
        layout.addView(Button(this).apply {
            text = "Enable Nearby devices permission"
            setOnClickListener { requestConnectPermission() }
        })
        layout.addView(Button(this).apply {
            text = "Open robot dashboard"
            setOnClickListener { openDashboard() }
        })
        val tabs = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 16, 0, 4) }
        listOf("Home", "Apps", "Navigation", "Robot", "More").forEachIndexed { index, label ->
            tabs.addView(Button(this).apply {
                text = label
                textSize = 10f
                isAllCaps = false
                minHeight = 48
                contentDescription = "$label tab"
                setOnClickListener { when (index) { 0 -> { selectedTab = 0; setContentView(homeScreen) }; 1 -> showApplicationModes(); else -> showSimpleTab(index) } }
            }, LinearLayout.LayoutParams(0, -2, 1f))
        }
        layout.addView(tabs)
        homeScreen = ScrollView(this).apply { addView(layout) }
        setContentView(homeScreen)
    }

    private fun showApplicationModes() {
        selectedTab = 1
        val page = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 32, 32, 24) }
        page.addView(Button(this).apply { text = "Back"; setOnClickListener { showingApps = false; setContentView(homeScreen) } })
        page.addView(TextView(this).apply {
            text = "APP PRIVACY\n\nRobot dashboard allowlist and this phone's choice both apply. New apps default to Off. When your phone is locked, content is reduced to app name only. Pattern redaction can miss sensitive text; use App name only for private apps."
            textSize = 17f
            setPadding(0, 12, 0, 18)
        })
        val modes = listOf("Off", "App name only", "Title only", "Title + redacted snippet")
        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, modes).also {
            it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        val launcher = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        val apps = packageManager.queryIntentActivities(launcher, PackageManager.MATCH_DEFAULT_ONLY)
            .map { it.activityInfo.applicationInfo }
            .distinctBy { it.packageName }
            .filter { it.packageName != packageName }
            .sortedBy { packageManager.getApplicationLabel(it).toString().lowercase() }
        for (app in apps) {
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, 8, 0, 8)
            }
            row.addView(TextView(this).apply {
                text = packageManager.getApplicationLabel(app)
                textSize = 16f
            })
            val picker = Spinner(this).apply { this.adapter = adapter }
            picker.setSelection(NotificationPrivacy.mode(this, app.packageName).ordinal)
            picker.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onNothingSelected(parent: AdapterView<*>?) = Unit
                override fun onItemSelected(parent: AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                    val selectedMode = AppContentMode.entries[position]
                    val changed = NotificationPrivacy.mode(this@MainActivity, app.packageName) != selectedMode
                    NotificationPrivacy.setMode(this@MainActivity, app.packageName, selectedMode)
                    if (changed) PhoneBridgeStatus.appModeChanged?.invoke(app.packageName, selectedMode)
                }
            }
            row.addView(picker)
            page.addView(row)
        }
        val scroll = ScrollView(this).apply { addView(page) }
        showingApps = true
        setContentView(scroll)
    }

    private fun showSimpleTab(index: Int) {
        selectedTab = index
        val titles = listOf("Home", "Apps", "Navigation", "Robot", "More")
        val page = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 30, 24, 20); setBackgroundColor(Color.rgb(246, 248, 249)) }
        page.addView(TextView(this).apply { text = "ROBO DESK"; textSize = 12f; setTextColor(Color.rgb(38, 111, 103)); setTypeface(null, Typeface.BOLD) })
        page.addView(TextView(this).apply { text = titles[index]; textSize = 28f; setTextColor(Color.rgb(24, 38, 48)); setTypeface(null, Typeface.BOLD); setPadding(0, 4, 0, 14) })
        fun panel(heading: String, body: String) {
            val card = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(16, 16, 16, 16); background = GradientDrawable().apply { setColor(Color.WHITE); cornerRadius = 18f } }
            card.addView(TextView(this).apply { text = heading; textSize = 17f; setTextColor(Color.rgb(24, 38, 48)); setTypeface(null, Typeface.BOLD) })
            val bodyView = TextView(this).apply { text = body; textSize = 14f; setTextColor(Color.rgb(77, 92, 103)); setPadding(0, 6, 0, 0); contentDescription = "$heading: $body" }
            card.addView(bodyView)
            if (heading == "Command status") commandStatusView = bodyView
            page.addView(card, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = 12 })
        }
        fun button(label: String, action: () -> Unit) {
            page.addView(Button(this).apply { text = label; isAllCaps = false; minHeight = 48; setOnClickListener { action() } }, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = 8 })
        }
        when (index) {
            2 -> {
                panel("Phone navigation", "When enabled, supported Google Maps turn prompts can be relayed to RoboDesk. This uses navigation notifications and does not start phone location tracking.")
                page.addView(Switch(this).apply { text = "Forward Google Maps navigation"; minHeight = 52; isChecked = getSharedPreferences("bridge", MODE_PRIVATE).getBoolean("navigation", false); setOnCheckedChangeListener { _, checked -> getSharedPreferences("bridge", MODE_PRIVATE).edit().putBoolean("navigation", checked).apply(); PhoneBridgeStatus.text = if (checked) "Navigation forwarding enabled." else "Navigation forwarding disabled." } })
                panel("Robot support", if (PhoneBridgeStatus.state.navigationEnabled) "Navigation bridge is available." else "Navigation bridge is not currently available.")
            }
            3 -> {
                panel("Connection", PhoneBridgeStatus.text)
                panel("Companion behavior", "Commands use the authenticated BLE session and robot safety gates. The robot confirms or rejects each command after the C3 gateway receives the S3 response.")
                page.addView(TextView(this).apply { text = "Character initiative"; textSize = 16f; setPadding(0, 12, 0, 4) })
                val intensityOptions = listOf("Subtle", "Active but calm", "Expressive")
                val intensityPicker = Spinner(this).apply { adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_item, intensityOptions).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }; contentDescription = "Character initiative level" }
                page.addView(intensityPicker)
                button("Apply initiative") { BridgeCompanionCommand.intensity(intensityPicker.selectedItemPosition)?.let { PhoneBridgeStatus.companionCommand?.invoke(it) } }
                page.addView(TextView(this).apply { text = "Start an activity"; textSize = 16f; setPadding(0, 12, 0, 4) })
                val activityPicker = Spinner(this).apply { adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_item, BridgeCompanionCommand.activities).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }; contentDescription = "Companion activity" }
                page.addView(activityPicker)
                button("Start selected activity") { BridgeCompanionCommand.startActivity(activityPicker.selectedItemPosition)?.let { PhoneBridgeStatus.companionCommand?.invoke(it) } }
                val actionRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                listOf("Pause" to BridgeCompanionCommand.pauseActivity(), "Resume" to BridgeCompanionCommand.resumeActivity(), "Cancel" to BridgeCompanionCommand.cancelActivity()).forEach { (label, payload) ->
                    actionRow.addView(Button(this).apply { text = label; isAllCaps = false; minHeight = 48; setOnClickListener { PhoneBridgeStatus.companionCommand?.invoke(payload.copyOf()) } }, LinearLayout.LayoutParams(0, -2, 1f))
                }
                page.addView(actionRow)
                val quietPrefs = getSharedPreferences("bridge", MODE_PRIVATE)
                var quietStart = quietPrefs.getInt("quietStartMin", -1)
                var quietEnd = quietPrefs.getInt("quietEndMin", -1)
                val quietTimes = TextView(this).apply { textSize = 14f; setPadding(0, 8, 0, 8); text = "Chosen quiet window: ${formatMinute(quietStart)}–${formatMinute(quietEnd)}. Robot's existing schedule is not read by the app." }
                page.addView(Switch(this).apply { text = "Quiet hours enabled (requested setting)"; minHeight = 52; contentDescription = "Request quiet hours enabled or disabled"; isChecked = quietPrefs.getBoolean("quietRequested", false); setOnCheckedChangeListener { _, enabled -> PhoneBridgeStatus.companionCommand?.invoke(BridgeCompanionCommand.quietEnabled(enabled)); quietPrefs.edit().putBoolean("quietRequested", enabled).apply() } })
                fun chooseQuietTime(initial: Int, selected: (Int) -> Unit) {
                    val minute = initial.takeIf { it in 0..1439 } ?: 0
                    TimePickerDialog(this, { _, hour, minuteOfHour -> selected(hour * 60 + minuteOfHour); quietTimes.text = "Chosen quiet window: ${formatMinute(quietStart)}–${formatMinute(quietEnd)}. Robot's existing schedule is not read by the app." }, minute / 60, minute % 60, true).show()
                }
                val quietRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                quietRow.addView(Button(this).apply { text = "Choose start"; isAllCaps = false; minHeight = 48; setOnClickListener { chooseQuietTime(quietStart.takeIf { it >= 0 } ?: 1320) { quietStart = it; quietPrefs.edit().putInt("quietStartMin", it).apply() } } }, LinearLayout.LayoutParams(0, -2, 1f))
                quietRow.addView(Button(this).apply { text = "Choose end"; isAllCaps = false; minHeight = 48; setOnClickListener { chooseQuietTime(quietEnd.takeIf { it >= 0 } ?: 420) { quietEnd = it; quietPrefs.edit().putInt("quietEndMin", it).apply() } } }, LinearLayout.LayoutParams(0, -2, 1f))
                page.addView(quietRow)
                page.addView(quietTimes)
                button("Save chosen quiet window") {
                    if (quietStart !in 0..1439 || quietEnd !in 0..1439) PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(companionCommandStatus = "Choose both quiet-hours times before saving.")
                    else PhoneBridgeStatus.companionCommand?.invoke(BridgeCompanionCommand.quietHours(quietPrefs.getBoolean("quietRequested", false), quietStart, quietEnd)!!)
                }
                panel("Command status", PhoneBridgeStatus.state.companionCommandStatus)
                if (PhoneBridgeStatus.companionCommand == null) panel("Connection required", "Connect and authenticate the selected robot before sending companion commands.")
                button("Select RoboDesk", ::selectRobot)
                button("Open robot dashboard", ::openDashboard)
                button("Forget selected robot") {
                    val remotePending = PhoneBridgeStatus.requestAuthenticatedForget()
                    val cleared = TrustedRobotStore.forget(this)
                    PhoneBridgeStatus.localForgetVerified = cleared
                    if (!remotePending) PhoneBridgeStatus.clearAfterForget()
                    PhoneBridgeStatus.text = if (remotePending && cleared) "Phone credential removed; waiting for robot confirmation." else if (cleared) "Phone trust data cleared. If the robot was offline, revoke it from the robot dashboard too." else "Could not verify local trust data removal; revoke the peer from the robot dashboard."
                    showSimpleTab(3)
                }
                panel("Pairing safety", "Pairing uses Android's system chooser and the code shown by the robot. Approve the phone in its dashboard. Trust data is protected with Android Keystore encryption and excluded from backup.")
            }
            else -> {
                val state=PhoneBridgeStatus.state
                val phase=state.phase.name.lowercase().replace('_',' ')
                panel("Connection diagnostics","State: $phase\nSelected robot: ${if(state.selectedRobot)"yes · address hidden" else "none"}\nAuthenticated session: ${if(state.authenticated)"active" else "inactive"}\nNotification access: ${if(state.notificationPermission)"granted" else "not granted"}\nNearby devices: ${if(state.nearbyPermission)"granted" else "not granted"}")
                panel("BLE link","RSSI: ${state.rssiDbm?.let{"$it dBm"}?:"unavailable"}\nNegotiated MTU: ${state.mtu}\nReconnect attempts: ${state.reconnectCount}\nS3 link: ${state.robotLinkStatus}")
                panel("Delivery queue","Queued notifications: ${state.queuedNotifications}\nOldest queued: ${state.queueOldestAgeMs/1000}s\nDropped: ${state.droppedCount} · coalesced: ${state.coalescedCount}\nLast ACK: ${if(state.lastAcknowledgedAt==0L)"none" else android.text.format.DateFormat.getTimeFormat(this).format(java.util.Date(state.lastAcknowledgedAt))}")
                panel("Companion command", state.companionCommandStatus)
                button("Reconnect now") { PhoneBridgeStatus.retryConnection?.invoke() ?: run { PhoneBridgeStatus.text="Open notification access to start the companion service." } }
                panel("Permissions", "Notification access and Nearby devices are used only for the companion features you enable. You can change or revoke them in Android Settings.")
                button("Notification access settings") { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
                button("Nearby devices permission", ::requestConnectPermission)
                button("Open robot dashboard", ::openDashboard)
                panel("Security", "Bridge messages are authenticated and replay-protected. Text redaction uses patterns and can miss sensitive content; choose App name only for private apps.")
            }
        }
        val tabs = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setBackgroundColor(Color.WHITE) }
        titles.forEachIndexed { tab, label -> tabs.addView(Button(this).apply { text = label; textSize = 12f; isAllCaps = false; minHeight = 48; contentDescription = "$label tab"; setOnClickListener { when (tab) { 0 -> { selectedTab = 0; setContentView(homeScreen) }; 1 -> showApplicationModes(); else -> showSimpleTab(tab) } } }, LinearLayout.LayoutParams(0, -2, 1f)) }
        val shell = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        shell.addView(ScrollView(this).apply { addView(page) }, LinearLayout.LayoutParams(-1, 0, 1f))
        shell.addView(tabs)
        setContentView(shell)
    }

    private fun formatMinute(minute: Int): String = if (minute !in 0..1439) "not set" else "%02d:%02d".format(minute / 60, minute % 60)

    private fun requestConnectPermission() {
        if (Build.VERSION.SDK_INT >= 31 && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), CONNECT_PERMISSION_REQUEST)
        } else PhoneBridgeStatus.text = "Nearby devices permission is ready."
    }

    private fun selectRobot() {
        if (Build.VERSION.SDK_INT >= 31 && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), CONNECT_PERMISSION_REQUEST)
            PhoneBridgeStatus.text = "Allow Nearby devices, then tap Select RoboDesk again."
            return
        }
        val manager = getSystemService(CompanionDeviceManager::class.java)
        if (manager == null) {
            PhoneBridgeStatus.text = "Android companion-device selection is unavailable on this phone."
            return
        }
        val filter = BluetoothLeDeviceFilter.Builder()
            .setScanFilter(ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build())
            .build()
        val request = AssociationRequest.Builder().addDeviceFilter(filter).setSingleDevice(true).build()
        PhoneBridgeStatus.text = "Opening Android's device chooser. Make sure the robot pairing window is open in its dashboard."
        try {
            if (Build.VERSION.SDK_INT >= 33) associateModern(manager, request)
            else manager.associate(request, object : CompanionDeviceManager.Callback() {
                override fun onDeviceFound(chooserLauncher: IntentSender) = launchChooser(chooserLauncher)
                override fun onFailure(error: CharSequence?) = reportAssociationFailure(error)
            }, handler)
        } catch (_: Exception) {
            PhoneBridgeStatus.text = "Android could not open the robot chooser. Check Bluetooth and Location Services, then retry."
        }
    }

    @android.annotation.TargetApi(33)
    private fun associateModern(manager: CompanionDeviceManager, request: AssociationRequest) {
        manager.associate(request, java.util.concurrent.Executor { it.run() }, object : CompanionDeviceManager.Callback() {
            override fun onAssociationPending(intentSender: IntentSender) = launchChooser(intentSender)
            override fun onAssociationCreated(info: android.companion.AssociationInfo) {
                val address = info.deviceMacAddress?.toString()
                if (!address.isNullOrBlank()) saveSelected(address, info.id)
            }
            override fun onFailure(error: CharSequence?) = reportAssociationFailure(error)
        })
    }

    private fun reportAssociationFailure(error: CharSequence?) {
        PhoneBridgeStatus.text = "Could not select a robot${error?.let { ": $it" }.orEmpty()}. Check that the robot is advertising and try again."
    }

    private fun launchChooser(sender: IntentSender) {
        try {
            startIntentSenderForResult(sender, SELECT_DEVICE_REQUEST, null, 0, 0, 0)
        } catch (_: IntentSender.SendIntentException) {
            PhoneBridgeStatus.text = "Android could not show its device chooser. Try selecting the robot again."
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != SELECT_DEVICE_REQUEST || resultCode != RESULT_OK || data == null) return
        val parcel = data.getParcelableExtra<android.os.Parcelable>(CompanionDeviceManager.EXTRA_DEVICE)
        val device = when (parcel) {
            is BluetoothDevice -> parcel
            is android.bluetooth.le.ScanResult -> parcel.device
            else -> null
        }
        if (device != null) saveSelected(device.address, null)
    }

    private fun saveSelected(address: String, associationId: Int?) {
        try {
            if(!TrustedRobotStore.save(this, address, associationId)){PhoneBridgeStatus.text="The previous robot credential could not be cleared. This robot was not selected.";return}
            PhoneBridgeStatus.clearAfterForget()
            val device = getSystemService(BluetoothManager::class.java)?.adapter?.getRemoteDevice(address)
            if (device?.bondState == BluetoothDevice.BOND_NONE) device?.createBond()
            PhoneBridgeStatus.text = "Robot selected. Enter the code shown on its OLED, then approve this phone in the robot dashboard."
        } catch (_: Exception) {
            PhoneBridgeStatus.text = "Android did not return a usable robot identity. Select it again."
        }
    }

    private fun openDashboard() {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse("http://192.168.1.6/"))
        try { startActivity(intent) } catch (_: Exception) {
            PhoneBridgeStatus.text = "Could not open the dashboard. Connect to the robot's Wi-Fi first."
        }
    }

    override fun onResume() { super.onResume(); handler.post(refresh) }
    override fun onPause() { handler.removeCallbacks(refresh); super.onPause() }
    @Deprecated("Deprecated in Android; handled for this single-screen settings flow")
    override fun onBackPressed() {
        if (showingApps) { showingApps = false; setContentView(homeScreen) }
        else super.onBackPressed()
    }
}
