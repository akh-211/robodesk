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

enum class BridgeConnectionPhase { IDLE, SELECTING, PAIRING, CONNECTING, AUTHENTICATING, POLICY, READY, OFFLINE, NEEDS_PERMISSION, INCOMPATIBLE, SECURITY_ERROR, REVOKING, REVOKED }
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
    fun clearAfterForget(){forgetLocalState?.invoke()}
    fun requestAuthenticatedForget()=authenticatedForget?.invoke()==true
    fun setPhase(phase:BridgeConnectionPhase,connected:Boolean=state.connected,authenticated:Boolean=state.authenticated,navigationEnabled:Boolean=state.navigationEnabled){state=state.copy(phase=phase,connected=connected,authenticated=authenticated,navigationEnabled=navigationEnabled)}
}

@SuppressLint("MissingPermission")
class RoboNotificationListener : NotificationListenerService() {
    companion object {
        private val SERVICE=UUID.fromString("93de0001-2c7d-4a52-9f1c-6f4b4f424c45")
        private val RX=UUID.fromString("93de0002-2c7d-4a52-9f1c-6f4b4f424c45")
        private val ALLOWLIST=UUID.fromString("93de0003-2c7d-4a52-9f1c-6f4b4f424c45")
        private val NAV=UUID.fromString("93de0004-2c7d-4a52-9f1c-6f4b4f424c45")
        private val CONFIG=UUID.fromString("93de0005-2c7d-4a52-9f1c-6f4b4f424c45")
        private val RX2=UUID.fromString("93de0006-2c7d-4a52-9f1c-6f4b4f424c45")
        private val IDENTITY=UUID.fromString("93de0007-2c7d-4a52-9f1c-6f4b4f424c45")
        private val CONTROL=UUID.fromString("93de0008-2c7d-4a52-9f1c-6f4b4f424c45")
        private val ENVELOPE=UUID.fromString("93de0009-2c7d-4a52-9f1c-6f4b4f424c45")
        private val ACK=UUID.fromString("93de000a-2c7d-4a52-9f1c-6f4b4f424c45")
        private val CCCD=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val MAPS="com.google.android.apps.maps"
    }
    private data class Packet(val kind:Int,val pkg:String,val key:String,val bytes:ByteArray,val at:Long=SystemClock.elapsedRealtime())
    private val handler=Handler(Looper.getMainLooper())
    private var gatt:BluetoothGatt?=null
    private var connectedAddress:String?=null
    private var listening=false
    private var ready=false
    private var busy=false
    private var awaitingAck=false
    private var awaitingControl=false
    private var operationAt=0L
    private var reconnectAttempt=0
    private var reconnectCount=0L
    private var droppedCount=0L
    private var coalescedCount=0L
    private var rssiDbm:Int?=null
    private var forgetInProgress=false
    private var mtu=23
    private var robotNavigation=false
    private var navKey:String?=null
    private var navigation:NavigationInstruction?=null
    private var navLatest:Packet?=null
    private var inFlight:Packet?=null
    private var inFlightCounter=0L
    private var pendingCommandResultCounter=0L
    private var pendingCommandResultAt=0L
    private var writeFrames:List<ByteArray> = emptyList()
    private var writeFrameIndex=0
    private var writeTarget:UUID?=null
    private var notificationSetupIndex=0
    private val controlReassembler=BridgeGattFraming.Reassembler()
    private val ackReassembler=BridgeGattFraming.Reassembler()
    private var phoneNonce:ByteArray?=null
    private var pendingChallenge:BridgeSessionHandshake.Challenge?=null
    private var credentialStore:BridgeCredentialStore?=null
    private var robotId:ByteArray?=null
    private var storedCredential:BridgeCredentialStore.ReadResult.Available?=null
    private var sessionSender:BridgeAuthenticatedEnvelope.SessionSender?=null
    private var ackReceiver:BridgeAuthenticatedEnvelope.SessionReceiver?=null
    private var allowed:Set<String> = emptySet()
    private val pending=ArrayDeque<Packet>()
    private val privacyBlockedApps=LinkedHashSet<String>()
    private val privacyRemovalPending=LinkedHashMap<String,MutableSet<String>>()
    private val privacyReapply=LinkedHashMap<String,MutableList<StatusBarNotification>>()
    private val privacyFailedRemovals=HashSet<String>()
    private var privacyReapplyCount=0
    private var privacyFailClosed=false
    private fun sharingPaused():Boolean=getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("paused",false)
    private fun clearPrivacyPurgeMarker(){getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("privacyPurgePending",false).commit()}
    private fun updateQueueState(){val oldest=pending.firstOrNull{it.kind==1};PhoneBridgeStatus.state=PhoneBridgeStatus.state.copy(selectedRobot=TrustedRobotStore.address(this)!=null,connected=gatt!=null&&ready,authenticated=sessionSender!=null,notificationPermission=android.provider.Settings.Secure.getString(contentResolver,"enabled_notification_listeners")?.contains(packageName)==true,nearbyPermission=Build.VERSION.SDK_INT<31||checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED,sharingPaused=sharingPaused(),navigationEnabled=robotNavigation,queuedNotifications=pending.count{it.kind==1},queueOldestAgeMs=oldest?.let{(SystemClock.elapsedRealtime()-it.at).coerceAtLeast(0)}?:0L,droppedCount=droppedCount,coalescedCount=coalescedCount,mtu=mtu,rssiDbm=rssiDbm,reconnectCount=reconnectCount)}
    private val heartbeat=object:Runnable { override fun run() {
        if(!listening)return
        val expired=pending.filter{it.kind!=5&&SystemClock.elapsedRealtime()-it.at>if(it.kind==6)30000 else 300000};expired.forEach{it.bytes.fill(0);droppedCount++};pending.removeAll{it.kind!=5&&SystemClock.elapsedRealtime()-it.at>if(it.kind==6)30000 else 300000};updateQueueState()
        if(pendingCommandResultCounter!=0L&&SystemClock.elapsedRealtime()-pendingCommandResultAt>20000){pendingCommandResultCounter=0;PhoneBridgeStatus.state=PhoneBridgeStatus.state.copy(companionCommandStatus="Robot command result timed out; check robot status before retrying.")}
        if((busy||awaitingAck||awaitingControl)&&SystemClock.elapsedRealtime()-operationAt>20000){disconnect()}
        ensureConnected()
        if(ready&&!busy) {
            if((getSystemService(android.app.KeyguardManager::class.java)?.isDeviceLocked==true)&&navigation!=null)endNavigation()
            if(!getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("navigation",false)&&navigation!=null)endNavigation()
            val key=navKey
            if(key!=null) { val active=try { activeNotifications?.firstOrNull { it.key==key } } catch(_:Exception){ null };if(active!=null)onPosted(active) else endNavigation() }
            if(!sharingPaused())try { activeNotifications?.filter { it.packageName!=packageName }?.forEach { onPosted(it) } } catch(_:Exception) { }
            if(!busy&&!awaitingAck&&!awaitingControl){val currentGatt=gatt;busy=currentGatt?.readRemoteRssi()==true;if(busy)operationAt=SystemClock.elapsedRealtime()}
            sendNext()
        }
        handler.postDelayed(this,15000)
    } }
    override fun onListenerConnected(){
        super.onListenerConnected()
        privacyFailClosed=getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("privacyPurgePending",false)
        PhoneBridgeStatus.forgetLocalState={handler.post{clearPending();clearPrivacyRefresh();navLatest?.bytes?.fill(0);navLatest=null;navigation=null;navKey=null;allowed=emptySet();disconnect()}}
        PhoneBridgeStatus.authenticatedForget=fun():Boolean{
            if(!ready||sessionSender==null||busy||awaitingAck||awaitingControl)return false
            forgetInProgress=true;clearPending();navLatest?.bytes?.fill(0);navLatest=null;navigation=null;navKey=null
            pending.addFirst(Packet(4,"","",byteArrayOf()));PhoneBridgeStatus.setPhase(BridgeConnectionPhase.REVOKING);sendNext();return true
        }
        PhoneBridgeStatus.pauseSharing={paused->handler.post{
            getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("paused",paused).commit()
            if(paused){clearPending();clearPrivacyRefresh();navLatest?.bytes?.fill(0);navLatest=null;navigation=null;navKey=null;if(inFlight?.kind==1)inFlight?.bytes?.fill(0);pending.addFirst(Packet(5,"","",byteArrayOf()));PhoneBridgeStatus.setPhase(BridgeConnectionPhase.READY)}
            else try{activeNotifications?.filter{it.packageName!=packageName}?.forEach{onPosted(it)}}catch(_:Exception){}
            updateQueueState();sendNext()
        }}
        PhoneBridgeStatus.appModeChanged={app,mode->handler.post{applyAppModeChange(app,mode)}}
        PhoneBridgeStatus.retryConnection={handler.post{if(!forgetInProgress){privacyFailedRemovals.clear();disconnect();ensureConnected()}}}
        PhoneBridgeStatus.companionCommand={payload->handler.post{queueCompanionCommand(payload)}}
        handler.post { listening=true;PhoneBridgeStatus.text="Notification access granted; connecting to RoboDesk.";ensureConnected();handler.removeCallbacks(heartbeat);handler.post(heartbeat) }
    }
    override fun onListenerDisconnected(){handler.post { listening=false;handler.removeCallbacks(heartbeat);disconnect();clearPending();endNavigation();PhoneBridgeStatus.text="Notification access disconnected." };super.onListenerDisconnected();requestRebind(ComponentName(this,RoboNotificationListener::class.java))}
    override fun onDestroy(){listening=false;handler.removeCallbacksAndMessages(null);PhoneBridgeStatus.forgetLocalState=null;PhoneBridgeStatus.authenticatedForget=null;PhoneBridgeStatus.pauseSharing=null;PhoneBridgeStatus.appModeChanged=null;PhoneBridgeStatus.retryConnection=null;PhoneBridgeStatus.companionCommand=null;clearPrivacyRefresh();disconnect();super.onDestroy()}
    override fun onNotificationPosted(sbn:StatusBarNotification){handler.post { onPosted(sbn) }}
    override fun onNotificationRemoved(sbn:StatusBarNotification){handler.post {
        if(forgetInProgress)return@post
        if(sharingPaused())return@post
        if(sbn.key==navKey)endNavigation()
        pending.filter { it.key==keyFor(sbn.key) }.forEach{it.bytes.fill(0)}
        pending.removeAll { it.key==keyFor(sbn.key) }
        if(allowed.contains(sbn.packageName))sendRemove(sbn)
        sendNext()
    } }
    private fun keyFor(key:String):String=MessageDigest.getInstance("SHA-256").digest(key.toByteArray()).take(12).joinToString(""){"%02x".format(it.toInt() and 255)}
    private fun onPosted(sbn:StatusBarNotification){
        if(sharingPaused())return
        if(connectedAddress!=TrustedRobotStore.address(this))return
        if(sbn.packageName==packageName)return
        val isMaps=sbn.packageName==MAPS
        if(!isMaps&&(sbn.isOngoing||!allowed.contains(sbn.packageName)))return
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
        if(isMaps&&(sbn.isOngoing||sbn.key==navKey)&&robotNavigation&&getSystemService(android.app.KeyguardManager::class.java)?.isDeviceLocked!=true&&getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("navigation",true)){
            val instruction=MapsNavigationParser.parse(title,body,extras.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString().orEmpty(),sbn.notification.category=="navigation")
            if(instruction!=null){navKey=if(instruction.state=="arrived")null else sbn.key;navigation=if(instruction.state=="arrived")null else instruction;navLatest=Packet(3,MAPS,sbn.key,instruction.wire());sendNext();return}
        }
        if(isMaps||mode==AppContentMode.OFF)return
        val label=try{packageManager.getApplicationLabel(packageManager.getApplicationInfo(sbn.packageName,0)).toString()}catch(_:Exception){sbn.packageName}
        val safe=NotificationPrivacy.sanitize(title,body,mode)
        val payload="${keyFor(sbn.key)}\n${utf8Field(sbn.packageName,63)}\n${utf8Field(label,39)}\n${safe.title}\n${safe.snippet}"
        enqueue(Packet(1,sbn.packageName,keyFor(sbn.key),payload.toByteArray()))
        ensureConnected();sendNext()
    }
    private fun sendRemove(sbn:StatusBarNotification){
        val key=keyFor(sbn.key)
        enqueue(Packet(2,sbn.packageName,key,"${sbn.packageName}\n$key".toByteArray()))
    }
    private fun queueCompanionCommand(payload:ByteArray){
        if(payload.isEmpty()||payload.size>8||!ready||sessionSender==null||forgetInProgress||inFlight?.kind==6||pending.any{it.kind==6}){PhoneBridgeStatus.state=PhoneBridgeStatus.state.copy(companionCommandStatus="Robot command unavailable or another command is still pending.");return}
        enqueue(Packet(6,"","companion:${SystemClock.elapsedRealtimeNanos()}",payload.copyOf()))
        PhoneBridgeStatus.state=PhoneBridgeStatus.state.copy(companionCommandStatus="Command queued for the selected robot.")
        sendNext()
    }
    private fun clearPending(){pending.forEach{it.bytes.fill(0)};pending.clear()}
    private fun clearPrivacyRefresh(){privacyBlockedApps.clear();privacyRemovalPending.clear();privacyReapply.clear();privacyFailedRemovals.clear();privacyReapplyCount=0;privacyFailClosed=false}
    private fun applyAppModeChange(app:String,mode:AppContentMode){
        if(!getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("privacyPurgePending",true).commit()){
            privacyFailClosed=true;PhoneBridgeStatus.text="Privacy change could not be durably recorded; notification forwarding remains blocked until the robot is cleared.";return
        }
        val active=try{activeNotifications?.filter{it.packageName==app&&!it.isOngoing}.orEmpty()}catch(_:Exception){emptyList()}
        val revokeKeys=LinkedHashSet<String>()
        pending.filter{it.kind==1&&it.pkg==app}.forEach{revokeKeys.add(it.key);it.bytes.fill(0)}
        pending.removeAll{it.kind==1&&it.pkg==app}
        pending.filter{it.kind==2&&it.pkg==app}.forEach{revokeKeys.add(it.key)}
        val current=inFlight?.takeIf{it.kind==1&&it.pkg==app}
        if(current!=null){revokeKeys.add(current.key);current.bytes.fill(0)}
        active.forEach{revokeKeys.add(keyFor(it.key))}
        privacyReapply.remove(app)?.let{privacyReapplyCount-=it.size}
        revokeKeys.addAll(privacyRemovalPending.remove(app).orEmpty())
        privacyBlockedApps.add(app)
        if(privacyBlockedApps.size>50){clearPrivacyRefresh();privacyFailClosed=true;clearPending();pending.addFirst(Packet(5,"","",byteArrayOf()));PhoneBridgeStatus.text="Privacy metadata limit reached; forwarding is paused while the robot clears transient content.";updateQueueState();sendNext();return}
        privacyRemovalPending[app]=revokeKeys
        if(mode!=AppContentMode.OFF){val fresh=active.take(6).toMutableList();privacyReapply[app]=fresh;privacyReapplyCount+=fresh.size}
        privacyFailedRemovals.removeAll{it.startsWith("$app\u0000")}
        schedulePrivacyRemovals()
        if(revokeKeys.isEmpty())finishPrivacyRefresh(app)
        PhoneBridgeStatus.text=if(mode==AppContentMode.OFF)"App sharing disabled; purging active and queued robot notifications." else "App privacy changed; old robot content is being removed before the new mode is applied."
        updateQueueState();sendNext()
    }
    private fun rememberPrivacyRefresh(sbn:StatusBarNotification,mode:AppContentMode){
        if(mode==AppContentMode.OFF)return
        val list=privacyReapply.getOrPut(sbn.packageName){mutableListOf()}
        val index=list.indexOfFirst{it.key==sbn.key}
        if(index>=0)list[index]=sbn else if(privacyReapplyCount<6){list.add(sbn);privacyReapplyCount++}
    }
    private fun schedulePrivacyRemovals(){
        if(!ready)return
        for((app,keys) in privacyRemovalPending){
            for(key in keys){
                if(pending.size>=12)return
                if(privacyFailedRemovals.contains("$app\u0000$key"))continue
                if(inFlight?.let{it.kind==2&&it.pkg==app&&it.key==key}==true||pending.any{it.kind==2&&it.pkg==app&&it.key==key})continue
                enqueue(Packet(2,app,key,"$app\n$key".toByteArray()))
            }
        }
    }
    private fun finishPrivacyRefresh(app:String){
        val keys=privacyRemovalPending[app]?:return
        if(keys.isNotEmpty()||pending.any{it.kind==2&&it.pkg==app}||inFlight?.let{it.kind==2&&it.pkg==app}==true)return
        privacyRemovalPending.remove(app);privacyBlockedApps.remove(app);val fresh=privacyReapply.remove(app).orEmpty();privacyReapplyCount=(privacyReapplyCount-fresh.size).coerceAtLeast(0)
        if(!sharingPaused())fresh.forEach{onPosted(it)}
        if(privacyRemovalPending.isEmpty()&&privacyBlockedApps.isEmpty()&&!privacyFailClosed)clearPrivacyPurgeMarker()
    }
    private fun enqueue(packet:Packet){if(forgetInProgress){packet.bytes.fill(0);return};val replaced=pending.filter{it.key==packet.key&&it!==inFlight};replaced.forEach{it.bytes.fill(0)};if(replaced.isNotEmpty())coalescedCount+=replaced.size;pending.removeAll{it.key==packet.key&&it!==inFlight};if(pending.size>=12){val old=pending.firstOrNull{it!==inFlight};if(old!=null){old.bytes.fill(0);pending.remove(old);droppedCount++}};pending.addLast(packet);updateQueueState()}
    private fun endNavigation(){navigation=null;navKey=null;navLatest?.bytes?.fill(0);navLatest=if(robotNavigation)Packet(3,MAPS,"",NavigationInstruction("ended","unknown",0,"","").wire())else null;sendNext()}
    private fun permissions():Boolean=Build.VERSION.SDK_INT<31||checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED
    private fun ensureConnected(){
        if(!listening)return
        val target=TrustedRobotStore.address(this)
        if(target==null){if(gatt!=null&&!forgetInProgress)disconnect();if(!forgetInProgress){PhoneBridgeStatus.text="Select one RoboDesk in the app before connecting.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.IDLE,connected=false,authenticated=false)};return}
        if(gatt!=null&&connectedAddress!=target){disconnect();clearPending();navLatest?.bytes?.fill(0);navLatest=null;navigation=null;navKey=null}
        if(gatt!=null)return
        if(!permissions()){PhoneBridgeStatus.text="Grant Nearby devices permission to connect to the selected robot.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.NEEDS_PERMISSION,connected=false,authenticated=false);return}
        val adapter=getSystemService(BluetoothManager::class.java)?.adapter ?: return
        if(!adapter.isEnabled){PhoneBridgeStatus.text="Bluetooth is off.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.OFFLINE,connected=false,authenticated=false);return}
        try {
            connectedAddress=target
            PhoneBridgeStatus.text="Connecting to selected RoboDesk; approve this phone in the robot dashboard.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.CONNECTING,connected=false,authenticated=false)
            gatt=adapter.getRemoteDevice(target).connectGatt(this,false,callback,BluetoothDevice.TRANSPORT_LE)
        } catch (_: Exception) {
            connectedAddress=null
            PhoneBridgeStatus.text="Could not reach the selected robot. Check Bluetooth and pairing, then retry."
            scheduleReconnect()
        }
    }
    private val callback=object:BluetoothGattCallback(){
        override fun onConnectionStateChange(g:BluetoothGatt,status:Int,state:Int){handler.post {if(g!==gatt)return@post;if(status!=BluetoothGatt.GATT_SUCCESS||state==BluetoothProfile.STATE_DISCONNECTED){disconnect()}else if(state==BluetoothProfile.STATE_CONNECTED){reconnectAttempt=0;PhoneBridgeStatus.setPhase(BridgeConnectionPhase.AUTHENTICATING,connected=true,authenticated=false);busy=true;operationAt=SystemClock.elapsedRealtime();if(!g.requestMtu(512)){busy=false;if(!g.discoverServices())disconnect()}}}}
        override fun onMtuChanged(g:BluetoothGatt,value:Int,status:Int){handler.post {if(g!==gatt)return@post;mtu=if(status==BluetoothGatt.GATT_SUCCESS)value else 23;updateQueueState();busy=true;operationAt=SystemClock.elapsedRealtime();if(!g.discoverServices())disconnect()}}
        override fun onReadRemoteRssi(g:BluetoothGatt,rssi:Int,status:Int){handler.post{if(g!==gatt)return@post;busy=false;operationAt=0L;if(status==BluetoothGatt.GATT_SUCCESS)rssiDbm=rssi;updateQueueState();sendNext()}}
        override fun onServicesDiscovered(g:BluetoothGatt,status:Int){handler.post {if(g!==gatt)return@post;val service=g.getService(SERVICE);val required=listOf(IDENTITY,CONTROL,ENVELOPE,ACK,ALLOWLIST,CONFIG);if(status!=BluetoothGatt.GATT_SUCCESS||service==null||required.any{service.getCharacteristic(it)==null}){PhoneBridgeStatus.text="This robot firmware lacks authenticated bridge support. Update the robot; legacy sending is disabled.";disconnect()}else{notificationSetupIndex=0;enableNextNotification(g)}}}
        @Deprecated("Legacy Android callback") override fun onCharacteristicRead(g:BluetoothGatt,c:BluetoothGattCharacteristic,status:Int){read(g,c.uuid,c.value?:byteArrayOf(),status)}
        override fun onCharacteristicRead(g:BluetoothGatt,c:BluetoothGattCharacteristic,value:ByteArray,status:Int){read(g,c.uuid,value,status)}
        override fun onDescriptorWrite(g:BluetoothGatt,d:BluetoothGattDescriptor,status:Int){handler.post {if(g!==gatt)return@post;busy=false;if(status!=BluetoothGatt.GATT_SUCCESS){g.disconnect();return@post};notificationSetupIndex++;enableNextNotification(g)}}
        @Deprecated("Legacy Android callback") override fun onCharacteristicChanged(g:BluetoothGatt,c:BluetoothGattCharacteristic){onBridgeNotification(g,c.uuid,c.value?:byteArrayOf())}
        override fun onCharacteristicChanged(g:BluetoothGatt,c:BluetoothGattCharacteristic,value:ByteArray){onBridgeNotification(g,c.uuid,value)}
        override fun onCharacteristicWrite(g:BluetoothGatt,c:BluetoothGattCharacteristic,status:Int){handler.post {if(g!==gatt||c.uuid!=writeTarget)return@post;if(status!=BluetoothGatt.GATT_SUCCESS){PhoneBridgeStatus.text="BLE write failed; reconnecting.";g.disconnect();return@post};writeFrameIndex++;if(writeFrameIndex<writeFrames.size){writeOneFrame(g)}else{busy=false;writeFrames.forEach{it.fill(0)};writeFrames=emptyList();writeFrameIndex=0;writeTarget=null;operationAt=SystemClock.elapsedRealtime();if(c.uuid==ENVELOPE){if(inFlight!=null)awaitingAck=true}else awaitingControl=true;sendNext()}}}
    }
    private fun read(g:BluetoothGatt,uuid:UUID,value:ByteArray,status:Int){handler.post {
        if(g!==gatt)return@post;busy=false
        if(status!=BluetoothGatt.GATT_SUCCESS){g.disconnect();return@post}
        if(uuid==IDENTITY){PhoneBridgeStatus.setPhase(BridgeConnectionPhase.AUTHENTICATING,connected=true,authenticated=false);if(value.size!=26||value[0].toInt()!=2||value[1].toInt()!=1){PhoneBridgeStatus.text="Robot identity response is invalid.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR);g.disconnect();return@post};val id=value.copyOfRange(6,22);val flags=readU32le(value,2);robotId=id;credentialStore=BridgeCredentialStore(this);val saved=credentialStore!!.read();storedCredential=saved as? BridgeCredentialStore.ReadResult.Available;if(saved is BridgeCredentialStore.ReadResult.RePairRequired){if(!credentialStore!!.clear()){PhoneBridgeStatus.text="Stored trust data is invalid and could not be removed.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR);g.disconnect();return@post};storedCredential=null};if(storedCredential?.robotId!=id.toHex()){storedCredential?.key?.fill(0);storedCredential=null};if((flags and 2L)!=0L&&storedCredential==null){PhoneBridgeStatus.text="Robot trust key is missing. Forget and pair this phone again from the robot dashboard.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR);g.disconnect();return@post};if((flags and 2L)==0L&&(flags and 4L)==0L){PhoneBridgeStatus.text="Robot has no active phone credential. Open Pair Phone in its dashboard and reconnect.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.PAIRING);g.disconnect();return@post};val nonce=ByteArray(32);SecureRandom().nextBytes(nonce);phoneNonce=nonce;val hello=BridgeSessionHandshake.hello(nonce)?:run{PhoneBridgeStatus.setPhase(BridgeConnectionPhase.SECURITY_ERROR);g.disconnect();return@post};beginWrite(CONTROL,hello);PhoneBridgeStatus.text=if((flags and 4L)!=0L)"Securing the approved robot session." else "Authenticating the selected robot.";return@post}
        if(uuid==ALLOWLIST){PhoneBridgeStatus.setPhase(BridgeConnectionPhase.POLICY,connected=true,authenticated=true);allowed=value.toString(Charsets.UTF_8).split(Regex("""[\r\n;\s]+""")).filter{it.isNotBlank()}.toSet();val config=g.getService(SERVICE)?.getCharacteristic(CONFIG);if(config!=null){busy=true;operationAt=SystemClock.elapsedRealtime();if(!g.readCharacteristic(config)){busy=false;g.disconnect()};return@post};robotNavigation=false}
        else if(uuid==CONFIG){val fields=value.toString(Charsets.UTF_8).split('\n');if(fields.size!=3||fields[0]!="1"||fields[1]!="android"){PhoneBridgeStatus.text="Select Android and approve this phone in the robot dashboard.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.POLICY,connected=true,authenticated=true);g.disconnect();return@post};robotNavigation=fields[2]=="1"&&g.getService(SERVICE)?.getCharacteristic(NAV)!=null}
        ready=true;privacyFailedRemovals.clear();schedulePrivacyRemovals();PhoneBridgeStatus.text="Connected. Notifications filtered by robot allowlist. Navigation: ${if(robotNavigation)"ready" else "unavailable"}.";PhoneBridgeStatus.setPhase(BridgeConnectionPhase.READY,connected=true,authenticated=true,navigationEnabled=robotNavigation);updateQueueState()
        if(sharingPaused()||privacyFailClosed){clearPending();navLatest?.bytes?.fill(0);navLatest=null;pending.addFirst(Packet(5,"","",byteArrayOf()));updateQueueState()}else try{activeNotifications?.filter{System.currentTimeMillis()-it.postTime<300000}?.forEach{onPosted(it)}}catch(_:Exception){}
        sendNext()
    } }
    private fun sendNext(){
        if(!ready||busy||awaitingAck||awaitingControl)return;val g=gatt?:return
        if(!forgetInProgress&&connectedAddress!=TrustedRobotStore.address(this)){clearPending();navLatest?.bytes?.fill(0);navLatest=null;disconnect();return}
        val now=SystemClock.elapsedRealtime();val rejected=pending.filter{(it.kind!=5&&now-it.at>if(it.kind==6)30000 else 300000)||(it.kind==1&&!allowed.contains(it.pkg))};rejected.forEach{it.bytes.fill(0);droppedCount++};pending.removeAll{(it.kind!=5&&now-it.at>if(it.kind==6)30000 else 300000)||(it.kind==1&&!allowed.contains(it.pkg))};val packet=navLatest?.takeIf{robotNavigation&&now-it.at<120000}?:pending.firstOrNull()?:return
        val age=if(packet.kind==5)0L else now-packet.at;if((packet.kind==3&&age>=120000)||(packet.kind==6&&age>=30000)||(packet.kind!=3&&packet.kind!=5&&packet.kind!=6&&age>=300000)){if(packet.kind==3){packet.bytes.fill(0);navLatest=null}else{packet.bytes.fill(0);pending.remove(packet);droppedCount++};updateQueueState();sendNext();return}
        val envelope=sessionSender?.create(packet.kind,age,packet.bytes)?:run{PhoneBridgeStatus.text="Authenticated bridge session expired.";g.disconnect();return}
        val counter=readU32le(envelope,12);inFlight=packet;inFlightCounter=counter;if(packet.kind==6){pendingCommandResultCounter=counter;pendingCommandResultAt=now};awaitingAck=true
        beginWrite(ENVELOPE,envelope)
    }
    private fun enableNextNotification(g:BluetoothGatt){
        if(g!==gatt)return
        if(notificationSetupIndex>=2){val identity=g.getService(SERVICE)?.getCharacteristic(IDENTITY)?:run{g.disconnect();return};busy=true;operationAt=SystemClock.elapsedRealtime();if(!g.readCharacteristic(identity)){busy=false;g.disconnect()};return}
        val uuid=if(notificationSetupIndex==0)CONTROL else ACK
        val characteristic=g.getService(SERVICE)?.getCharacteristic(uuid)?:run{g.disconnect();return}
        val descriptor=characteristic.getDescriptor(CCCD)?:run{g.disconnect();return}
        if(!g.setCharacteristicNotification(characteristic,true)){g.disconnect();return}
        busy=true;operationAt=SystemClock.elapsedRealtime()
        val started=if(Build.VERSION.SDK_INT>=33)g.writeDescriptor(descriptor,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)==BluetoothStatusCodes.SUCCESS else{descriptor.value=BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE;g.writeDescriptor(descriptor)}
        if(!started){busy=false;g.disconnect()}
    }
    private fun beginWrite(target:UUID,bytes:ByteArray){
        if(busy||bytes.isEmpty())return
        val frames=BridgeGattFraming.fragments(bytes,mtu,SecureRandom().nextInt(0xffff)+1);bytes.fill(0)
        if(frames==null){PhoneBridgeStatus.text="Could not frame authenticated bridge data at this MTU.";gatt?.disconnect();return}
        writeFrames=frames;writeFrameIndex=0;writeTarget=target;busy=true;operationAt=SystemClock.elapsedRealtime();writeOneFrame(gatt?:return)
    }
    private fun writeOneFrame(g:BluetoothGatt){
        val uuid=writeTarget?:return
        val characteristic=g.getService(SERVICE)?.getCharacteristic(uuid)?:run{g.disconnect();return}
        val frame=writeFrames.getOrNull(writeFrameIndex)?:run{g.disconnect();return}
        val started=if(Build.VERSION.SDK_INT>=33)g.writeCharacteristic(characteristic,frame,BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==BluetoothStatusCodes.SUCCESS else{characteristic.writeType=BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;characteristic.value=frame;g.writeCharacteristic(characteristic)}
        if(!started){busy=false;g.disconnect()}
    }
    private fun onBridgeNotification(g:BluetoothGatt,uuid:UUID,value:ByteArray){handler.post {
        if(g!==gatt)return@post
        when(uuid){
            CONTROL->{val complete=controlReassembler.accept(value,SystemClock.elapsedRealtime())?:return@post;handleControlResponse(g,complete)}
            ACK->{val complete=ackReassembler.accept(value,SystemClock.elapsedRealtime())?:return@post;handleApplicationAck(complete)}
        }
    }}
    private fun handleControlResponse(g:BluetoothGatt,frame:ByteArray){
        val challenge=pendingChallenge
        if(challenge==null){
            val id=robotId?:return;val nonce=phoneNonce?:return
            val existing=storedCredential?.key
            val parsed=BridgeSessionHandshake.parseChallenge(frame,id,nonce,existing)
            if(parsed==null){PhoneBridgeStatus.text="Robot authentication failed. Check pairing and dashboard approval.";g.disconnect();return}
            if(parsed.enrollment){
                if(storedCredential!=null&&!storedCredential!!.pending&&!MessageDigest.isEqual(storedCredential!!.key,parsed.key)){parsed.key.fill(0);PhoneBridgeStatus.text="Robot key changed; pair it again from the dashboard.";g.disconnect();return}
                if(credentialStore?.savePending(id.toHex(),parsed.key)!=true){parsed.key.fill(0);PhoneBridgeStatus.text="Could not store the robot credential securely.";g.disconnect();return}
            }
            pendingChallenge=parsed;awaitingControl=false;val finish=BridgeSessionHandshake.finish(parsed);beginWrite(CONTROL,finish);PhoneBridgeStatus.text="Robot identity verified; completing secure pairing."
            return
        }
        if(!BridgeSessionHandshake.verifyAccepted(challenge,frame)){PhoneBridgeStatus.text="Robot session confirmation failed.";g.disconnect();return}
        val id=robotId?:return
        if(credentialStore?.markActive(id.toHex())!=true){PhoneBridgeStatus.text="Credential activation could not be confirmed on this phone.";g.disconnect();return}
        sessionSender=BridgeAuthenticatedEnvelope.SessionSender(challenge.key,challenge.sessionId,BridgeAuthenticatedEnvelope.Direction.PHONE_TO_ROBOT)
        ackReceiver=BridgeAuthenticatedEnvelope.SessionReceiver(challenge.key,challenge.sessionId,BridgeAuthenticatedEnvelope.Direction.ROBOT_TO_PHONE,mapOf(0x7f to 20_000L,0x7e to 20_000L))
        challenge.key.fill(0);pendingChallenge=null;storedCredential?.key?.fill(0);storedCredential=null;phoneNonce?.fill(0);phoneNonce=null;awaitingControl=false
        val allowlist=g.getService(SERVICE)?.getCharacteristic(ALLOWLIST)?:run{g.disconnect();return}
        busy=true;operationAt=SystemClock.elapsedRealtime();if(!g.readCharacteristic(allowlist)){busy=false;g.disconnect()};PhoneBridgeStatus.text="Secure robot session established; loading its sharing policy."
    }
    private fun handleApplicationAck(wire:ByteArray){
        val message=ackReceiver?.accept(wire)?:return
        if(message.kind==0x7e){if(message.payload.size!=6||readU32le(message.payload,0)!=pendingCommandResultCounter||pendingCommandResultCounter==0L)return;val status=message.payload[4].toInt() and 0xff;val reason=message.payload[5].toInt() and 0xff;pendingCommandResultCounter=0;PhoneBridgeStatus.state=PhoneBridgeStatus.state.copy(companionCommandStatus=if(status==0)"Robot confirmed the command was applied." else "Robot rejected the command: ${commandReason(reason)}.");return}
        if(message.kind!=0x7f||message.payload.size!=6||readU32le(message.payload,0)!=inFlightCounter)return
        val packet=inFlight?:return;val status=message.payload[4].toInt() and 0xff
        if(packet.kind==3){if(navLatest===packet)navLatest=null}else pending.remove(packet)
        packet.bytes.fill(0);inFlight=null;inFlightCounter=0;awaitingAck=false;PhoneBridgeStatus.state=PhoneBridgeStatus.state.copy(lastAcknowledgedAt=System.currentTimeMillis())
        if(packet.kind==4){forgetInProgress=false;sessionSender?.close();sessionSender=null;ackReceiver?.close();ackReceiver=null;PhoneBridgeStatus.setPhase(BridgeConnectionPhase.REVOKED,connected=false,authenticated=false);disconnect();PhoneBridgeStatus.setPhase(BridgeConnectionPhase.REVOKED,connected=false,authenticated=false);PhoneBridgeStatus.text=if(status==0&&PhoneBridgeStatus.localForgetVerified)"Robot confirmed peer revocation; local credential has been removed." else if(status==0)"Robot confirmed peer revocation, but local credential cleanup needs review in Android settings." else "Local credential is removed, but the robot did not confirm peer revocation. Revoke it from the robot dashboard.";return}
        if(packet.kind==5){if(status==0){clearPrivacyPurgeMarker();if(privacyFailClosed){privacyFailClosed=false;PhoneBridgeStatus.text="Robot transient content cleared; app notifications can resume under their selected privacy modes.";if(!sharingPaused())try{activeNotifications?.filter{it.packageName!=packageName}?.forEach{onPosted(it)}}catch(_:Exception){}}else PhoneBridgeStatus.text="Sharing paused; transient robot notifications and navigation were cleared."}else PhoneBridgeStatus.text="Sharing is paused on this phone, but the robot could not confirm clearing transient data.";updateQueueState();sendNext();return}
        if(packet.kind==2){val marker="${packet.pkg}\u0000${packet.key}";if(status==0)privacyRemovalPending[packet.pkg]?.remove(packet.key)else{privacyFailedRemovals.add(marker);PhoneBridgeStatus.text="Robot did not confirm purging old app notification content; that app remains paused until retry."};schedulePrivacyRemovals();finishPrivacyRefresh(packet.pkg);sendNext();return}
        if(packet.kind==6){if(status!=0)pendingCommandResultCounter=0;pendingCommandResultAt=SystemClock.elapsedRealtime();PhoneBridgeStatus.state=PhoneBridgeStatus.state.copy(companionCommandStatus=if(status==0)"Gateway accepted the command; waiting for robot confirmation." else "Gateway rejected the command (reason ${message.payload[5].toInt() and 0xff}).");sendNext();return}
        PhoneBridgeStatus.text=if(status==0)"Robot acknowledged the update." else "Robot rejected an update (reason ${message.payload[5].toInt() and 0xff})."
        sendNext()
    }
    private fun commandReason(reason:Int)=when(reason){1->"robot link unavailable";2->"robot busy or safety gate active";3->"command unsupported or invalid";4->"robot could not persist the setting";else->"robot unavailable"}
    private fun readU32le(bytes:ByteArray,offset:Int):Long=(bytes[offset].toLong() and 0xff) or ((bytes[offset+1].toLong() and 0xff) shl 8) or ((bytes[offset+2].toLong() and 0xff) shl 16) or ((bytes[offset+3].toLong() and 0xff) shl 24)
    private fun ByteArray.toHex()=joinToString(""){"%02x".format(it.toInt() and 0xff)}
    private fun disconnect(){val localRevokeIncomplete=forgetInProgress;val commandOutcomeUnknown=pendingCommandResultCounter!=0L||inFlight?.kind==6||pending.any{it.kind==6};if(commandOutcomeUnknown){inFlight?.takeIf{it.kind==6}?.bytes?.fill(0);pending.filter{it.kind==6}.forEach{it.bytes.fill(0)};pending.removeAll{it.kind==6};PhoneBridgeStatus.state=PhoneBridgeStatus.state.copy(companionCommandStatus="Connection lost during command delivery; check robot status before retrying.")};forgetInProgress=false;val old=gatt;gatt=null;connectedAddress=null;old?.close();ready=false;busy=false;awaitingAck=false;awaitingControl=false;pendingCommandResultCounter=0;inFlight?.bytes?.fill(0);inFlight=null;inFlightCounter=0;writeFrames.forEach{it.fill(0)};writeFrames=emptyList();writeFrameIndex=0;writeTarget=null;robotNavigation=false;rssiDbm=null;mtu=23;controlReassembler.reset();ackReassembler.reset();phoneNonce?.fill(0);phoneNonce=null;pendingChallenge?.key?.fill(0);pendingChallenge=null;storedCredential?.key?.fill(0);storedCredential=null;sessionSender?.close();sessionSender=null;ackReceiver?.close();ackReceiver=null;robotId?.fill(0);robotId=null;credentialStore=null;updateQueueState();if(!localRevokeIncomplete)PhoneBridgeStatus.setPhase(if(TrustedRobotStore.address(this)==null)BridgeConnectionPhase.IDLE else BridgeConnectionPhase.OFFLINE,connected=false,authenticated=false,navigationEnabled=false);if(localRevokeIncomplete)PhoneBridgeStatus.text="Local credential is removed, but the robot did not confirm peer revocation. Revoke it from the robot dashboard.";else PhoneBridgeStatus.text="Robot disconnected; retrying.";scheduleReconnect()}
    private fun scheduleReconnect(){if(!listening||TrustedRobotStore.address(this)==null)return;val delays=longArrayOf(2000,5000,10000,30000,60000);val base=delays[reconnectAttempt.coerceAtMost(delays.lastIndex)];reconnectAttempt=(reconnectAttempt+1).coerceAtMost(delays.lastIndex);reconnectCount++;updateQueueState();val jitter=SecureRandom().nextInt((base/5).toInt().coerceAtLeast(1)).toLong();handler.removeCallbacks(reconnect);handler.postDelayed(reconnect,base-base/10+jitter)}
    private val reconnect=Runnable { ensureConnected() }
}
