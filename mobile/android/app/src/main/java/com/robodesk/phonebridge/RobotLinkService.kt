package com.robodesk.phonebridge

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.UUID

/** One queued bridge packet (notification, remove, navigation, forget, pause, companion command). */
class RobotPacket(val kind: Int, val pkg: String, val key: String, val bytes: ByteArray, val at: Long = SystemClock.elapsedRealtime())

/** Implemented by RoboNotificationListener: the only code that reads notifications. The link works without it. */
interface RobotLinkHost {
    /** The link just became READY. Return true when the host handled privacy refresh / re-posting of active notifications. */
    fun onReady(): Boolean
    /** Periodic upkeep while READY and idle (navigation expiry, re-post of active notifications). */
    fun onHeartbeatReady()
    /** The robot acknowledged a kind 2 (remove) or kind 5 (pause/clear) packet. */
    fun onPacketAcked(packet: RobotPacket, status: Int)
    /** Navigation state kept by the host must be dropped (authenticated forget started). */
    fun onNavigationCleared()
    /** Local state was forgotten: drop privacy refresh bookkeeping and navigation. */
    fun onForgetLocal()
}

/** Rendezvous between the foreground service (owner of the only GATT) and its clients. */
object RobotLinkHub {
    @Volatile var core: RobotLinkCore? = null
    @Volatile var host: RobotLinkHost? = null
    /** Set by the activity lifecycle; status polling stops in the background. */
    @Volatile var appForeground = false
}

/**
 * The single GATT connection, HMAC session, framing and packet pipeline. Everything runs on the main looper.
 * Moved verbatim from RoboNotificationListener (A1); request scheduling for settings is [RobotRequestScheduler].
 */
@SuppressLint("MissingPermission")
class RobotLinkCore(private val context: Context) {
    companion object {
        private val SERVICE = UUID.fromString("93de0001-2c7d-4a52-9f1c-6f4b4f424c45")
        private val ALLOWLIST = UUID.fromString("93de0003-2c7d-4a52-9f1c-6f4b4f424c45")
        private val NAV = UUID.fromString("93de0004-2c7d-4a52-9f1c-6f4b4f424c45")
        private val CONFIG = UUID.fromString("93de0005-2c7d-4a52-9f1c-6f4b4f424c45")
        private val IDENTITY = UUID.fromString("93de0007-2c7d-4a52-9f1c-6f4b4f424c45")
        private val CONTROL = UUID.fromString("93de0008-2c7d-4a52-9f1c-6f4b4f424c45")
        private val ENVELOPE = UUID.fromString("93de0009-2c7d-4a52-9f1c-6f4b4f424c45")
        private val ACK = UUID.fromString("93de000a-2c7d-4a52-9f1c-6f4b4f424c45")
        private val CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        /** How long one scan-before-connect looks for the robot's advertisement. */
        private const val FINDER_MS = 8_000L
    }

    val handler = Handler(Looper.getMainLooper())
    private var gatt: BluetoothGatt? = null
    private var finder: ScanCallback? = null
    /** Alternates after a first connect that times out (GATT 147/133/62): some phones only reach the robot via a background connect. */
    private var useAutoConnect = false
    private var bondAttemptAt = 0L
    private var bondAttempts = 0
    private var linkFailures = 0
    private var notifyRetries = 0
    private var refreshNext = false
    var connectedAddress: String? = null
        private set
    private var active = false
    var ready = false
        private set
    private var busy = false
    private var awaitingAck = false
    private var awaitingControl = false
    private var operationAt = 0L
    private val backoff = ReconnectBackoff()
    private var reconnectCount = 0L
    var droppedCount = 0L
    var coalescedCount = 0L
    private var rssiDbm: Int? = null
    var forgetInProgress = false
        private set
    private var mtu = 23
    var robotNavigation = false
        private set
    var navLatest: RobotPacket? = null
    private var inFlight: RobotPacket? = null
    private var inFlightCounter = 0L
    private var pendingCommandResultCounter = 0L
    private var pendingCommandResultAt = 0L
    private var writeFrames: List<ByteArray> = emptyList()
    private var writeFrameIndex = 0
    private var writeTarget: UUID? = null
    private var notificationSetupIndex = 0
    private val controlReassembler = BridgeGattFraming.Reassembler()
    private val ackReassembler = BridgeGattFraming.Reassembler()
    private var phoneNonce: ByteArray? = null
    private var pendingChallenge: BridgeSessionHandshake.Challenge? = null
    private var credentialStore: BridgeCredentialStore? = null
    private var robotId: ByteArray? = null
    private var storedCredential: BridgeCredentialStore.ReadResult.Available? = null
    private var sessionSender: BridgeAuthenticatedEnvelope.SessionSender? = null
    private var ackReceiver: BridgeAuthenticatedEnvelope.SessionReceiver? = null
    val scheduler = RobotRequestScheduler()
    /** Status polling; screens set [StatusPoller.screenNeedsStatus], the activity sets [StatusPoller.foreground]. */
    val poller = StatusPoller(BridgeSettingsClient(PhoneSettingsBridge), scheduler)
    private val pollTask = object : Runnable {
        override fun run() {
            if (!active) return
            poller.foreground = RobotLinkHub.appForeground
            poller.connected = ready && sessionSender != null && !forgetInProgress
            poller.tick(SystemClock.elapsedRealtime())
            handler.postDelayed(this, 1000)
        }
    }
    private var settingsSessionId = 0L
    private val handshakeGuard = HandshakeGuard()
    private val rejectGuard = SessionRejectGuard()
    private val search = PairingSearch()
    private var approvalWaitSince = 0L
    private var connectStartedAt = 0L
    private val schedulerTick = Runnable { pumpScheduler(); sendNext() }
    var allowed: Set<String> = emptySet()
        private set
    val pending = ArrayDeque<RobotPacket>()

    /** The robot allowlist changed over BLE (saved settings): forwarding follows it at once instead of after the next reconnect. */
    fun setAllowed(packages: Set<String>) = handler.post { allowed = packages.toSet() }

    private val host: RobotLinkHost? get() = RobotLinkHub.host

    fun sharingPaused(): Boolean = context.getSharedPreferences("bridge", Context.MODE_PRIVATE).getBoolean("paused", false)

    fun hasSession(): Boolean = sessionSender != null

    fun inFlightPacket(): RobotPacket? = inFlight

    fun updateQueueState() {
        val oldest = pending.firstOrNull { it.kind == 1 }
        PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(
            selectedRobot = TrustedRobotStore.address(context) != null, connected = gatt != null && ready, authenticated = sessionSender != null,
            notificationPermission = android.provider.Settings.Secure.getString(context.contentResolver, "enabled_notification_listeners")?.contains(context.packageName) == true,
            nearbyPermission = Build.VERSION.SDK_INT < 31 || context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED,
            sharingPaused = sharingPaused(), navigationEnabled = robotNavigation, queuedNotifications = pending.count { it.kind == 1 },
            queueOldestAgeMs = oldest?.let { (SystemClock.elapsedRealtime() - it.at).coerceAtLeast(0) } ?: 0L,
            droppedCount = droppedCount, coalescedCount = coalescedCount, mtu = mtu, rssiDbm = rssiDbm, reconnectCount = reconnectCount)
    }

    private val heartbeat = object : Runnable {
        override fun run() {
            if (!active) return
            val expired = pending.filter { it.kind != 5 && SystemClock.elapsedRealtime() - it.at > if (it.kind == 6) 30000 else 300000 }
            expired.forEach { it.bytes.fill(0); droppedCount++ }
            pending.removeAll { it.kind != 5 && SystemClock.elapsedRealtime() - it.at > if (it.kind == 6) 30000 else 300000 }
            updateQueueState()
            if (pendingCommandResultCounter != 0L && SystemClock.elapsedRealtime() - pendingCommandResultAt > 20000) {
                pendingCommandResultCounter = 0
                PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(companionCommandStatus = "Robot command result timed out; check robot status before retrying.")
            }
            if ((busy || awaitingAck || awaitingControl) && SystemClock.elapsedRealtime() - operationAt > if (search.active(SystemClock.elapsedRealtime())) PairingSearch.OPERATION_MS else 20000) disconnect()
            ensureConnected()
            if (ready && !busy) {
                host?.onHeartbeatReady()
                if (!busy && !awaitingAck && !awaitingControl) {
                    val currentGatt = gatt
                    busy = currentGatt?.readRemoteRssi() == true
                    if (busy) operationAt = SystemClock.elapsedRealtime()
                }
                sendNext()
            }
            handler.postDelayed(this, 15000)
        }
    }

    // ------------------------------------------------------------------ lifecycle

    fun start() {
        if (active) return
        active = true
        PhoneBridgeStatus.forgetLocalState = {
            handler.post {
                clearPending(); host?.onForgetLocal(); navLatest?.bytes?.fill(0); navLatest = null; allowed = emptySet(); disconnect()
            }
        }
        PhoneBridgeStatus.authenticatedForget = fun(): Boolean {
            if (!ready || sessionSender == null || busy || awaitingAck || awaitingControl) return false
            forgetInProgress = true; clearPending(); navLatest?.bytes?.fill(0); navLatest = null; host?.onNavigationCleared()
            pending.addFirst(RobotPacket(4, "", "", byteArrayOf())); PhoneBridgeStatus.setPhase(BridgeConnectionPhase.REVOKING); sendNext(); return true
        }
        PhoneBridgeStatus.retryConnection = { handler.post { if (!forgetInProgress) { backoff.reset(); rejectGuard.reset(); disconnect(reconnect = false); ensureConnected() } } }
        PhoneBridgeStatus.companionCommand = { payload -> handler.post { queueCompanionCommand(payload) } }
        PhoneSettingsBridge.submit = { request, callback -> handler.post { submitSettings(request, callback) } }
        handler.post {
            PhoneBridgeStatus.text = "Connecting to RoboDesk."
            ensureConnected()
            handler.removeCallbacks(heartbeat)
            handler.post(heartbeat)
            handler.removeCallbacks(pollTask)
            handler.post(pollTask)
        }
    }

    fun stop() {
        active = false
        handler.removeCallbacksAndMessages(null)
        PhoneBridgeStatus.forgetLocalState = null; PhoneBridgeStatus.authenticatedForget = null; PhoneBridgeStatus.retryConnection = null
        PhoneBridgeStatus.companionCommand = null; PhoneSettingsBridge.submit = null
        disconnect(reconnect = false)
        clearPending()
        PhoneBridgeStatus.setPhase(BridgeConnectionPhase.IDLE, connected = false, authenticated = false)
    }

    /**
     * Onboarding step "jendela pairing": retry a fresh connection every 1.5 s for [PairingSearch.WINDOW_MS], because the robot
     * only treats a connection made while its window is open as a pairing candidate. Clears earlier failure state.
     */
    fun beginPairingSearch() = handler.post {
        if (!active) return@post
        search.begin(SystemClock.elapsedRealtime()); bondAttemptAt = 0L; bondAttempts = 0; linkFailures = 0; PhoneBridgeStatus.staleBond = false; PhoneBridgeStatus.pairStep = 1
        handshakeGuard.onReady(); PhoneBridgeStatus.handshakeFailures = 0; rejectGuard.reset(); backoff.reset(); PhoneBridgeStatus.reconnectAttempt = 0
        PhoneBridgeStatus.failure = null; PhoneBridgeStatus.stage = null
        if (!ready) { if (gatt != null) disconnect(reconnect = false); ensureConnected() }
        PhoneBridgeStatus.republishLink()
    }

    /** ACTION_BOND_STATE_CHANGED for the selected robot: once bonded during pairing, open GATT directly (the link may already be up, so no scan). */
    fun onBondStateChanged(address: String, state: Int) = handler.post {
        if (active && state == BluetoothDevice.BOND_BONDING && search.active(SystemClock.elapsedRealtime())) setPairStep(2)
        if (!active || state != BluetoothDevice.BOND_BONDED || gatt != null || TrustedRobotStore.address(context) != address) return@post
        if (!permissions()) return@post
        stopFinder()
        // Let Android finish its own bonding work on the link and the robot store the new keys before our GATT starts (a GATT started in the
        // same instant had its first notification write refused).
        setPairStep(3)
        handler.postDelayed({ connectAfterBond(address) }, 1800L)
    }

    private fun connectAfterBond(address: String) {
        if (!active || gatt != null || TrustedRobotStore.address(context) != address) return
        try {
            val device = (context.getSystemService(BluetoothManager::class.java)?.adapter ?: return).getRemoteDevice(address)
            connectedAddress = address; connectStartedAt = SystemClock.elapsedRealtime()
            LinkDiag.log("bond selesai · connectGatt")
            gatt = openGatt(device, false)
        } catch (_: Exception) { connectedAddress = null; scheduleReconnect() }
    }

    fun searching(): Boolean = search.active(SystemClock.elapsedRealtime())

    fun endPairingSearch() = handler.post { search.end(); PhoneBridgeStatus.stage = null; PhoneBridgeStatus.pairStep = 0; PhoneBridgeStatus.republishLink() }

    /** Bluetooth turned on/off or the app was resumed: (re)start the connection attempt. */
    fun onBluetoothStateChanged(enabled: Boolean) = handler.post {
        if (!active) return@post
        if (enabled) { backoff.reset(); ensureConnected() } else if (gatt != null || finder != null) disconnect()
    }

    // ------------------------------------------------------------------ queueing

    fun keyFor(key: String): String = MessageDigest.getInstance("SHA-256").digest(key.toByteArray()).take(12).joinToString("") { "%02x".format(it.toInt() and 255) }

    private fun queueCompanionCommand(payload: ByteArray) {
        if (payload.isEmpty() || payload.size > 8 || !ready || sessionSender == null || forgetInProgress || inFlight?.kind == 6 || pending.any { it.kind == 6 }) {
            PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(companionCommandStatus = "Robot command unavailable or another command is still pending."); return
        }
        enqueue(RobotPacket(6, "", "companion:${SystemClock.elapsedRealtimeNanos()}", payload.copyOf()))
        PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(companionCommandStatus = "Command queued for the selected robot.")
        sendNext()
    }

    private fun submitSettings(request: BridgeSettingsProtocol.Request, callback: SettingsDone<BridgeSettingsProtocol.Response>) {
        if (!ready || sessionSender == null || forgetInProgress) { request.wipe(); callback(SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.NOT_CONNECTED))); return }
        if (!scheduler.enqueue(request, callback)) { request.wipe(); callback(SettingsOutcome.Fail(SettingsFailure(SettingsFailure.Reason.QUEUE_FULL))); return }
        sendNext()
    }

    /** Pumps the scheduler: expiry/backoff first, then starts the next request if the GATT write pipe is free. */
    private fun pumpScheduler() {
        val now = SystemClock.elapsedRealtime()
        scheduler.expire(now)
        if (!scheduler.inFlight && scheduler.hasWaiting && !busy && !awaitingAck && !awaitingControl && ready) {
            scheduler.startNext(now) { request ->
                val payload = request.payloadFor(settingsSessionId) ?: return@startNext null
                val envelope = sessionSender?.create(request.kind, 0, payload); payload.fill(0)
                if (envelope == null) null else { val counter = readU32le(envelope, 12); beginWrite(ENVELOPE, envelope); counter }
            }
        }
        handler.removeCallbacks(schedulerTick)
        scheduler.nextWakeMs(now)?.let { handler.postDelayed(schedulerTick, (it - now).coerceAtLeast(10L)) }
    }

    fun clearPending() { pending.forEach { it.bytes.fill(0) }; pending.clear() }

    fun enqueue(packet: RobotPacket) {
        if (forgetInProgress) { packet.bytes.fill(0); return }
        val replaced = pending.filter { it.key == packet.key && it !== inFlight }
        replaced.forEach { it.bytes.fill(0) }
        if (replaced.isNotEmpty()) coalescedCount += replaced.size
        pending.removeAll { it.key == packet.key && it !== inFlight }
        if (pending.size >= 12) { val old = pending.firstOrNull { it !== inFlight }; if (old != null) { old.bytes.fill(0); pending.remove(old); droppedCount++ } }
        pending.addLast(packet); updateQueueState()
    }

    private fun permissions(): Boolean = Build.VERSION.SDK_INT < 31 || context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    fun ensureConnected() {
        if (!active) return
        if (rejectGuard.consecutive >= SessionRejectGuard.LIMIT) return // the robot keeps refusing this phone: wait for the user's retry
        val target = TrustedRobotStore.address(context)
        if (target == null) {
            if ((gatt != null || finder != null) && !forgetInProgress) disconnect()
            if (!forgetInProgress) { PhoneBridgeStatus.text = "Select one RoboDesk in the app before connecting."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.IDLE, connected = false, authenticated = false) }
            return
        }
        if ((gatt != null || finder != null) && connectedAddress != target) { disconnect(); clearPending(); navLatest?.bytes?.fill(0); navLatest = null; host?.onNavigationCleared() }
        // A direct connectGatt that never completes would block every retry until Android's ~30 s timeout; during pairing search give up after 25 s (fast connect/close cycles wedge some Android BLE stacks).
        if (gatt != null && search.active(SystemClock.elapsedRealtime()) && PhoneBridgeStatus.state.phase == BridgeConnectionPhase.CONNECTING && SystemClock.elapsedRealtime() - connectStartedAt > 25000L) { LinkDiag.log("tak tersambung 25 dtk (auto=$useAutoConnect) · ganti mode"); useAutoConnect = !useAutoConnect; disconnect(); return }
        if (gatt != null || finder != null) return
        // Only the user starts pairing: with a robot selected but no trust record, a background connect would make Android pop up a
        // pairing request (even with the app closed). Connect only inside the onboarding pairing search until the phone is paired.
        if (!search.active(SystemClock.elapsedRealtime()) && !(try { BridgeCredentialStore(context).hasRecord() } catch (_: Exception) { false })) {
            PhoneBridgeStatus.text = "Robot dipilih tetapi belum dipasangkan · mulai pairing dari app"
            PhoneBridgeStatus.setPhase(BridgeConnectionPhase.IDLE, connected = false, authenticated = false)
            return
        }
        if (!permissions()) { PhoneBridgeStatus.text = "Grant Nearby devices permission to connect to the selected robot."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.NEEDS_PERMISSION, connected = false, authenticated = false); return }
        val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter ?: return
        if (!adapter.isEnabled) { PhoneBridgeStatus.text = "Bluetooth is off."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.OFFLINE, connected = false, authenticated = false); return }
        try {
            connectedAddress = target
            PhoneBridgeStatus.text = "Connecting to the selected RoboDesk."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.CONNECTING, connected = false, authenticated = false)
            val device = adapter.getRemoteDevice(target)
            // Android is bonding on its own link (user entering the passkey): a second connectGatt/scan would cancel it on some Samsung stacks.
            if (search.active(SystemClock.elapsedRealtime()) && device.bondState == BluetoothDevice.BOND_BONDING) {
                connectedAddress = null
                PhoneBridgeStatus.stage = "Masukkan kode yang tampil di layar robot"; setPairStep(2)
                PhoneBridgeStatus.setPhase(BridgeConnectionPhase.PAIRING, connected = false, authenticated = false)
                scheduleReconnect(); return
            }
            // autoConnect=true is slow and unreliable for a first connection; only use it to wait for an already bonded robot.
            val wait = !search.active(SystemClock.elapsedRealtime()) && device.bondState == BluetoothDevice.BOND_BONDED
            // Look at the robot's advertisement first, bonded or not: while its pairing window is open and the user is not pairing, a background
            // connect would become an unwanted pairing candidate (passkey on the OLED before the robot is chosen). See connectFound().
            if (startFinder(adapter, target)) return
            LinkDiag.log("connectGatt dari alamat tersimpan (auto=$wait, bond=${device.bondState})")
            connectStartedAt = SystemClock.elapsedRealtime()
            gatt = openGatt(device, wait)
        } catch (_: Exception) {
            connectedAddress = null
            PhoneBridgeStatus.text = "Could not reach the selected robot. Check Bluetooth and pairing, then retry."
            scheduleReconnect()
        }
    }

    private fun openGatt(device: BluetoothDevice, autoConnect: Boolean): BluetoothGatt = device.connectGatt(context, autoConnect, callback, BluetoothDevice.TRANSPORT_LE)

    /**
     * Counts connects that fail while the phone still holds a bond with this robot (outside the pairing search, where failures are
     * expected). Three in a row mark the bond as stale; the UI offers re-pairing only when the robot is also seen advertising.
     */
    private fun setPairStep(step: Int) { if (PhoneBridgeStatus.pairStep != step) { PhoneBridgeStatus.pairStep = step; PhoneBridgeStatus.republishLink() } }

    private fun noteLinkFailure(g: BluetoothGatt, status: Int, state: Int) {
        if (status == BluetoothGatt.GATT_SUCCESS && state == BluetoothProfile.STATE_CONNECTED) return
        if (search.active(SystemClock.elapsedRealtime())) return
        if (g.device.bondState != BluetoothDevice.BOND_BONDED) { linkFailures = 0; return }
        if (++linkFailures >= 3 && !PhoneBridgeStatus.staleBond) { PhoneBridgeStatus.staleBond = true; LinkDiag.log("bond HP mungkin basi ($linkFailures gagal berturut-turut)"); PhoneBridgeStatus.republishLink() }
    }

    /** Discovers services; after a failed attempt first clears Android's cached GATT table for the robot (hidden `refresh`). */
    private fun discover(g: BluetoothGatt): Boolean {
        if (refreshNext) {
            refreshNext = false
            val ok = try { g.javaClass.getMethod("refresh").invoke(g) as? Boolean } catch (_: Throwable) { null }
            LinkDiag.log("cache GATT dibersihkan=$ok")
        }
        return g.discoverServices()
    }

    private fun scanPermission(): Boolean = context.checkSelfPermission(if (Build.VERSION.SDK_INT >= 31) Manifest.permission.BLUETOOTH_SCAN else Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

    /**
     * Scan-then-connect for an unbonded robot or during pairing search. A blind connectGatt on an address string can wait ~30 s
     * and never reach the robot on some phones (the C3 logged no connection at all); connecting with the device object from a
     * fresh advertisement carries the address type and only starts once the robot is actually advertising.
     */
    private fun startFinder(adapter: BluetoothAdapter, target: String): Boolean {
        if (!scanPermission()) return false
        val scanner = adapter.bluetoothLeScanner ?: return false
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) { val self = this; handler.post { if (finder === self && result.device.address.equals(target, true)) connectFound(result) } }
            override fun onScanFailed(errorCode: Int) { val self = this; handler.post { if (finder !== self) return@post; LinkDiag.log("pindai gagal kode=$errorCode"); stopFinder(); connectedAddress = null; if (search.active(SystemClock.elapsedRealtime())) PhoneBridgeStatus.failure = "Pemindaian Bluetooth gagal · matikan lalu nyalakan Bluetooth, lalu coba lagi"; scheduleReconnect() } }
        }
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()
        return try {
            scanner.startScan(listOf(filter), ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
            finder = cb; handler.postDelayed(finderTimeout, FINDER_MS); LinkDiag.log("pindai mulai"); true
        } catch (e: Exception) { LinkDiag.log("pindai tak bisa mulai: ${e.javaClass.simpleName}"); false }
    }

    private val finderTimeout = Runnable {
        if (finder == null) return@Runnable
        LinkDiag.log("robot tak terlihat 8 dtk")
        stopFinder()
        val waiting = connectedAddress
        if (waiting != null && !search.active(SystemClock.elapsedRealtime())) {
            // Not advertising right now: wait for it in the background (autoConnect), as before the advertisement check existed.
            val device = try { context.getSystemService(BluetoothManager::class.java)?.adapter?.getRemoteDevice(waiting) } catch (_: Exception) { null }
            if (device != null && device.bondState == BluetoothDevice.BOND_BONDED) {
                try { connectStartedAt = SystemClock.elapsedRealtime(); LinkDiag.log("menunggu robot di latar (autoConnect)"); gatt = openGatt(device, true); return@Runnable } catch (_: Exception) { }
            }
        }
        connectedAddress = null
        if (search.active(SystemClock.elapsedRealtime())) PhoneBridgeStatus.failure = "Robot tidak terlihat · pastikan robot menyala dan dekat HP · mencoba lagi"
        PhoneBridgeStatus.text = "The selected robot is not advertising nearby; retrying."
        PhoneBridgeStatus.setPhase(BridgeConnectionPhase.OFFLINE, connected = false, authenticated = false)
        scheduleReconnect()
    }

    private fun stopFinder() {
        handler.removeCallbacks(finderTimeout)
        val cb = finder ?: return
        finder = null
        try { context.getSystemService(BluetoothManager::class.java)?.adapter?.bluetoothLeScanner?.stopScan(cb) } catch (_: Exception) { }
    }

    private fun connectFound(result: ScanResult) {
        stopFinder()
        if (!active || gatt != null) return
        val device = result.device
        val beaconOpen = PairingBeacon.parse(result.scanRecord?.getManufacturerSpecificData(PairingBeacon.COMPANY_ID))
        val nowB = SystemClock.elapsedRealtime()
        if (!search.active(nowB) && beaconOpen == true) {
            // The robot waits for a new phone and the user is not pairing: do not connect (it would become a candidate and show a passkey).
            LinkDiag.log("robot membuka pairing · tidak menyambung otomatis")
            PhoneBridgeStatus.beaconOpenAt = nowB
            connectedAddress = null
            PhoneBridgeStatus.text = "Robot membuka pairing baru · pasangkan ulang untuk menyambung"
            PhoneBridgeStatus.setPhase(BridgeConnectionPhase.OFFLINE, connected = false, authenticated = false)
            scheduleReconnect(); return
        }
        if (search.active(nowB) && beaconOpen == false && device.bondState == BluetoothDevice.BOND_NONE) {
            // The user is pairing but the robot's window is closed: wait for it instead of bonding against a robot that would drop the link.
            LinkDiag.log("menunggu pairing terbuka di robot")
            connectedAddress = null
            PhoneBridgeStatus.failure = null
            PhoneBridgeStatus.stage = "Tahan sensor kepala robot 5 detik sampai layarnya menampilkan PAIRING TERBUKA"; setPairStep(1)
            PhoneBridgeStatus.setPhase(BridgeConnectionPhase.PAIRING, connected = false, authenticated = false)
            scheduleReconnect(); return
        }
        LinkDiag.log("terlihat rssi=${result.rssi} connectable=${result.isConnectable} phy=${result.primaryPhy}/${result.secondaryPhy}" + if (Build.VERSION.SDK_INT >= 35) " tipe=${device.addressType}" else "")
        // The pre-Compose app paired with createBond() alone and that worked on a Samsung A16, while a plain connectGatt to the
        // unbonded C3 times out (GATT 147, the C3 never sees a connection; nRF Connect's Connect fails the same way). So during the
        // pairing search let Android's bonding path open the link first and run NO connectGatt in parallel (it cancels the bond on
        // some stacks); onBondStateChanged() connects once BOND_BONDED. After two bond attempts that leave BOND_NONE, fall through.
        val now = SystemClock.elapsedRealtime()
        if (search.active(now) && device.bondState == BluetoothDevice.BOND_NONE && bondAttempts < 2) {
            if (now - bondAttemptAt > 15_000L) {
                bondAttemptAt = now; bondAttempts++
                val started = try { device.createBond() } catch (_: Exception) { false }
                LinkDiag.log("createBond=$started (percobaan $bondAttempts)")
                if (!started) bondAttempts = 2
            }
            if (bondAttempts < 2 || device.bondState != BluetoothDevice.BOND_NONE) {
                connectedAddress = null
                PhoneBridgeStatus.failure = null
                PhoneBridgeStatus.stage = "Masukkan kode yang tampil di layar robot"; setPairStep(2)
                PhoneBridgeStatus.setPhase(BridgeConnectionPhase.PAIRING, connected = false, authenticated = false)
                scheduleReconnect(); return
            }
        }
        PhoneBridgeStatus.failure = null
        try {
            connectStartedAt = SystemClock.elapsedRealtime()
            LinkDiag.log("connectGatt auto=$useAutoConnect")
            gatt = openGatt(device, useAutoConnect)
        } catch (e: Exception) {
            LinkDiag.log("connectGatt gagal: ${e.javaClass.simpleName}")
            connectedAddress = null
            PhoneBridgeStatus.text = "Could not reach the selected robot. Check Bluetooth and pairing, then retry."
            scheduleReconnect()
        }
    }

    // ------------------------------------------------------------------ GATT

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, state: Int) { handler.post { if (g !== gatt) return@post; LinkDiag.log("GATT status=$status state=$state (${(SystemClock.elapsedRealtime() - connectStartedAt) / 100 / 10.0} dtk)"); if (!ready) noteLinkFailure(g, status, state); if (!ready && state != BluetoothProfile.STATE_CONNECTED && (status == 147 || status == 133 || status == 62)) useAutoConnect = !useAutoConnect; if (status != BluetoothGatt.GATT_SUCCESS || state == BluetoothProfile.STATE_DISCONNECTED) { if (search.active(SystemClock.elapsedRealtime())) PhoneBridgeStatus.failure = "Belum tersambung ke robot · mencoba lagi"; disconnect() } else if (state == BluetoothProfile.STATE_CONNECTED) { PhoneBridgeStatus.setPhase(BridgeConnectionPhase.AUTHENTICATING, connected = true, authenticated = false); if (search.active(SystemClock.elapsedRealtime())) { PhoneBridgeStatus.stage = "Terhubung ke robot · masukkan kode dari layar robot di HP"; setPairStep(if (g.device.bondState == BluetoothDevice.BOND_BONDED) 3 else 2); PhoneBridgeStatus.republishLink() }; busy = true; operationAt = SystemClock.elapsedRealtime(); if (!g.requestMtu(512)) { busy = false; if (!discover(g)) disconnect() } } } }
        override fun onMtuChanged(g: BluetoothGatt, value: Int, status: Int) { handler.post { if (g !== gatt) return@post; LinkDiag.log("MTU $value status=$status"); mtu = if (status == BluetoothGatt.GATT_SUCCESS) value else 23; updateQueueState(); busy = true; operationAt = SystemClock.elapsedRealtime(); if (!discover(g)) disconnect() } }
        override fun onReadRemoteRssi(g: BluetoothGatt, rssi: Int, status: Int) { handler.post { if (g !== gatt) return@post; busy = false; operationAt = 0L; if (status == BluetoothGatt.GATT_SUCCESS) rssiDbm = rssi; updateQueueState(); sendNext() } }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) { handler.post { if (g !== gatt) return@post; val service = g.getService(SERVICE); val required = listOf(IDENTITY, CONTROL, ENVELOPE, ACK, ALLOWLIST, CONFIG); LinkDiag.log("layanan status=$status ada=${service != null} hilang=${required.count { service?.getCharacteristic(it) == null }}/${required.size}"); if (status != BluetoothGatt.GATT_SUCCESS || service == null || required.any { service.getCharacteristic(it) == null }) { PhoneBridgeStatus.text = "This robot firmware lacks authenticated bridge support. Update the robot; legacy sending is disabled."; if (search.active(SystemClock.elapsedRealtime())) PhoneBridgeStatus.failure = "Layanan robot belum terbaca di HP · hapus RoboDesk dari daftar Bluetooth HP lalu ulangi"; disconnect() } else { notificationSetupIndex = 0; notifyRetries = 0; enableNextNotification(g) } } }
        @Deprecated("Legacy Android callback") override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) { read(g, c.uuid, c.value ?: byteArrayOf(), status) }
        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray, status: Int) { read(g, c.uuid, value, status) }
        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) { handler.post { if (g !== gatt) return@post; busy = false; if (status != BluetoothGatt.GATT_SUCCESS) { LinkDiag.log("notifikasi gagal status=$status uuid=${d.characteristic?.uuid.toString().take(8)} bond=${g.device.bondState} coba=$notifyRetries"); refreshNext = true; if (notifyRetries++ < 1) { handler.postDelayed({ if (g === gatt && !ready) enableNextNotification(g) }, 900L); return@post }; g.disconnect(); return@post }; notificationSetupIndex++; enableNextNotification(g) } }
        @Deprecated("Legacy Android callback") override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) { onBridgeNotification(g, c.uuid, c.value ?: byteArrayOf()) }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) { onBridgeNotification(g, c.uuid, value) }
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) { handler.post { if (g !== gatt || c.uuid != writeTarget) return@post; if (status != BluetoothGatt.GATT_SUCCESS) { PhoneBridgeStatus.text = "BLE write failed; reconnecting."; g.disconnect(); return@post }; writeFrameIndex++; if (writeFrameIndex < writeFrames.size) { writeOneFrame(g) } else { busy = false; writeFrames.forEach { it.fill(0) }; writeFrames = emptyList(); writeFrameIndex = 0; writeTarget = null; operationAt = SystemClock.elapsedRealtime(); if (c.uuid == ENVELOPE) { if (inFlight != null) awaitingAck = true } else awaitingControl = true; sendNext() } } }
    }

    private fun read(g: BluetoothGatt, uuid: UUID, value: ByteArray, status: Int) {
        handler.post {
            if (g !== gatt) return@post; busy = false
            if (status != BluetoothGatt.GATT_SUCCESS) { LinkDiag.log("baca gagal status=$status"); g.disconnect(); return@post }
            if (uuid == IDENTITY && (value.isNotEmpty() || approvalWaitSince == 0L)) LinkDiag.log("identitas ${value.size} byte")
            if (uuid == IDENTITY) {
                PhoneBridgeStatus.setPhase(BridgeConnectionPhase.AUTHENTICATING, connected = true, authenticated = false)
                val parsed = IdentityGate.parse(value)
                if (parsed === IdentityGate.Parsed.AwaitingApproval) {
                    // The identity characteristic is empty until the robot approves this bond (S3 touch -> C3). Disconnecting here would
                    // make the robot delete the candidate bond, so keep the connection and read again until the approval window ends.
                    val now = SystemClock.elapsedRealtime()
                    if (approvalWaitSince == 0L) approvalWaitSince = now
                    if (now - approvalWaitSince > PairingSearch.APPROVAL_MS) { fail(g, "Robot belum menyetujui HP ini (jendela 60 detik habis). Tahan sensor kepala lagi, lalu coba lagi."); g.disconnect(); return@post }
                    PhoneBridgeStatus.stage = "Menunggu robot menyetujui HP ini"; setPairStep(3); PhoneBridgeStatus.text = "Bond created; waiting for the robot to approve this phone."
                    PhoneBridgeStatus.setPhase(BridgeConnectionPhase.PAIRING, connected = true, authenticated = false)
                    handler.postDelayed({ if (g === gatt && !ready && !busy && !awaitingControl) readIdentity(g) }, 1500L)
                    return@post
                }
                approvalWaitSince = 0L
                if (parsed !is IdentityGate.Parsed.Ok) { fail(g, "Respons identitas robot tidak valid · perbarui firmware robot"); PhoneBridgeStatus.text = "Robot identity response is invalid."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR); g.disconnect(); return@post }
                val id = parsed.robotId; val flags = parsed.flags; robotId = id; credentialStore = BridgeCredentialStore(context); val saved = credentialStore!!.read()
                storedCredential = saved as? BridgeCredentialStore.ReadResult.Available
                if (saved is BridgeCredentialStore.ReadResult.RePairRequired) { if (!credentialStore!!.clear()) { fail(g, "Data kepercayaan tersimpan rusak dan tidak bisa dihapus"); PhoneBridgeStatus.text = "Stored trust data is invalid and could not be removed."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR); g.disconnect(); return@post }; storedCredential = null }
                if (storedCredential?.robotId != id.toHex()) { storedCredential?.key?.fill(0); storedCredential = null }
                when (IdentityGate.decide(flags, storedCredential != null)) {
                    IdentityGate.Verdict.KEY_MISSING -> { fail(g, "Kunci kepercayaan robot hilang di HP · lupakan robot lalu pasangkan ulang"); PhoneBridgeStatus.text = "Robot trust key is missing. Forget the robot and pair this phone again."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR); g.disconnect(); return@post }
                    IdentityGate.Verdict.NO_CREDENTIAL -> { search.end(); fail(g, "Robot menyetujui bond tetapi tidak membuka pendaftaran kunci Android · kemungkinan robot disetel untuk iPhone (reset pabrik lalu pasangkan ulang)"); PhoneBridgeStatus.text = "Robot has no active phone credential."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.PAIRING); g.disconnect(); return@post }
                    IdentityGate.Verdict.PROCEED -> Unit
                }
                PhoneBridgeStatus.stage = "Menyiapkan koneksi aman"; setPairStep(3)
                val nonce = ByteArray(32); SecureRandom().nextBytes(nonce); phoneNonce = nonce
                val hello = BridgeSessionHandshake.hello(nonce) ?: run { fail(g, "Handshake tidak bisa dimulai"); PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR); g.disconnect(); return@post }
                beginWrite(CONTROL, hello); PhoneBridgeStatus.text = if ((flags and 4L) != 0L) "Securing the approved robot session." else "Authenticating the selected robot."; return@post
            }
            if (uuid == ALLOWLIST) {
                PhoneBridgeStatus.setPhase(BridgeConnectionPhase.POLICY, connected = true, authenticated = true)
                allowed = value.toString(Charsets.UTF_8).split(Regex("""[\r\n;\s]+""")).filter { it.isNotBlank() }.toSet()
                val config = g.getService(SERVICE)?.getCharacteristic(CONFIG)
                if (config != null) { busy = true; operationAt = SystemClock.elapsedRealtime(); if (!g.readCharacteristic(config)) { busy = false; g.disconnect() }; return@post }
                robotNavigation = false
            } else if (uuid == CONFIG) {
                val fields = value.toString(Charsets.UTF_8).split('\n')
                if (fields.size != 3 || fields[0] != "1" || fields[1] != "android") { PhoneBridgeStatus.text = "This robot is not set for Android; factory reset it and pair again."; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.POLICY, connected = true, authenticated = true); g.disconnect(); return@post }
                robotNavigation = fields[2] == "1" && g.getService(SERVICE)?.getCharacteristic(NAV) != null
            }
            ready = true; linkFailures = 0; PhoneBridgeStatus.staleBond = false; backoff.reset(); PhoneBridgeStatus.reconnectAttempt = 0; handshakeGuard.onReady(); PhoneBridgeStatus.handshakeFailures = 0; PhoneBridgeStatus.failure = null; PhoneBridgeStatus.stage = null; search.end(); approvalWaitSince = 0L
            val handled = host?.onReady() ?: false
            PhoneBridgeStatus.text = "Connected. Notifications filtered by robot allowlist. Navigation: ${if (robotNavigation) "ready" else "unavailable"}."
            PhoneBridgeStatus.setPhase(BridgeConnectionPhase.READY, connected = true, authenticated = true, navigationEnabled = robotNavigation); updateQueueState()
            if (!handled && sharingPaused()) { clearPending(); navLatest?.bytes?.fill(0); navLatest = null; pending.addFirst(RobotPacket(5, "", "", byteArrayOf())); updateQueueState() }
            sendNext()
        }
    }

    /** Sends the next queued item: settings requests first (scheduler lanes), then bridge packets. */
    fun sendNext() {
        if (!ready || busy || awaitingAck || awaitingControl) return
        val g = gatt ?: return
        if (!forgetInProgress && connectedAddress != TrustedRobotStore.address(context)) { clearPending(); navLatest?.bytes?.fill(0); navLatest = null; disconnect(); return }
        if (scheduler.inFlight) return
        if (scheduler.hasWaiting) { pumpScheduler(); if (scheduler.inFlight || busy) return }
        val now = SystemClock.elapsedRealtime()
        val rejected = pending.filter { (it.kind != 5 && now - it.at > if (it.kind == 6) 30000 else 300000) || (it.kind == 1 && !allowed.contains(it.pkg)) }
        rejected.forEach { it.bytes.fill(0); droppedCount++ }
        pending.removeAll { (it.kind != 5 && now - it.at > if (it.kind == 6) 30000 else 300000) || (it.kind == 1 && !allowed.contains(it.pkg)) }
        val packet = navLatest?.takeIf { robotNavigation && now - it.at < 120000 } ?: pending.firstOrNull() ?: return
        val age = if (packet.kind == 5) 0L else now - packet.at
        if ((packet.kind == 3 && age >= 120000) || (packet.kind == 6 && age >= 30000) || (packet.kind != 3 && packet.kind != 5 && packet.kind != 6 && age >= 300000)) {
            if (packet.kind == 3) { packet.bytes.fill(0); navLatest = null } else { packet.bytes.fill(0); pending.remove(packet); droppedCount++ }
            updateQueueState(); sendNext(); return
        }
        val envelope = sessionSender?.create(packet.kind, age, packet.bytes) ?: run { PhoneBridgeStatus.text = "Authenticated bridge session expired."; g.disconnect(); return }
        val counter = readU32le(envelope, 12); inFlight = packet; inFlightCounter = counter
        if (packet.kind == 6) { pendingCommandResultCounter = counter; pendingCommandResultAt = now }
        awaitingAck = true
        beginWrite(ENVELOPE, envelope)
    }

    private fun readIdentity(g: BluetoothGatt) {
        val identity = g.getService(SERVICE)?.getCharacteristic(IDENTITY) ?: run { g.disconnect(); return }
        busy = true; operationAt = SystemClock.elapsedRealtime()
        if (!g.readCharacteristic(identity)) { busy = false; g.disconnect() }
    }

    /** Records a specific failure line that survives the disconnect which follows (shown in onboarding and Koneksi). */
    private fun fail(g: BluetoothGatt, message: String) { PhoneBridgeStatus.failure = message; PhoneBridgeStatus.stage = null }

    private fun enableNextNotification(g: BluetoothGatt) {
        if (g !== gatt) return
        if (notificationSetupIndex >= 2) { readIdentity(g); return }
        val uuid = if (notificationSetupIndex == 0) CONTROL else ACK
        val characteristic = g.getService(SERVICE)?.getCharacteristic(uuid) ?: run { g.disconnect(); return }
        val descriptor = characteristic.getDescriptor(CCCD) ?: run { g.disconnect(); return }
        if (!g.setCharacteristicNotification(characteristic, true)) { g.disconnect(); return }
        busy = true; operationAt = SystemClock.elapsedRealtime()
        val started = if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS else { @Suppress("DEPRECATION") run { descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; g.writeDescriptor(descriptor) } }
        if (!started) { busy = false; g.disconnect() }
    }

    private fun beginWrite(target: UUID, bytes: ByteArray) {
        if (busy || bytes.isEmpty()) return
        val frames = BridgeGattFraming.fragments(bytes, mtu, SecureRandom().nextInt(0xffff) + 1); bytes.fill(0)
        if (frames == null) { PhoneBridgeStatus.text = "Could not frame authenticated bridge data at this MTU."; gatt?.disconnect(); return }
        writeFrames = frames; writeFrameIndex = 0; writeTarget = target; busy = true; operationAt = SystemClock.elapsedRealtime(); writeOneFrame(gatt ?: return)
    }

    private fun writeOneFrame(g: BluetoothGatt) {
        val uuid = writeTarget ?: return
        val characteristic = g.getService(SERVICE)?.getCharacteristic(uuid) ?: run { g.disconnect(); return }
        val frame = writeFrames.getOrNull(writeFrameIndex) ?: run { g.disconnect(); return }
        val started = if (Build.VERSION.SDK_INT >= 33) g.writeCharacteristic(characteristic, frame, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS else { @Suppress("DEPRECATION") run { characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT; characteristic.value = frame; g.writeCharacteristic(characteristic) } }
        if (!started) { busy = false; g.disconnect() }
    }

    private fun onBridgeNotification(g: BluetoothGatt, uuid: UUID, value: ByteArray) {
        handler.post {
            if (g !== gatt) return@post
            when (uuid) {
                CONTROL -> { val complete = controlReassembler.accept(value, SystemClock.elapsedRealtime()) ?: return@post; handleControlResponse(g, complete) }
                ACK -> { val complete = ackReassembler.accept(value, SystemClock.elapsedRealtime()) ?: return@post; handleApplicationAck(complete) }
            }
        }
    }

    private fun handleControlResponse(g: BluetoothGatt, frame: ByteArray) {
        val challenge = pendingChallenge
        if (challenge == null) {
            val id = robotId ?: return; val nonce = phoneNonce ?: return
            val existing = storedCredential?.key
            val parsed = BridgeSessionHandshake.parseChallenge(frame, id, nonce, existing)
            if (parsed == null) { PhoneBridgeStatus.failure = "Robot gagal membuktikan identitas (kunci tidak cocok) · lupakan robot lalu pasangkan ulang"; PhoneBridgeStatus.text = "Robot authentication failed. Check pairing."; g.disconnect(); return }
            if (parsed.enrollment) {
                if (storedCredential != null && !storedCredential!!.pending && !MessageDigest.isEqual(storedCredential!!.key, parsed.key)) { parsed.key.fill(0); PhoneBridgeStatus.failure = "Kunci robot berubah · lupakan robot lalu pasangkan ulang"; PhoneBridgeStatus.text = "Robot key changed; pair it again."; g.disconnect(); return }
                if (credentialStore?.savePending(id.toHex(), parsed.key) != true) { parsed.key.fill(0); PhoneBridgeStatus.failure = "Kunci robot tidak bisa disimpan aman di HP"; PhoneBridgeStatus.text = "Could not store the robot credential securely."; g.disconnect(); return }
            }
            pendingChallenge = parsed; awaitingControl = false; val finish = BridgeSessionHandshake.finish(parsed); beginWrite(CONTROL, finish); PhoneBridgeStatus.text = "Robot identity verified; completing secure pairing."
            return
        }
        if (!BridgeSessionHandshake.verifyAccepted(challenge, frame)) { PhoneBridgeStatus.failure = "Konfirmasi sesi robot gagal"; PhoneBridgeStatus.text = "Robot session confirmation failed."; g.disconnect(); return }
        val id = robotId ?: return
        if (credentialStore?.markActive(id.toHex()) != true) { PhoneBridgeStatus.text = "Credential activation could not be confirmed on this phone."; g.disconnect(); return }
        settingsSessionId = challenge.sessionId; scheduler.onSessionStart(challenge.sessionId)
        sessionSender = BridgeAuthenticatedEnvelope.SessionSender(challenge.key, challenge.sessionId, BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT)
        ackReceiver = BridgeAuthenticatedEnvelope.SessionReceiver(challenge.key, challenge.sessionId, BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE, mapOf(0x7f to 20_000L, 0x7e to 20_000L, 0x7d to 0L))
        challenge.key.fill(0); pendingChallenge = null; storedCredential?.key?.fill(0); storedCredential = null; phoneNonce?.fill(0); phoneNonce = null; awaitingControl = false
        val allowlist = g.getService(SERVICE)?.getCharacteristic(ALLOWLIST) ?: run { g.disconnect(); return }
        busy = true; operationAt = SystemClock.elapsedRealtime(); if (!g.readCharacteristic(allowlist)) { busy = false; g.disconnect() }; PhoneBridgeStatus.text = "Secure robot session established; loading its sharing policy."
    }

    private fun handleApplicationAck(wire: ByteArray) {
        val message = ackReceiver?.accept(wire) ?: return
        if (message.kind == 0x7d) {
            val response = BridgeSettingsProtocol.parseResponse(message.payload)
            if (response != null && scheduler.onResponse(response, SystemClock.elapsedRealtime(), settingsSessionId)) rejectGuard.onAccepted()
            sendNext(); pumpScheduler(); return
        }
        if (message.kind == 0x7e) { if (message.payload.size != 6 || readU32le(message.payload, 0) != pendingCommandResultCounter || pendingCommandResultCounter == 0L) return; val status = message.payload[4].toInt() and 0xff; val reason = message.payload[5].toInt() and 0xff; pendingCommandResultCounter = 0; PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(companionCommandStatus = if (status == 0) "Robot confirmed the command was applied." else "Robot rejected the command: ${commandReason(reason)}."); return }
        if (message.kind == 0x7f && message.payload.size == 6 && (message.payload[4].toInt() and 0xff) != 0 && scheduler.onRejectedAck(readU32le(message.payload, 0), settingsSessionId)) {
            // Replay/revoke: the robot did not process the request. One fresh session; repeated refusals stop the reconnect (no loop).
            if (rejectGuard.onRejected() == SessionRejectGuard.Action.GIVE_UP) {
                disconnect(reconnect = false)
                PhoneBridgeStatus.text = "Robot menolak sesi berulang kali; lupakan robot lalu pasangkan ulang."
                PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR, connected = false, authenticated = false)
            } else disconnect()
            return
        }
        if (message.kind != 0x7f || message.payload.size != 6 || readU32le(message.payload, 0) != inFlightCounter) return
        val packet = inFlight ?: return; val status = message.payload[4].toInt() and 0xff
        if (packet.kind == 3) { if (navLatest === packet) navLatest = null } else pending.remove(packet)
        packet.bytes.fill(0); inFlight = null; inFlightCounter = 0; awaitingAck = false; PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(lastAcknowledgedAt = System.currentTimeMillis())
        if (packet.kind == 4) { val revokedAddress = connectedAddress; forgetInProgress = false; sessionSender?.close(); sessionSender = null; ackReceiver?.close(); ackReceiver = null; PhoneBridgeStatus.setPhase(BridgeConnectionPhase.REVOKED, connected = false, authenticated = false); disconnect(); PhoneBridgeStatus.setPhase(BridgeConnectionPhase.REVOKED, connected = false, authenticated = false); revokedAddress?.let { TrustedRobotStore.removeBond(context, it) }; PhoneBridgeStatus.text = if (status == 0 && PhoneBridgeStatus.localForgetVerified) "Robot confirmed peer revocation; local credential has been removed." else if (status == 0) "Robot confirmed peer revocation, but local credential cleanup needs review in Android settings." else "Local credential is removed, but the robot did not confirm peer revocation. Remove trust on the robot with a factory reset."; return }
        if (packet.kind == 5 || packet.kind == 2) { host?.onPacketAcked(packet, status); updateQueueState(); sendNext(); return }
        if (packet.kind == 6) { if (status != 0) pendingCommandResultCounter = 0; pendingCommandResultAt = SystemClock.elapsedRealtime(); PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(companionCommandStatus = if (status == 0) "Gateway accepted the command; waiting for robot confirmation." else "Gateway rejected the command (reason ${message.payload[5].toInt() and 0xff})."); sendNext(); return }
        PhoneBridgeStatus.text = if (status == 0) "Robot acknowledged the update." else "Robot rejected an update (reason ${message.payload[5].toInt() and 0xff})."
        sendNext()
    }

    private fun commandReason(reason: Int) = when (reason) { 1 -> "robot link unavailable"; 2 -> "robot busy or safety gate active"; 3 -> "command unsupported or invalid"; 4 -> "robot could not persist the setting"; else -> "robot unavailable" }
    private fun readU32le(bytes: ByteArray, offset: Int): Long = (bytes[offset].toLong() and 0xff) or ((bytes[offset + 1].toLong() and 0xff) shl 8) or ((bytes[offset + 2].toLong() and 0xff) shl 16) or ((bytes[offset + 3].toLong() and 0xff) shl 24)
    private fun ByteArray.toHex() = joinToString("") { "%02x".format(it.toInt() and 0xff) }

    /** Ends the session: wipes secrets, fails queued settings requests (mutating in-flight => UNCERTAIN) and schedules a reconnect. */
    fun disconnect(reconnect: Boolean = true) {
        val localRevokeIncomplete = forgetInProgress
        if (!ready && !forgetInProgress && !search.active(SystemClock.elapsedRealtime()) && PhoneBridgeStatus.state.phase.let { it == BridgeConnectionPhase.AUTHENTICATING || it == BridgeConnectionPhase.POLICY || it == BridgeConnectionPhase.PAIRING }) {
            handshakeGuard.onHandshakeFailed(); PhoneBridgeStatus.handshakeFailures = handshakeGuard.failures
        }
        val commandOutcomeUnknown = pendingCommandResultCounter != 0L || inFlight?.kind == 6 || pending.any { it.kind == 6 }
        if (commandOutcomeUnknown) {
            inFlight?.takeIf { it.kind == 6 }?.bytes?.fill(0); pending.filter { it.kind == 6 }.forEach { it.bytes.fill(0) }; pending.removeAll { it.kind == 6 }
            PhoneBridgeStatus.state = PhoneBridgeStatus.state.copy(companionCommandStatus = "Connection lost during command delivery; check robot status before retrying.")
        }
        stopFinder(); approvalWaitSince = 0L; PhoneBridgeStatus.stage = null; forgetInProgress = false; val old = gatt; gatt = null; connectedAddress = null; old?.close(); ready = false; busy = false; awaitingAck = false; awaitingControl = false
        pendingCommandResultCounter = 0; inFlight?.bytes?.fill(0); inFlight = null; inFlightCounter = 0
        writeFrames.forEach { it.fill(0) }; writeFrames = emptyList(); writeFrameIndex = 0; writeTarget = null
        robotNavigation = false; rssiDbm = null; mtu = 23; controlReassembler.reset(); ackReassembler.reset()
        phoneNonce?.fill(0); phoneNonce = null; pendingChallenge?.key?.fill(0); pendingChallenge = null; storedCredential?.key?.fill(0); storedCredential = null
        handler.removeCallbacks(schedulerTick); scheduler.onDisconnected(); settingsSessionId = 0L
        sessionSender?.close(); sessionSender = null; ackReceiver?.close(); ackReceiver = null; robotId?.fill(0); robotId = null; credentialStore = null
        updateQueueState()
        if (!localRevokeIncomplete) PhoneBridgeStatus.setPhase(if (TrustedRobotStore.address(context) == null) BridgeConnectionPhase.IDLE else BridgeConnectionPhase.OFFLINE, connected = false, authenticated = false, navigationEnabled = false)
        if (localRevokeIncomplete) PhoneBridgeStatus.text = "Local credential is removed, but the robot did not confirm peer revocation. Remove trust on the robot with a factory reset." else PhoneBridgeStatus.text = "Robot disconnected; retrying."
        if (reconnect) scheduleReconnect()
    }

    private fun scheduleReconnect() {
        if (!active || TrustedRobotStore.address(context) == null) return
        val base = backoff.next(); PhoneBridgeStatus.reconnectAttempt = backoff.attempt
        reconnectCount++; updateQueueState(); PhoneBridgeStatus.republishLink()
        val jitter = SecureRandom().nextInt((base / 10).toInt().coerceAtLeast(1)).toLong()
        handler.removeCallbacks(reconnectTask); handler.postDelayed(reconnectTask, if (search.active(SystemClock.elapsedRealtime())) PairingSearch.RETRY_MS else base + jitter)
    }

    private val reconnectTask = Runnable { ensureConnected() }
}

/**
 * Foreground service (type connectedDevice) that owns the only GATT connection and HMAC session.
 * RoboNotificationListener and the UI are clients. Not yet exercised on a device.
 */
class RobotLinkService : Service() {
    companion object {
        private const val CHANNEL = "robot_link"
        private const val NOTIFICATION_ID = 41

        /** Starts the service from the foreground, boot or listener binding; failures (background start limits) are swallowed. */
        fun start(context: Context) {
            try { context.startForegroundService(Intent(context, RobotLinkService::class.java)) } catch (_: Exception) { }
        }
    }

    private var core: RobotLinkCore? = null
    private var unobserve: (() -> Unit)? = null
    private val bluetoothReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, intent: Intent) {
            if (intent.action == BluetoothDevice.ACTION_BOND_STATE_CHANGED) {
                LinkDiag.log("bond ${intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, -1)}->${intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)} alasan=${intent.getIntExtra("android.bluetooth.device.extra.REASON", -1)}")
                val bondState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)
                val address = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)?.address
                if (address != null) core?.onBondStateChanged(address, bondState)
                return
            }
            if (intent.action != BluetoothAdapter.ACTION_STATE_CHANGED) return
            core?.onBluetoothStateChanged(intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR) == BluetoothAdapter.STATE_ON)
        }
    }

    override fun onCreate() {
        super.onCreate()
        getSystemService(NotificationManager::class.java).createNotificationChannel(NotificationChannel(CHANNEL, "Koneksi robot", NotificationManager.IMPORTANCE_LOW))
        enterForeground(LinkState())
        val created = RobotLinkCore(this)
        core = created
        RobotLinkHub.core = created
        registerReceiver(bluetoothReceiver, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED).apply { addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED) })
        unobserve = RobotLink.state.observe { state ->
            try { getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, buildNotification(state)) } catch (_: Exception) { }
        }
        created.start()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        core?.handler?.post { core?.ensureConnected() }
        return START_STICKY
    }

    override fun onDestroy() {
        unobserve?.invoke(); unobserve = null
        try { unregisterReceiver(bluetoothReceiver) } catch (_: Exception) { }
        core?.stop()
        RobotLinkHub.core = null
        core = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun enterForeground(state: LinkState) {
        val notification = buildNotification(state)
        try {
            if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            else startForeground(NOTIFICATION_ID, notification)
        } catch (_: Exception) { stopSelf() }
    }

    private fun buildNotification(state: LinkState): Notification {
        val text = when (state.phase) {
            LinkPhase.READY -> "Terhubung ke RoboDesk"
            LinkPhase.RECONNECTING -> "Menyambung ulang ke RoboDesk (${state.attempt})"
            LinkPhase.ERROR -> state.cause ?: "Kesalahan koneksi"
            LinkPhase.OFFLINE -> "Belum terhubung"
            else -> "Menyambung ke RoboDesk"
        }
        return Notification.Builder(this, CHANNEL).setSmallIcon(android.R.drawable.stat_sys_data_bluetooth).setContentTitle("RoboDesk").setContentText(text)
            .setOngoing(true).setOnlyAlertOnce(true).build()
    }
}

/** Restarts the link after the phone boots (the service then reconnects to the stored robot). */
class RobotLinkBootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED || intent.action == Intent.ACTION_MY_PACKAGE_REPLACED) RobotLinkService.start(context)
    }
}
