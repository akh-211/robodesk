package com.robodesk.phonebridge.ui

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.robodesk.phonebridge.R
import com.robodesk.phonebridge.BridgeCredentialStore
import com.robodesk.phonebridge.BridgeSettingsClient
import com.robodesk.phonebridge.ChatController
import com.robodesk.phonebridge.ChatPort
import com.robodesk.phonebridge.BridgeSettingsProtocol as P
import com.robodesk.phonebridge.Done
import com.robodesk.phonebridge.PhoneBridgeStatus
import com.robodesk.phonebridge.RobotScanner
import com.robodesk.phonebridge.ForgetPort
import com.robodesk.phonebridge.Allowlist
import com.robodesk.phonebridge.LinkNotices
import com.robodesk.phonebridge.OtaTracker
import com.robodesk.phonebridge.UiNotice
import com.robodesk.phonebridge.LinkPhase
import com.robodesk.phonebridge.LinkState
import com.robodesk.phonebridge.MemoryCacheStore
import com.robodesk.phonebridge.MoodDiary
import com.robodesk.phonebridge.OtaResult
import com.robodesk.phonebridge.PhoneSettingsBridge
import com.robodesk.phonebridge.Res
import com.robodesk.phonebridge.RobotLink
import com.robodesk.phonebridge.LinkDiag
import com.robodesk.phonebridge.RobotLinkHub
import com.robodesk.phonebridge.RobotLinkService
import com.robodesk.phonebridge.RobotRepository
import com.robodesk.phonebridge.RobotSettings
import com.robodesk.phonebridge.StatusView
import com.robodesk.phonebridge.TrustedRobotStore
import com.robodesk.phonebridge.UpdateManifest

/** Bottom sheets of the mockup (`openSheet`). [arg] carries the Wi-Fi slot. */
enum class SheetKind { REBOOT, WIFI, CLEAR_MEMORY, CLEAR_EXPERIENCES, OTA, FORGET, WIPE, CLEAR_INTEGRATIONS, RESET_LEARNING, CLOUD_CONSENT, WIPE_DATA, NOTIF_ACCESS }

// PD-4.2 five-tab IA (mockup android_app_mockup_5tab.html). Titles live in strings.xml (ID default, EN).
val TAB_TITLES = listOf(R.string.tab_robot, R.string.tab_chat, R.string.tab_play, R.string.tab_memories, R.string.tab_settings)
val SUB_TITLES = mapOf(
    "act" to R.string.sub_activities, "int" to R.string.sub_integrations, "ir" to R.string.sub_ir, "snd" to R.string.sub_sound,
    "rem" to R.string.sub_reminders, "net" to R.string.sub_network, "help" to R.string.sub_help, "diag" to R.string.sub_diagnostics,
    "upd" to R.string.sub_update, "dng" to R.string.sub_danger, "abt" to R.string.sub_about, "priv" to R.string.sub_privacy
)
/** Sub-screens opened from another sub-screen return there on back. */
val SUB_PARENT = mapOf("diag" to "help", "abt" to "help")

/**
 * One process-wide UI state holder (survives rotation). Everything robot-related goes through [repo]; this class only
 * adapts callbacks to Compose state. All methods run on the main thread (the link service posts there too).
 */
object RoboApp {
    private lateinit var ctx: Context
    private val main = Handler(Looper.getMainLooper())

    val repo = RobotRepository(BridgeSettingsClient(PhoneSettingsBridge))
    val toast = ToastState()

    var link by mutableStateOf(LinkState())
    var tab by mutableIntStateOf(0)
    var sub by mutableStateOf<String?>(null)
    var sheet by mutableStateOf<SheetKind?>(null)
    var sheetArg by mutableIntStateOf(0)
    var onboarding by mutableStateOf(false)
    var hasRobot by mutableStateOf(false)
    var foreground by mutableStateOf(false)
    /** Bumped on every activity resume so permission/listener-access status is re-read. */
    var resumeTick by mutableIntStateOf(0)

    var status by mutableStateOf<StatusView?>(null)
    var settings by mutableStateOf<RobotSettings?>(null)
    var settingsLoading by mutableStateOf(false)
    var settingsError by mutableStateOf<String?>(null)
    var saving by mutableStateOf(false)
    var busy by mutableIntStateOf(0)
    /** Bumped on every draft edit so composables that read it recompose. */
    var tick by mutableIntStateOf(0)
    /** Bumped when the draft is replaced from the robot so text fields re-read their value. */
    var loadGen by mutableIntStateOf(0)

    private var draftDoc: RobotSettings? = null
    val draft: RobotSettings? get() { tick; return draftDoc }
    val dirty: Boolean get() { tick; return draftDoc?.dirty == true }
    val ready: Boolean get() = link.ready
    /** Writes are off while disconnected, saving, or while the robot is updating (the update screen owns the app). */
    val canWrite: Boolean get() = ready && !saving && otaResult == null && otaStartedAt == 0L

    // OTA (GAP-D)
    private val ota = OtaTracker({ SystemClock.elapsedRealtime() }, { repo.otaResult(it) })
    var otaStartedAt by mutableStateOf(0L)
    var otaResult by mutableStateOf<OtaResult?>(null)
    private fun syncOta() { otaStartedAt = ota.startedAt; otaResult = ota.result }

    // A10: bluetooth / permission state for the notice under the appbar (re-read every second)
    var bluetoothOn by mutableStateOf(true)
    var nearbyGranted by mutableStateOf(true)
    /** From the robot's advertisement (see [watchBeacon]): it is nearby / its pairing window is open. Refreshed every second. */
    /** False while a robot is selected but the phone holds no trust record for it (never finished pairing): the link stays idle. */
    var paired by mutableStateOf(true)
    var robotSeen by mutableStateOf(false)
    var robotPairingOpen by mutableStateOf(false)
    val notice: UiNotice?
        get() = LinkNotices.compute(link, hasRobot, bluetoothOn, nearbyGranted, otaStartedAt != 0L && otaResult == null,
            status?.s, robotSeen = robotSeen, robotPairingOpen = robotPairingOpen, paired = paired) // iPhone-mode banner lives on Beranda (HomeScreen) from the settings document
    var latestVersion by mutableStateOf<String?>(null)
    var latestNewer by mutableStateOf(false)

    /** Onboarding "jendela pairing" step is showing: the link core retries fast and does not count rejected tries as failures. */
    var pairingMode by mutableStateOf(false)
        private set
    var searching by mutableStateOf(false)

    // Chat with the robot (RAM only)
    var chatVersion by mutableIntStateOf(0)
        private set
    val chat = ChatController(object : ChatPort {
        override fun send(text: String, done: Done<Long>) = repo.chatSend(text, done)
        override fun poll(turn: Long, offset: Int, done: Done<P.ChatPoll>) = repo.chatPoll(turn, offset, done)
    }, { SystemClock.elapsedRealtime() })
    /** Last connection steps from [LinkDiag], shown on the pairing step. */
    var diag by mutableStateOf("")
        private set
    private var searchStarted = false
    fun enterPairingMode(on: Boolean) {
        pairingMode = on
        if (!on) { searchStarted = false; searching = false; RobotLinkHub.core?.endPairingSearch() }
    }
    /** "Coba lagi": run another 2-minute search (clears the earlier failure). */
    fun retryPairingSearch() { searchStarted = false }

    private var wasReady = false
    private var pollerAttached = false

    fun init(context: Context) {
        if (::ctx.isInitialized) return
        ctx = context.applicationContext
        hasRobot = TrustedRobotStore.address(ctx) != null
        onboarding = !hasRobot
        RobotLink.state.observe { st -> main.post { onLink(st) } }
        chat.onChange = { chatVersion++ }
        main.post(object : Runnable { override fun run() { if (chat.busy) chat.tick(); main.postDelayed(this, 600) } })
        LinkDiag.text.observe { t -> main.post { diag = t } }
        main.post(object : Runnable {
            override fun run() { housekeeping(); main.postDelayed(this, 1000) }
        })
    }

    val context: Context get() = ctx

    // ---------------------------------------------------------------- link and polling

    private fun onLink(st: LinkState) {
        link = st
        if (st.ready && !wasReady) onReady()
        ota.onLink(st.ready); syncOta()
        wasReady = st.ready
    }

    private fun onReady() {
        refreshStatus()
        if (!dirty) loadSettings()
        refreshStatus { s -> ota.onStatus(s.s); syncOta() }
    }

    private fun needsStatus() = (sub == null && tab == 0) || sub in listOf("diag", "upd", "act", "ir", "int", "net", "help") || otaStartedAt != 0L

    private fun housekeeping() {
        val core = RobotLinkHub.core ?: run { searching = false; return }
        core.poller.screenNeedsStatus = needsStatus()
        if (pairingMode && !searchStarted && !ready) { searchStarted = true; core.beginPairingSearch() }
        searching = pairingMode && core.searching()
        if (!pollerAttached) {
            pollerAttached = true
            core.poller.latest.observe { s -> if (s != null) main.post { status = StatusView(s); ota.onStatus(s); syncOta() } }
        }
        ota.tick(); syncOta()
        watchBeacon()
        paired = !hasRobot || try { BridgeCredentialStore(ctx).hasRecord() } catch (_: Exception) { true }
        try {
            bluetoothOn = ctx.getSystemService(android.bluetooth.BluetoothManager::class.java)?.adapter?.isEnabled != false
            nearbyGranted = android.os.Build.VERSION.SDK_INT < 31 || ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) == android.content.pm.PackageManager.PERMISSION_GRANTED
        } catch (_: Exception) { }
    }

    // ---------------------------------------------------------------- pairing beacon

    private var beacon: RobotScanner? = null
    private var notReadySince = 0L
    private var seenAt = 0L
    private var pairingOpenAt = 0L

    /**
     * While the app is open and the selected robot is not connected for 8 s, watch its advertisement (low-power scan, no connection):
     * [robotSeen] = still nearby, [robotPairingOpen] = its pairing window is open, which after a robot-side reset means it waits for a new pairing.
     */
    private fun watchBeacon() {
        val now = SystemClock.elapsedRealtime()
        if (ready || !hasRobot || onboarding) notReadySince = 0L else if (notReadySince == 0L) notReadySince = now
        val want = foreground && notReadySince != 0L && now - notReadySince >= 8000L && bluetoothOn && nearbyGranted
        if (want && beacon == null) {
            val scanner = RobotScanner(ctx)
            beacon = scanner
            scanner.start({ list ->
                val target = TrustedRobotStore.address(ctx)
                val r = list.firstOrNull { it.address.equals(target, true) } ?: return@start
                main.post { val t = SystemClock.elapsedRealtime(); seenAt = t; pairingOpenAt = if (r.pairingOpen == true) t else 0L }
            }, { main.post { beacon = null } }, android.bluetooth.le.ScanSettings.SCAN_MODE_LOW_POWER)
        } else if (!want && beacon != null) { beacon?.stop(); beacon = null; seenAt = 0L; pairingOpenAt = 0L }
        robotSeen = seenAt != 0L && now - seenAt < 7000L
        val coreSaw = PhoneBridgeStatus.beaconOpenAt
        robotPairingOpen = (pairingOpenAt != 0L && now - pairingOpenAt < 7000L) || (coreSaw != 0L && now - coreSaw < 9000L)
    }

    fun refreshStatus(extra: (StatusView) -> Unit = {}) {
        if (!ready) return
        repo.readStatus { r -> if (r is Res.Ok) { status = r.value; recordMood(r.value); extra(r.value) } }
    }

    // PD-3.4 opt-in mood diary (phone only, encrypted, 7 days, no transcript)
    var moodDiaryOn by mutableStateOf(false)
    var moodDiary by mutableStateOf<List<MoodDiary.Entry>>(emptyList())
    private var moodDiaryRead = false
    fun loadMoodDiary() {
        if (moodDiaryRead) return
        moodDiaryRead = true
        moodDiaryOn = MemoryCacheStore.diaryEnabled(ctx)
        if (moodDiaryOn) moodDiary = MemoryCacheStore.readDiary(ctx)
    }
    private fun recordMood(s: StatusView) {
        loadMoodDiary()
        if (!moodDiaryOn) return
        val next = MoodDiary.record(moodDiary, System.currentTimeMillis() / 1000, s.s.moodId, s.s.sleepState)
        if (next != moodDiary) { moodDiary = next; MemoryCacheStore.saveDiary(ctx, next) }
    }
    fun setMoodDiary(on: Boolean) { MemoryCacheStore.setDiaryEnabled(ctx, on); moodDiaryOn = on; if (!on) moodDiary = emptyList() }
    fun clearMoodDiary() { MemoryCacheStore.clearDiary(ctx); moodDiary = emptyList() }

    // ---------------------------------------------------------------- generic helpers

    fun say(text: String) = toast.show(text)

    /** Runs a repository call: refuses when not READY, tracks the busy counter, toasts errors (and [okToast] on success). */
    fun <T> call(okToast: String? = null, start: (Done<T>) -> Unit, onOk: (T) -> Unit = {}, onErr: (Res.Err) -> Unit = {}) {
        if (!ready) { say("Sesi BLE berakhir · menyambung ulang…"); return }
        busy++
        start { r ->
            busy--
            when (r) {
                is Res.Ok -> { if (okToast != null) say(okToast); onOk(r.value) }
                is Res.Err -> { say(r.message); onErr(r) }
            }
        }
    }

    // ---------------------------------------------------------------- settings (shared dirty state for Beranda and Setelan)

    fun loadSettings() {
        if (!ready || settingsLoading) return
        settingsLoading = true
        settingsError = null
        repo.loadSettings { r ->
            settingsLoading = false
            when (r) {
                is Res.Ok -> { settings = r.value; draftDoc = r.value.copy(); loadGen++; tick++ }
                is Res.Err -> { settingsError = r.message; say(r.message) }
            }
        }
    }

    /** Applies an edit to the shared draft; the SaveBar appears on the first change. */
    /**
     * A toggle takes effect at once: the draft shows the new value immediately and ONLY this key is saved (other unsaved edits stay
     * pending). On failure the toggle returns to the robot's value and the reason is shown.
     */
    fun toggle(key: String, value: Boolean, after: ((Boolean) -> Unit)? = null) = setNow(key, if (value) 1 else 0) { after?.invoke(it != 0) }

    /** Same as [toggle] for a small numeric choice (a select with a few options). */
    fun setNow(key: String, value: Int, after: ((Int) -> Unit)? = null) {
        val d = draftDoc ?: return
        val base = settings ?: return
        if (!canWrite) { say("Terputus: tidak bisa mengubah"); return }
        d.setInt(key, value); tick++
        val one = base.copy().also { it.setInt(key, value) }
        saving = true
        repo.saveSettings(one) { r ->
            saving = false
            when (r) {
                is Res.Ok -> {
                    val fresh = r.value.reread
                    RobotLinkHub.core?.setAllowed(Allowlist.parse(fresh.string("notificationAllowlist")))
                    val pending = (draftDoc ?: d).changedKeys().filter { it != key }
                    val next = fresh.copy()
                    pending.forEach { k -> (draftDoc ?: d).let { src -> next.copyKeyFrom(src, k) } }
                    settings = fresh; draftDoc = next
                    loadGen++; tick++
                    after?.invoke(fresh.int(key))
                }
                is Res.Err -> {
                    (draftDoc ?: d).copyKeyFrom(base, key); tick++
                    say(r.message)
                }
            }
        }
    }

    fun edit(change: (RobotSettings) -> Unit) {
        val d = draftDoc ?: return
        if (!canWrite) { say("Terputus: tidak bisa mengubah"); return }
        change(d)
        tick++
    }

    fun save() {
        val d = draftDoc ?: return
        if (!canWrite) { say("Terputus: tidak bisa menyimpan"); return }
        saving = true
        say("Menyimpan…")
        repo.saveSettings(d) { r ->
            saving = false
            when (r) {
                is Res.Ok -> {
                    RobotLinkHub.core?.setAllowed(Allowlist.parse(r.value.reread.string("notificationAllowlist")))
                    settings = r.value.reread
                    draftDoc = r.value.reread.copy()
                    loadGen++; tick++
                    say(if (r.value.differing.isEmpty()) "Tersimpan & terverifikasi baca-ulang" else "Tersimpan; robot menormalkan: ${r.value.differing.joinToString(", ")}")
                }
                is Res.Err -> say(r.message + if (r.uncertain) "" else " · data lokal dipertahankan")
            }
        }
    }

    fun discardDraft() { draftDoc = settings?.copy(); loadGen++; tick++ }

    // ---------------------------------------------------------------- memory (shared by Memori and the owner-name field)

    var facts by mutableStateOf<List<P.Fact>>(emptyList())
    var memRevision by mutableStateOf<Long?>(null)
    var memLoading by mutableStateOf(false)
    private var memCacheRead = false

    fun loadMemory() {
        if (memLoading) return
        if (!memCacheRead) {
            memCacheRead = true
            MemoryCacheStore.read(ctx)?.let { facts = it.facts; memRevision = it.revision }
        }
        if (!ready) return
        memLoading = true
        repo.loadMemory(memRevision, facts) { r ->
            memLoading = false
            when (r) {
                is Res.Ok -> {
                    facts = r.value.facts
                    if (!r.value.fromCache) { memRevision = r.value.revision; MemoryCacheStore.save(ctx, r.value.revision, r.value.facts) }
                }
                is Res.Err -> say(r.message)
            }
        }
    }

    fun clearMemoryCache() { facts = emptyList(); memRevision = null; MemoryCacheStore.clear(ctx) }

    /** PD-4.14 "hapus semua data" without the dashboard: robot memory + conversation summaries, then the phone caches. Settings stay. */
    fun wipeAllData() {
        call<Long>(null, { d -> repo.clearMemory(d) }, onOk = {
            clearMemoryCache()
            call<Unit>("Semua data dihapus (memori robot, ringkasan obrolan, diary, cache HP)", { d -> repo.experiencesClear(d) }, onOk = { clearMoodDiary() })
        })
    }

    // ---------------------------------------------------------------- forget / onboarding exit

    val forgetPort = object : ForgetPort {
        override fun requestRemoteForget(): Boolean = PhoneBridgeStatus.requestAuthenticatedForget()
        override fun clearLocal(): Boolean {
            val cleared = TrustedRobotStore.forget(ctx)
            PhoneBridgeStatus.localForgetVerified = cleared
            return cleared
        }
    }

    fun forgetRobot() {
        val address = TrustedRobotStore.address(ctx)
        val outcome = repo.forgetRobot(forgetPort)
        if (!outcome.robotRevoking) PhoneBridgeStatus.clearAfterForget()
        resetSession()
        say(outcome.message)
        startOnboarding()
        removeBondWhenFree(address)
    }

    /**
     * Removes the Android bond for a robot that was just forgotten. The revoke packet still needs the link, so wait until the link core is
     * done with it, retry every 1.5 s for 20 s (the removal is asynchronous and can be refused while the link is still closing), and if
     * Android keeps the bond tell the user to remove it in Bluetooth settings.
     */
    private fun removeBondWhenFree(address: String?) {
        if (address == null) return
        val deadline = SystemClock.elapsedRealtime() + 20_000L
        main.post(object : Runnable {
            override fun run() {
                val bond = try { ctx.getSystemService(android.bluetooth.BluetoothManager::class.java)?.adapter?.getRemoteDevice(address)?.bondState } catch (_: Exception) { null }
                if (bond == null || bond == android.bluetooth.BluetoothDevice.BOND_NONE) return
                if (RobotLinkHub.core?.forgetInProgress != true) TrustedRobotStore.removeBond(ctx, address)
                if (SystemClock.elapsedRealtime() < deadline) main.postDelayed(this, 1500L)
                else say("Bond Bluetooth RoboDesk belum terhapus · hapus manual di Setelan Bluetooth")
            }
        })
    }

    /** The robot wiped itself: nothing to revoke remotely; drop local trust and go back to onboarding. */
    fun afterFactoryReset() {
        val address = TrustedRobotStore.address(ctx)
        forgetPort.clearLocal()
        PhoneBridgeStatus.clearAfterForget()
        resetSession()
        startOnboarding()
        removeBondWhenFree(address)
    }

    /**
     * One tap for "the robot and this phone no longer agree" (robot reset, stale bond): drop the local trust, remove the Android bond,
     * and go to onboarding. If Android keeps the bond, onboarding offers the Bluetooth settings.
     */
    /** The button on a notice: pairing for a never-paired robot, otherwise the full re-pair. */
    fun noticeAction(notice: UiNotice) { if (notice.kind == UiNotice.Kind.NOT_PAIRED) startOnboarding() else repairRobot() }

    fun repairRobot() {
        PhoneBridgeStatus.staleBond = false
        afterFactoryReset()
        say("Kepercayaan lama dilepas · tahan sensor kepala robot 5 detik untuk membuka pairing")
    }

    private fun resetSession() {
        chat.clear()
        status = null; settings = null; draftDoc = null; facts = emptyList(); memRevision = null; memCacheRead = false; ota.dismiss(); syncOta()
        tick++
        hasRobot = TrustedRobotStore.address(ctx) != null
    }

    fun startOnboarding() { sub = null; tab = 0; sheet = null; onboarding = true; hasRobot = TrustedRobotStore.address(ctx) != null }

    fun finishOnboarding() { onboarding = false; tab = 0; hasRobot = TrustedRobotStore.address(ctx) != null; RobotLinkService.start(ctx); if (ready) { refreshStatus(); loadSettings() } }

    fun selectRobot(address: String): Boolean {
        val ok = try { TrustedRobotStore.save(ctx, address, null) } catch (_: Exception) { false }
        if (!ok) { say("Robot sebelumnya belum bisa dilepas; robot ini tidak dipilih"); return false }
        PhoneBridgeStatus.clearAfterForget()
        hasRobot = true
        MemoryCacheStore.clear(ctx)
        clearMoodDiary()
        RobotLinkService.start(ctx)
        return true
    }

    // ---------------------------------------------------------------- navigation

    fun openTab(index: Int) { tab = index; sub = null }
    fun openSub(key: String) { sub = key }

    /** Returns true when the back press was consumed. */
    fun back(): Boolean = when {
        sheet != null -> { sheet = null; true }
        sub != null -> { sub = SUB_PARENT[sub]; true }
        tab != 0 -> { tab = 0; true }
        else -> false
    }

    // ---------------------------------------------------------------- update

    fun startOta() {
        call<Unit>("Perintah update diterima robot", { repo.startOta(it) }, onOk = {
            ota.start(); syncOta()
        })
    }

    fun dismissOta() { ota.dismiss(); syncOta() }

    fun checkLatest() {
        say("Memeriksa GitHub…")
        val installed = status?.s?.c3Version ?: 0L
        Thread {
            val text = fetchManifest()
            val res = repo.latestVersion({ text }, installed)
            main.post {
                when (res) {
                    is Res.Ok -> { latestVersion = res.value.first; latestNewer = res.value.second
                        say(if (res.value.second) "${res.value.first} tersedia · robot memverifikasi ulang saat update" else "Sudah versi terbaru (${res.value.first})") }
                    is Res.Err -> say(res.message)
                }
            }
        }.start()
    }

    /** HTTPS only, small, with timeouts; the answer is informational and never trusted by the robot. */
    private fun fetchManifest(): String? = try {
        val conn = java.net.URL(UpdateManifest.URL).openConnection() as java.net.HttpURLConnection
        conn.connectTimeout = 8000; conn.readTimeout = 8000; conn.instanceFollowRedirects = true
        if (conn.responseCode != 200 || conn.url.protocol != "https") null
        else conn.inputStream.use { input ->
            val buf = ByteArray(UpdateManifest.MAX_BYTES)
            var n = 0
            while (n < buf.size) { val r = input.read(buf, n, buf.size - n); if (r < 0) break; n += r }
            String(buf, 0, n, Charsets.UTF_8)
        }
    } catch (_: Exception) { null }
}
