package com.robodesk.phonebridge

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.bluetooth.*
import android.content.ComponentName
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.UUID

data class PhoneBridgeUiState(
    val phase:BridgeConnectionPhase=BridgeConnectionPhase.IDLE,
    val selectedRobot:Boolean=false,
    val connected:Boolean=false,
    val authenticated:Boolean=false,
    val notificationPermission:Boolean=false,
    val nearbyPermission:Boolean=false,
    val sharingPaused:Boolean=false,
    val navigationEnabled:Boolean=false,
    val queuedNotifications:Int=0,
    val queueOldestAgeMs:Long=0L,
    val droppedCount:Long=0L,
    val coalescedCount:Long=0L,
    val mtu:Int=23,
    val rssiDbm:Int?=null,
    val reconnectCount:Long=0L,
    val lastAcknowledgedAt:Long=0L,
    val robotLinkStatus:String="not reported",
    val companionCommandStatus:String="No companion command sent"
)

object PhoneBridgeStatus {
    @Volatile var text = "Waiting for notification access and Bluetooth."
    @Volatile var state = PhoneBridgeUiState()
    @Volatile var forgetLocalState: (() -> Unit)? = null
    @Volatile var authenticatedForget: (() -> Boolean)? = null
    @Volatile var pauseSharing: ((Boolean) -> Unit)? = null
    @Volatile var appModeChanged: ((String,AppContentMode) -> Unit)? = null
    @Volatile var retryConnection: (() -> Unit)? = null
    @Volatile var companionCommand: ((ByteArray) -> Unit)? = null
    @Volatile var localForgetVerified = true
    @Volatile var reconnectAttempt = 0
    @Volatile var handshakeFailures = 0
    /** Repeated failures to reach a robot this phone is still bonded with (see RobotLinkCore.noteLinkFailure). */
    @Volatile var staleBond = false
    /** Onboarding pairing progress: 1 waiting for the robot's window, 2 entering the code, 3 securing the link, 0 not pairing. */
    @Volatile var pairStep = 0
    /** elapsedRealtime when the link core last saw the robot advertising its pairing window while this app was not pairing (0 = never). */
    @Volatile var beaconOpenAt = 0L
    /** Indonesian onboarding lines: last specific failure (survives the disconnect that follows) and the current stage. */
    @Volatile var failure: String? = null
    @Volatile var stage: String? = null
    fun republishLink() { RobotLink.state.set(LinkPhaseMapper.map(state.phase, reconnectAttempt, text, handshakeFailures, failure ?: stage, staleBond, pairStep)) }
    fun clearAfterForget(){forgetLocalState?.invoke()}
    fun requestAuthenticatedForget()=authenticatedForget?.invoke()==true
    fun setPhase(phase:BridgeConnectionPhase,connected:Boolean=state.connected,authenticated:Boolean=state.authenticated,navigationEnabled:Boolean=state.navigationEnabled){state=state.copy(phase=phase,connected=connected,authenticated=authenticated,navigationEnabled=navigationEnabled);republishLink()}
}

@SuppressLint("MissingPermission")
class RoboNotificationListener : NotificationListenerService(), RobotLinkHost {
    companion object {
        private const val MAPS="com.google.android.apps.maps"
    }
    private val handler=Handler(Looper.getMainLooper())
    private var listening=false
    private var navKey:String?=null
    private var navigation:NavigationInstruction?=null
    private val privacyBlockedApps=LinkedHashSet<String>()
    private val privacyRemovalPending=LinkedHashMap<String,MutableSet<String>>()
    private val privacyReapply=LinkedHashMap<String,MutableList<StatusBarNotification>>()
    private val privacyFailedRemovals=HashSet<String>()
    private var privacyReapplyCount=0
    private var privacyFailClosed=false
    private val core:RobotLinkCore? get()=RobotLinkHub.core
    private fun sharingPaused():Boolean=getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("paused",false)
    private fun clearPrivacyPurgeMarker(){getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("privacyPurgePending",false).commit()}
    private fun ready()=core?.ready==true

    // ---- RobotLinkHost (called by the link service on the main looper)
    override fun onReady():Boolean{
        if(!listening)return false
        privacyFailedRemovals.clear();schedulePrivacyRemovals()
        val c=core?:return false
        if(sharingPaused()||privacyFailClosed){c.clearPending();c.navLatest?.bytes?.fill(0);c.navLatest=null;c.pending.addFirst(RobotPacket(5,"","",byteArrayOf()));c.updateQueueState()}
        else try{activeNotifications?.filter{System.currentTimeMillis()-it.postTime<300000}?.forEach{onPosted(it)}}catch(_:Exception){}
        return true
    }
    override fun onHeartbeatReady(){
        if(!listening)return
        if((getSystemService(android.app.KeyguardManager::class.java)?.isDeviceLocked==true)&&navigation!=null)endNavigation()
        if(!getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("navigation",false)&&navigation!=null)endNavigation()
        val key=navKey
        if(key!=null) { val active=try { activeNotifications?.firstOrNull { it.key==key } } catch(_:Exception){ null };if(active!=null)onPosted(active) else endNavigation() }
        if(!sharingPaused())try { activeNotifications?.filter { it.packageName!=packageName }?.forEach { onPosted(it) } } catch(_:Exception) { }
    }
    override fun onPacketAcked(packet:RobotPacket,status:Int){
        if(packet.kind==5){
            if(status==0){clearPrivacyPurgeMarker();if(privacyFailClosed){privacyFailClosed=false;PhoneBridgeStatus.text="Robot transient content cleared; app notifications can resume under their selected privacy modes.";if(!sharingPaused())try{activeNotifications?.filter{it.packageName!=packageName}?.forEach{onPosted(it)}}catch(_:Exception){}}else PhoneBridgeStatus.text="Sharing paused; transient robot notifications and navigation were cleared."}else PhoneBridgeStatus.text="Sharing is paused on this phone, but the robot could not confirm clearing transient data."
        }else if(packet.kind==2){
            val marker="${packet.pkg}\u0000${packet.key}";if(status==0)privacyRemovalPending[packet.pkg]?.remove(packet.key)else{privacyFailedRemovals.add(marker);PhoneBridgeStatus.text="Robot did not confirm purging old app notification content; that app remains paused until retry."};schedulePrivacyRemovals();finishPrivacyRefresh(packet.pkg)
        }
    }
    override fun onNavigationCleared(){navigation=null;navKey=null}
    override fun onForgetLocal(){clearPrivacyRefresh();navigation=null;navKey=null}

    override fun onListenerConnected(){
        super.onListenerConnected()
        privacyFailClosed=getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("privacyPurgePending",false)
        RobotLinkHub.host=this
        PhoneBridgeStatus.pauseSharing={paused->handler.post{
            val c=core
            getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("paused",paused).commit()
            if(paused){c?.clearPending();clearPrivacyRefresh();c?.navLatest?.bytes?.fill(0);c?.navLatest=null;navigation=null;navKey=null;if(c?.inFlightPacket()?.kind==1)c.inFlightPacket()?.bytes?.fill(0);c?.pending?.addFirst(RobotPacket(5,"","",byteArrayOf()));if(c?.ready==true)PhoneBridgeStatus.setPhase(BridgeConnectionPhase.READY)}
            else try{activeNotifications?.filter{it.packageName!=packageName}?.forEach{onPosted(it)}}catch(_:Exception){}
            c?.updateQueueState();c?.sendNext()
        }}
        PhoneBridgeStatus.appModeChanged={app,mode->handler.post{applyAppModeChange(app,mode)}}
        RobotLinkService.start(this)
        handler.post { listening=true;PhoneBridgeStatus.text="Notification access granted; connecting to RoboDesk.";core?.ensureConnected();if(ready())onReady() }
    }
    override fun onListenerDisconnected(){handler.post { listening=false;core?.clearPending();clearPrivacyRefresh();endNavigation();RobotLinkHub.host=null;PhoneBridgeStatus.text="Notification access disconnected." };super.onListenerDisconnected();requestRebind(ComponentName(this,RoboNotificationListener::class.java))}
    override fun onDestroy(){listening=false;handler.removeCallbacksAndMessages(null);if(RobotLinkHub.host===this)RobotLinkHub.host=null;PhoneBridgeStatus.pauseSharing=null;PhoneBridgeStatus.appModeChanged=null;clearPrivacyRefresh();super.onDestroy()}
    override fun onNotificationPosted(sbn:StatusBarNotification){handler.post { onPosted(sbn) }}
    override fun onNotificationRemoved(sbn:StatusBarNotification){handler.post {
        val c=core?:return@post
        if(c.forgetInProgress)return@post
        if(sharingPaused())return@post
        if(sbn.key==navKey)endNavigation()
        val key=c.keyFor(sbn.key)
        c.pending.filter { it.key==key }.forEach{it.bytes.fill(0)}
        c.pending.removeAll { it.key==key }
        if(c.allowed.contains(sbn.packageName))sendRemove(sbn)
        c.sendNext()
    } }
    private fun onPosted(sbn:StatusBarNotification){
        val c=core?:return
        if(sharingPaused())return
        if(c.connectedAddress!=TrustedRobotStore.address(this))return
        if(sbn.packageName==packageName)return
        val isMaps=sbn.packageName==MAPS
        if(!isMaps&&(sbn.isOngoing||!c.allowed.contains(sbn.packageName)))return
        val mode=NotificationPrivacy.effectiveMode(this,sbn.packageName)
        if(!isMaps&&privacyFailClosed)return
        if(!isMaps&&privacyBlockedApps.contains(sbn.packageName)){
            rememberPrivacyRefresh(sbn,mode)
            return
        }
        if(!isMaps&&mode==AppContentMode.OFF){sendRemove(sbn);return}
        val extras=sbn.notification.extras ?: return
        val title=extras.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
        val body=(extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?:extras.getCharSequence(Notification.EXTRA_TEXT))?.toString().orEmpty()
        if(isMaps&&(sbn.isOngoing||sbn.key==navKey)&&c.robotNavigation&&getSystemService(android.app.KeyguardManager::class.java)?.isDeviceLocked!=true&&getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("navigation",true)){
            val instruction=MapsNavigationParser.parse(title,body,extras.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString().orEmpty(),sbn.notification.category=="navigation")
            if(instruction!=null){navKey=if(instruction.state=="arrived")null else sbn.key;navigation=if(instruction.state=="arrived")null else instruction;c.navLatest=RobotPacket(3,MAPS,sbn.key,instruction.wire());c.sendNext();return}
        }
        if(isMaps||mode==AppContentMode.OFF)return
        val label=try{packageManager.getApplicationLabel(packageManager.getApplicationInfo(sbn.packageName,0)).toString()}catch(_:Exception){sbn.packageName}
        val safe=NotificationPrivacy.sanitize(title,body,mode)
        val payload="${c.keyFor(sbn.key)}\n${utf8Field(sbn.packageName,63)}\n${utf8Field(label,39)}\n${safe.title}\n${safe.snippet}"
        c.enqueue(RobotPacket(1,sbn.packageName,c.keyFor(sbn.key),payload.toByteArray()))
        c.ensureConnected();c.sendNext()
    }
    private fun sendRemove(sbn:StatusBarNotification){
        val c=core?:return
        val key=c.keyFor(sbn.key)
        c.enqueue(RobotPacket(2,sbn.packageName,key,"${sbn.packageName}\n$key".toByteArray()))
    }
    private fun clearPrivacyRefresh(){privacyBlockedApps.clear();privacyRemovalPending.clear();privacyReapply.clear();privacyFailedRemovals.clear();privacyReapplyCount=0;privacyFailClosed=false}
    private fun applyAppModeChange(app:String,mode:AppContentMode){
        val c=core
        if(!getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("privacyPurgePending",true).commit()){
            privacyFailClosed=true;PhoneBridgeStatus.text="Privacy change could not be durably recorded; notification forwarding remains blocked until the robot is cleared.";return
        }
        val active=try{activeNotifications?.filter{it.packageName==app&&!it.isOngoing}.orEmpty()}catch(_:Exception){emptyList()}
        val revokeKeys=LinkedHashSet<String>()
        c?.pending?.filter{it.kind==1&&it.pkg==app}?.forEach{revokeKeys.add(it.key);it.bytes.fill(0)}
        c?.pending?.removeAll{it.kind==1&&it.pkg==app}
        c?.pending?.filter{it.kind==2&&it.pkg==app}?.forEach{revokeKeys.add(it.key)}
        val current=c?.inFlightPacket()?.takeIf{it.kind==1&&it.pkg==app}
        if(current!=null){revokeKeys.add(current.key);current.bytes.fill(0)}
        if(c!=null)active.forEach{revokeKeys.add(c.keyFor(it.key))}
        privacyReapply.remove(app)?.let{privacyReapplyCount-=it.size}
        revokeKeys.addAll(privacyRemovalPending.remove(app).orEmpty())
        privacyBlockedApps.add(app)
        if(privacyBlockedApps.size>50){clearPrivacyRefresh();privacyFailClosed=true;c?.clearPending();c?.pending?.addFirst(RobotPacket(5,"","",byteArrayOf()));PhoneBridgeStatus.text="Privacy metadata limit reached; forwarding is paused while the robot clears transient content.";c?.updateQueueState();c?.sendNext();return}
        privacyRemovalPending[app]=revokeKeys
        if(mode!=AppContentMode.OFF){val fresh=active.take(6).toMutableList();privacyReapply[app]=fresh;privacyReapplyCount+=fresh.size}
        privacyFailedRemovals.removeAll{it.startsWith("$app\u0000")}
        schedulePrivacyRemovals()
        if(revokeKeys.isEmpty())finishPrivacyRefresh(app)
        PhoneBridgeStatus.text=if(mode==AppContentMode.OFF)"App sharing disabled; purging active and queued robot notifications." else "App privacy changed; old robot content is being removed before the new mode is applied."
        c?.updateQueueState();c?.sendNext()
    }
    private fun rememberPrivacyRefresh(sbn:StatusBarNotification,mode:AppContentMode){
        if(mode==AppContentMode.OFF)return
        val list=privacyReapply.getOrPut(sbn.packageName){mutableListOf()}
        val index=list.indexOfFirst{it.key==sbn.key}
        if(index>=0)list[index]=sbn else if(privacyReapplyCount<6){list.add(sbn);privacyReapplyCount++}
    }
    private fun schedulePrivacyRemovals(){
        val c=core?:return
        if(!c.ready)return
        for((app,keys) in privacyRemovalPending){
            for(key in keys){
                if(c.pending.size>=12)return
                if(privacyFailedRemovals.contains("$app\u0000$key"))continue
                if(c.inFlightPacket()?.let{it.kind==2&&it.pkg==app&&it.key==key}==true||c.pending.any{it.kind==2&&it.pkg==app&&it.key==key})continue
                c.enqueue(RobotPacket(2,app,key,"$app\n$key".toByteArray()))
            }
        }
    }
    private fun finishPrivacyRefresh(app:String){
        val c=core
        val keys=privacyRemovalPending[app]?:return
        if(keys.isNotEmpty()||c?.pending?.any{it.kind==2&&it.pkg==app}==true||c?.inFlightPacket()?.let{it.kind==2&&it.pkg==app}==true)return
        privacyRemovalPending.remove(app);privacyBlockedApps.remove(app);val fresh=privacyReapply.remove(app).orEmpty();privacyReapplyCount=(privacyReapplyCount-fresh.size).coerceAtLeast(0)
        if(!sharingPaused())fresh.forEach{onPosted(it)}
        if(privacyRemovalPending.isEmpty()&&privacyBlockedApps.isEmpty()&&!privacyFailClosed)clearPrivacyPurgeMarker()
    }
    private fun endNavigation(){val c=core;navigation=null;navKey=null;c?.navLatest?.bytes?.fill(0);c?.navLatest=if(c?.robotNavigation==true)RobotPacket(3,MAPS,"",NavigationInstruction("ended","unknown",0,"","").wire())else null;c?.sendNext()}
}
