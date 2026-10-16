#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_rom_sys.h>
#include <esp_sntp.h>
#include <esp_heap_caps.h>
#if CONFIG_IDF_TARGET_ESP32C3 && !ARDUINO_USB_CDC_ON_BOOT
#include <hal/usb_serial_jtag_ll.h>
#endif
#if defined(ROBODESK_BLE_DIAGNOSTIC) && ROBODESK_BLE_DIAGNOSTIC
#include <esp_timer.h>
#include <esp_wifi.h>
#include <hal/usb_serial_jtag_ll.h>
// Use the ROM console without installing a heap-backed CDC driver. A missing
// USB host may drop output, but each character waits at most one millisecond.
inline void gatewayDiagnosticUsbPutc(char c){
  const int64_t deadline=esp_timer_get_time()+1000;
  while(!usb_serial_jtag_ll_txfifo_writable()&&esp_timer_get_time()<deadline)esp_rom_delay_us(10);
  if(!usb_serial_jtag_ll_txfifo_writable())return;
  const uint8_t byte=static_cast<uint8_t>(c);
  usb_serial_jtag_ll_write_txfifo(&byte,1);usb_serial_jtag_ll_txfifo_flush();
}
#endif
#include "RoboBuildRole.h"
#if ROBODESK_NO_DASHBOARD
// No web server: take the non-HTTP headers SettingsDashboard.h used to pull into the gateway.
#include "RoboLog.h"
#include "RoboBoardProfile.h"
#include <HTTP_Method.h> // HTTPMethod enum only (gatewayRpc); no server code.
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "RoboMemoryDiagnostics.h"
#include "FirmwareOtaKey.h"
#include "RuntimeSettings.h"
#include "GitHubOtaUpdate.h"
#include "DashboardSecurity.h"
#include "PhoneNotificationBridge.h"
#else
#include "SettingsDashboard.h"
#endif
#include "PhoneBleTransport.h"
#include "RoboDualRuntime.h"
#include "RoboBehaviorWire.h"
#include "RoboSignedImage.h"
#include "TlsMemory.h"
#include "RoboExternalIntegrations.h"
#include "CompanionActivityCatalog.h"
#include "PhoneCompanionCommand.h"
#include "GoogleGtsRootR1.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

inline void gatewayTraceMin(const char*site){
  static uint32_t last=0xffffffffu;const uint32_t m=uint32_t(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(m+2048u<last){last=m;esp_rom_printf("[HeapTrace] %s min=%u free=%u largest=%u\n",site,unsigned(m),unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));}
}
inline RuntimeSettings gatewaySettings;
inline RuntimeSettingsStore gatewayStore;
#if ROBODESK_NO_DASHBOARD
// Keeps the setup-AP bookkeeping call sites (all no-ops) without compiling WebServer, the AP or mDNS.
struct GatewayNoDashboard{bool apStarted()const{return false;}bool startSetupAp(){return false;}bool stopSetupApIfUnused(){return false;}void service(uint32_t){}};
inline GatewayNoDashboard gatewayDashboard;
// Largest S3 reply still routed through gatewayRpc: external fetch body + 3-byte header + "text/plain" (see RoboDualRuntime.h).
static_assert(roboexternal::BodyCapacity+3+10<=RoboDualRuntime::MessageMax,"RPC buffer smaller than the largest S3 reply");
#else
inline SettingsDashboard gatewayDashboard;
#endif
inline PhoneNavigationBridge gatewayNavigation;
inline bool gatewayNavigationSupported=false;inline uint32_t gatewayPhonePeer=0,gatewayPhoneProbeAt=0,gatewayNavRevision=0;
inline PhoneNotificationBridge gatewayNotifications;
inline PhoneBleTransport gatewayBle;
inline bool gatewayBleReady=false;
inline constexpr uint32_t GatewayDashboardMinFreeHeap=24576u; // Dashboard load + /api/status pushed C3 free heap to 472 B (device 2026-10-08); shed status polls first.
inline bool gatewayBleOwnerSafe=true;
inline bool gatewayClearLocalPhoneCredential(){
#if ROBODESK_PHONE_BLE_ROBOT
  Preferences p;if(!p.begin("robodesk_v2auth",false))return false;
  const bool ok=(!p.isKey("record")||p.remove("record"))&&!p.isKey("record")&&p.getBytesLength("record")==0;p.end();
  if(!ok)return false;
  if(gatewaySettings.phoneBlePeer[0]){RuntimeSettings next=gatewaySettings;next.phoneBlePeer[0]=0;if(!gatewayStore.save(next))return false;gatewaySettings=next;}
  return true;
#else
  return gatewayBle.clearPeer(true);
#endif
}
#if defined(ROBODESK_NIMBLE_EXTERNAL)
#if defined(ROBODESK_BLE_DIAGNOSTIC) && ROBODESK_BLE_DIAGNOSTIC
inline constexpr size_t GatewayBleStartupReserve=73728; // Isolated measurement build only.
#else
inline constexpr size_t GatewayBleStartupReserve=96*1024; // Boot heap at the BLE gate is ~102 KB after the deferred RPC buffer (device 2026-10-08); 140 KiB is unreachable since Wi-Fi alone leaves 131 KB. 80 KiB previously let save/phone peaks reach ~2 KB (watchdog).
#endif
#else
inline constexpr size_t GatewayBleStartupReserve=150000;
#endif
#if defined(ROBODESK_NIMBLE_EXTERNAL) && (!defined(ROBODESK_BLE_DIAGNOSTIC) || !ROBODESK_BLE_DIAGNOSTIC)
inline constexpr size_t GatewayBleStartupLargestBlock=80*1024; // Measured pre-NimBLE block, rounded up. // Measured pre-NimBLE block, rounded up.
#else
inline constexpr size_t GatewayBleStartupLargestBlock=32768;
#endif
inline bool gatewayPhonePersistRemoteRevoke(void*){RuntimeSettings next=gatewaySettings;next.phoneBlePeer[0]=0;if(!gatewayStore.save(next))return false;gatewaySettings=next;return true;}
inline std::atomic<bool> gatewayConnecting{false},gatewayConnectDone{false};
inline uint32_t gatewayConnectGeneration=0,gatewayClockAt=0,gatewayWifiAt=0,gatewaySyncAt=0,gatewayPeer=0,gatewaySetupApAttemptAt=0;
inline uint8_t gatewayWifiIndex=0;
inline bool gatewayWifiPrevious=false,gatewaySuspended=false,gatewaySetupApAttempted=false,gatewayWifiReady=false;
inline bool gatewayForceSettingsSync=false;
inline uint8_t gatewayReceive[1024];
inline size_t gatewayReceiveSize=0,gatewayReceiveOffset=0;
// Raw relay: the S3 terminates TLS; the C3 only moves bytes to an allowlisted host.
// Reserves are unmeasured placeholders; BLE-H5 replaces them with measured values.
inline constexpr size_t GatewayRelayReserve=24576,GatewayRelayLargestBlock=8192;
inline WiFiClient gatewayRelay;inline bool gatewayRawMode=false;inline char gatewayRelayHost[robotunnel::HostMax+1];inline uint16_t gatewayRelayPort=0;
inline bool gatewayTunnelAllowed(const char*host,uint16_t port){
  robotunnel::Allowlist a;a.add("generativelanguage.googleapis.com",443);a.add("api.open-meteo.com",443);char h[robotunnel::HostMax+1];uint16_t p=0;
  if(roboexternal::configuredHomeAssistant()&&robotunnel::parseHttpsUrl(roboexternal::config.homeAssistantBase,h,sizeof(h),p))a.add(h,p);
  if(roboexternal::configuredCalendar()&&robotunnel::parseHttpsUrl(roboexternal::config.calendarUrl,h,sizeof(h),p))a.add(h,p);
  return a.allowed(host,port);
}
inline GitHubOtaUpdate gatewayLocalOta,gatewayRobotOta;
enum class GatewayUpdatePhase:uint8_t{Idle,CheckRobot,CheckGateway,Ready,InstallRobot,WaitRobot,InstallGateway,Done,Failed};
inline GatewayUpdatePhase gatewayUpdate=GatewayUpdatePhase::Idle;
inline bool gatewayLocalS3Active=false,gatewayLocalS3Ok=false;inline uint16_t gatewayLocalS3Code=0;inline String gatewayLocalS3Error;
inline uint32_t gatewayPairVersion=0,gatewayUpdateAt=0,gatewayRemoteOffset=0;
inline String gatewayUpdateError;
inline uint32_t gatewayRebootAt=0;
inline uint32_t gatewayExternalContextAt=0;
inline bool gatewayMigrationComplete=false;
inline uint8_t gatewayFactoryState=0;
inline uint32_t gatewayFactoryRetryAt=0;
// BLE ota-start: scheduled by the Command handler, started on the main loop after
// the response; runs the dashboard's signed GitHub pair flow (S3, confirm, C3).
inline PhoneSettingsProtocol::OtaStartScheduler gatewayBleOtaStart;
inline bool gatewayBleOtaAuto=false;
inline uint8_t gatewayBleOtaLast[6]{};  // state, error, target u32 (NVS robodesk_pair/ota_ble)
inline void gatewayOtaStatus(uint8_t out[7]);
struct GatewayPhoneCommand{uint32_t sessionId=0,requestCounter=0,ageMs=0,receivedAt=0;uint8_t size=0;uint8_t payload[12]{};};
inline QueueHandle_t gatewayPhoneCommandQueue=nullptr;
inline TaskHandle_t gatewayPhoneCommandTask=nullptr;
inline std::atomic<bool> gatewayPhoneCommandBusy{false};
inline bool gatewayUpdateBusy();
inline bool gatewayValidKey();
struct RoboGatewayReply {int code=503;String type="text/plain",body="Robot is disconnected";};
// Periodic, deferrable RPC work (settings sync, probes, context push) yields when the C3 heap is tight (audit 2026-10-13: ~43 KB free / 32 KB block with BLE).
inline bool gatewayHeapAllowsOptional(){return ESP.getFreeHeap()>=16384u&&heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>=8192u;}
inline RoboGatewayReply gatewayRpc(const String&uri,HTTPMethod method,JsonObjectConst args={},uint32_t timeout=5000){
  RoboGatewayReply out;if(!RoboDual.link.connected())return out;const uint32_t rpcStarted=millis();struct RpcTimer{const String&uri;uint32_t at;~RpcTimer(){gatewayTraceMin(uri.c_str());const uint32_t ms=uint32_t(millis()-at);if(ms>1500u)RoboLog.printf("LEV,RPC,SLOW,uri=%s,ms=%lu\n",uri.c_str(),(unsigned long)ms);}}rpcTimer{uri,rpcStarted};JsonDocument request;request["uri"]=uri;request["method"]=method==HTTP_GET?1:2;if(!args.isNull())request["args"].set(args);String wire;wire.reserve(measureJson(request));serializeJson(request,wire);
  if(!RoboDual.request(reinterpret_cast<const uint8_t*>(wire.c_str()),wire.length(),timeout)){out.body="Robot did not acknowledge the request";return out;}
  const size_t n=RoboDual.messageSize();const uint8_t*p=RoboDual.message();
  if(n>=3&&p[2]<=n-3){out.code=robolink::get16(p);out.type=String(reinterpret_cast<const char*>(p+3),p[2]);out.body=String(reinterpret_cast<const char*>(p+3+p[2]),n-3-p[2]);}
  RoboDual.finishRequest();return out;
}
inline bool gatewayDispatchPhoneCommand(uint32_t sessionId,uint32_t requestCounter,uint32_t ageMs,const uint8_t*payload,size_t size){
  if(!sessionId||!requestCounter||!payload||!size||size>8||!gatewayPhoneCommandQueue)return false;
  bool expected=false;if(!gatewayPhoneCommandBusy.compare_exchange_strong(expected,true))return false;
  GatewayPhoneCommand command{};command.sessionId=sessionId;command.requestCounter=requestCounter;command.ageMs=ageMs;command.receivedAt=millis();command.size=uint8_t(size);memcpy(command.payload,payload,size);
  if(xQueueSend(gatewayPhoneCommandQueue,&command,0)!=pdTRUE){memset(&command,0,sizeof(command));gatewayPhoneCommandBusy=false;return false;}return true;
}
// BLE settings (PhoneSettingsProtocol): runs on rdPhoneCmd. The only buffer is the
// 500 B BLE exchange slot; UART headers are prepended in place and the S3 reply
// lands in the same buffer, so neither the 12 KB RPC buffer nor a JSON document
// is allocated on the C3. Status flags were snapshotted on the main loop.
inline void gatewayRunSettingsExchange(const GatewayPhoneCommand&command){
  namespace ps=PhoneSettingsProtocol;auto&x=gatewayBle.settingsExchange();uint8_t*b=x.buffer();const uint8_t kind=command.payload[0];
  uint8_t status=ps::RobotUnavailable;size_t body=0;
  if(x.state()!=ps::Exchange::Working||x.session()!=command.sessionId||x.counter()!=command.requestCounter)return;
  if(!gatewayBle.sessionActive(command.sessionId)){x.complete(ps::NotAuthorized,0);return;}
  if(kind==ps::KindStatusSnapshot){
    ps::StatusSnapshot st{};uint32_t version=0;RoboSignedImage::version(&version);st.c3Version=version;st.c3UptimeMs=millis();
    st.c3HeapFree=ESP.getFreeHeap();st.c3HeapMinFree=ESP.getMinFreeHeap();st.c3HeapLargest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    const auto h=RoboDual.link.health();st.linkFlags=h.connected?1:0;st.linkAgeMs=h.ageMs;st.linkRetries=h.retries;st.linkCrcErrors=h.corrupt;st.linkDisconnects=h.disconnects;
    if(h.connected&&!gatewayUpdateBusy()){uint8_t*req=b+ps::Exchange::RequestOffset-5;req[4]=ps::LinkInfo;size_t n=0;
      if(RoboDual.settingsTransact(b,ps::Exchange::Capacity,size_t(req-b),5,1500,n)&&b[5]==ps::Ok&&n>=ps::ResponseHeader+ps::LinkInfoSize){
        st.s3Version=ps::get32(b+6);st.s3UptimeMs=ps::get32(b+10);st.s3InternalFree=ps::get32(b+14);st.s3InternalMinFree=ps::get32(b+18);st.linkFlags|=2;
        if(n>=ps::ResponseHeader+ps::LinkInfoSizeV2)ps::getLive(b+ps::ResponseHeader+ps::LinkInfoSize,st);if(n>=ps::ResponseHeader+ps::LinkInfoSizeV3)ps::getMood(b+ps::ResponseHeader+ps::LinkInfoSizeV2,st);}}
    const bool wifi=WiFi.status()==WL_CONNECTED;st.wifiActiveSlot=wifi?uint8_t(gatewayWifiIndex+1):0;st.integrationBusy=roboexternal::busy()?1:0;st.wifiState=uint8_t((wifi?1:0)|((command.payload[3]&1)?2:0));st.wifiRssi=wifi?int8_t(WiFi.RSSI()):0;
    st.configFlags=command.payload[1];st.bleFlags=command.payload[2];st.otaState=command.payload[4];st.otaError=command.payload[5];st.otaPercent=command.payload[6];st.otaTargetVersion=ps::get32(command.payload+7);
    memset(b+ps::ResponseHeader,0,ps::Exchange::Capacity-ps::ResponseHeader);body=ps::encodeStatusPage(st,b+ps::ResponseHeader,ps::Exchange::Capacity-ps::ResponseHeader);x.complete(body?ps::Ok:ps::NoMemory,body);return;
  }
  const bool live=kind==ps::KindCommand;  // live ops 4..13 only (gatewayDispatchSettings); forwarded as LinkCommand
  const bool forward=ps::isForwardedKind(kind)||live;const bool irList=kind==ps::KindIrProfileList;  // kind 20: S3 op LinkIrProfiles, request = page byte like GetPage
  if(kind!=ps::KindSettingsGet&&kind!=ps::KindSettingsSet&&!forward&&!irList){x.complete(ps::Unsupported,0);return;}
  const uint8_t gate=gatewayUpdateBusy()?ps::UpdateBusy:(RoboDual.link.connected()?ps::Ok:ps::RobotUnavailable);if(gate!=ps::Ok){x.complete(gate,0);return;}
  const bool put=kind==ps::KindSettingsSet;const bool withSession=put||forward;const size_t header=withSession?9:5;uint8_t*req=b+ps::Exchange::RequestOffset-header;
  req[4]=live?ps::LinkCommand:(irList?ps::LinkIrProfiles:(forward?ps::linkOpForKind(kind):(put?ps::LinkPutPage:ps::LinkGetPage)));if(withSession)ps::put32(req+5,command.sessionId);
  size_t n=0;if(RoboDual.settingsTransact(b,ps::Exchange::Capacity,size_t(req-b),header+x.requestSize(),(put||(forward&&!live&&!ps::isListKind(kind)&&!ps::isChatKind(kind)))?8000:3000,n)){status=b[5];body=n-ps::ResponseHeader;}
  x.complete(status,body);
}
inline void gatewayPhoneCommandWorker(void*){
  for(;;){GatewayPhoneCommand command{};if(xQueueReceive(gatewayPhoneCommandQueue,&command,portMAX_DELAY)!=pdTRUE)continue;
    if(!command.size){gatewayRunSettingsExchange(command);memset(&command,0,sizeof(command));continue;}
    PhoneCompanionCommand::Command decoded{};int code=503;
    const bool fresh=uint32_t(millis()-command.receivedAt)+command.ageMs<30000u;
    if(fresh&&gatewayBle.sessionActive(command.sessionId)&&PhoneCompanionCommand::decode(command.payload,command.size,decoded)&&RoboDual.link.connected()&&!gatewayUpdateBusy()){
      if(decoded.operation==PhoneCompanionCommand::Operation::SetIntensity||decoded.operation==PhoneCompanionCommand::Operation::SetQuietHours){
        JsonDocument args;if(decoded.operation==PhoneCompanionCommand::Operation::SetIntensity)args["intensity"]=String(decoded.intensity);
        else{args["enabled"]=String(decoded.quietEnabled?1:0);if(decoded.hasQuietTimes){args["startMin"]=String(decoded.quietStartMin);args["endMin"]=String(decoded.quietEndMin);}}
        code=gatewayRpc("/_companion/settings",HTTP_POST,args.as<JsonObjectConst>(),5000).code;
      }else{
        const char*action=nullptr;String value;
        switch(decoded.operation){
          case PhoneCompanionCommand::Operation::StartActivity:{const auto*definition=companion::activityDefinition(decoded.activityId);if(definition){action="activity_start";value=definition->name;}break;}
          case PhoneCompanionCommand::Operation::PauseActivity:action="activity_pause";break;
          case PhoneCompanionCommand::Operation::ResumeActivity:action="activity_resume";break;
          case PhoneCompanionCommand::Operation::CancelActivity:action="activity_cancel";break;
          default:break;
        }
        if(action){JsonDocument args;args["action"]=String(action);if(value.length())args["value"]=value;code=gatewayRpc("/companion/action",HTTP_POST,args.as<JsonObjectConst>(),5000).code;}
      }
    }
    const uint8_t status=code==200?0:1;const uint8_t reason=code==200?0:(code==409?2:((code==400||code==404)?3:(code==503?1:4)));
    gatewayBle.publishCompanionResult(command.sessionId,command.requestCounter,status,reason);memset(&command,0,sizeof(command));gatewayPhoneCommandBusy=false;
  }
}
inline bool gatewaySetFactoryState(uint8_t state){Preferences p;if(!p.begin("robodesk_pair",false))return false;bool ok=p.putUChar("factory",state)==1;p.end();if(ok)gatewayFactoryState=state;return ok;}
inline bool gatewayClearFactoryLocal(){
  if(roboexternal::busy()||!gatewayClearLocalPhoneCredential()||!gatewayStore.clear()||!roboexternal::save(roboexternal::Config{}))return false;
  Preferences p;if(!p.begin("robodesk_pair",false))return false;const bool ok=p.clear();p.end();if(!ok)return false;
  gatewayFactoryState=0;gatewayRebootAt=millis()+1000;return true;
}
inline bool gatewayCompleteFactory(){
  if(gatewayFactoryState==1){const auto r=gatewayRpc("/factory",HTTP_POST);if(r.code!=200||!gatewaySetFactoryState(2))return false;}
  return gatewayFactoryState==2&&gatewayClearFactoryLocal();
}
// Shared by the dashboard actions and the dashboard-free triggers (S3 head+side hold, USB serial).
inline bool gatewayPairingPhysical=false; // window opened by the S3 head-touch hold: physical presence plus the typed passkey replace the dashboard approval
inline bool gatewayOpenPairing(){gatewayPairingPhysical=false;RuntimeSettings next=gatewaySettings;next.phoneBlePeer[0]=0;if(!gatewayStore.save(next))return false;gatewaySettings=next;if(!gatewayBle.clearPeer())return false;gatewayBle.openPairingWindow(millis());return true;}
inline bool gatewayApproveCandidate(){PhoneBleTransport::CandidateApproval candidate{};if(!gatewayBle.snapshotCandidateApproval(candidate)||!gatewayBle.prepareCandidateApproval(candidate))return false;
  const RuntimeSettings previous=gatewaySettings;RuntimeSettings next=gatewaySettings;RuntimeSettings::copy(next.phoneBlePeer,sizeof(next.phoneBlePeer),candidate.address);
  if(gatewayStore.save(next)){gatewaySettings=next;if(gatewayBle.approveCandidate(candidate))return true;}
  gatewayBle.clearPeer();gatewaySettings=previous;gatewaySettings.phoneBlePeer[0]=0;gatewayStore.save(gatewaySettings);return false;}
// Write-only secrets from an authenticated BLE session. Main loop only (owns
// gatewaySettings); values are never logged or echoed and the copies are wiped.
inline void gatewayCopyBleField(char*out,size_t cap,const uint8_t*p,size_t n){memset(out,0,cap);if(n>=cap)n=cap-1;if(n)memcpy(out,p,n);}
inline uint8_t gatewayApplyBleSecret(const uint8_t*p,size_t n){
  namespace ps=PhoneSettingsProtocol;ps::Secret secret{};if(!ps::decodeSecret(p,n,secret))return ps::BadRequest;
  RuntimeSettings next=gatewaySettings;bool wifi=secret.type==ps::SecretWifi;
  if(wifi){char*ssid=secret.slot==0?next.wifiSsid:(secret.slot==1?next.wifiSsid2:next.wifiSsid3);char*password=secret.slot==0?next.wifiPassword:(secret.slot==1?next.wifiPassword2:next.wifiPassword3);
    gatewayCopyBleField(ssid,33,secret.ssid,secret.ssidLength);if(!(secret.flags&ps::WifiKeepPassword))gatewayCopyBleField(password,65,secret.password,secret.passwordLength);}
  else gatewayCopyBleField(next.geminiApiKey,sizeof(next.geminiApiKey),secret.key,secret.keyLength);
  const bool saved=gatewayStore.save(next);const bool ok=saved||(wifi&&gatewayStore.wifiCredentialsCommittedThisSave());
  if(ok){gatewaySettings=next;if(wifi)gatewayWifiAt=0;}
  volatile uint8_t*wipe=reinterpret_cast<volatile uint8_t*>(&next);for(size_t i=0;i<sizeof(next);++i)wipe[i]=0;
  return ok?ps::Ok:ps::StorageFailed;
}
// Factory reset reuses the persisted dashboard path: state 1 is stored now and
// the main loop runs gatewayCompleteFactory() after the response has gone out.
inline uint8_t gatewayRunBleCommand(const uint8_t*p,size_t n,uint32_t sessionId){
  namespace ps=PhoneSettingsProtocol;ps::Command command{};const uint8_t decoded=ps::decodeCommand(p,n,sessionId,command);if(decoded!=ps::Ok)return decoded;
  if(ps::isLiveCommand(command.op))return ps::Unsupported;  // routed to the S3 by gatewayDispatchSettings
  if(command.op==ps::CommandOtaStart){ps::OtaStartGate g;g.admitted=gatewayBle.sessionActive(sessionId);g.updateBusy=gatewayUpdateBusy()||gatewayBleOtaAuto||gatewayFactoryState||gatewayLocalS3Active;
    g.linkConnected=RoboDual.link.connected();g.wifiConnected=WiFi.status()==WL_CONNECTED;g.clockSet=time(nullptr)>=1700000000;
    const uint8_t status=gatewayBleOtaStart.request(g,millis());if(status==ps::Ok)gatewayBleOtaLast[0]=ps::OtaIdle;return status;}
  if(gatewayUpdateBusy()||gatewayBleOtaStart.pending())return ps::UpdateBusy;
  if(command.op==ps::CommandFactoryReset){if(!gatewayFactoryState&&!gatewaySetFactoryState(1))return ps::StorageFailed;gatewayFactoryRetryAt=millis();return ps::Ok;}
  if(command.target&ps::RebootRobot){if(!RoboDual.link.connected())return ps::RobotUnavailable;uint8_t frame[5]{};frame[4]=ps::LinkReboot;if(!RoboDual.link.send(robolink::Settings,frame,sizeof(frame),0,10))return ps::Busy;}
  if(command.target&ps::RebootGateway)gatewayRebootAt=millis()+1500;
  return ps::Ok;
}
// External integrations over BLE (kinds 21..24). Main loop only, like the dashboard /external/* handlers: the same
// roboexternal validators, save(), requestRefresh() and busy(); no allocation (documents are built in the 500 B exchange
// buffer). The HA token and calendar URL are write-only: never copied into a response, local copies are wiped.
inline void gatewayWipeBytes(void*p,size_t n){volatile uint8_t*w=static_cast<volatile uint8_t*>(p);for(size_t i=0;i<n;++i)w[i]=0;}
inline uint8_t gatewayApplyIntegrationsSet(const uint8_t*p,size_t n){
  namespace ps=PhoneSettingsProtocol;ps::IntegrationsSet s{};if(!ps::decodeIntegrationsSet(p,n,s))return ps::BadRequest;
  if(roboexternal::busy())return ps::Busy;
  roboexternal::Config next=roboexternal::config;uint8_t status=ps::Ok;
  if(s.type==ps::IntSetConfig){
    memset(next.homeAssistantBase,0,sizeof(next.homeAssistantBase));memcpy(next.homeAssistantBase,s.base,s.baseLength);
    if(next.homeAssistantBase[0]&&!roboexternal::httpsUrl(next.homeAssistantBase,sizeof(next.homeAssistantBase),false))status=ps::BadRequest;
    memset(next.entities,0,sizeof(next.entities));
    for(uint8_t i=0;status==ps::Ok&&i<s.entityCount;++i){memcpy(next.entities[i],s.entity[i],s.entityLength[i]);
      if(!roboexternal::validEntity(next.entities[i],sizeof(next.entities[i])))status=ps::BadRequest;
      for(uint8_t j=0;j<i;++j)if(!strcmp(next.entities[j],next.entities[i]))status=ps::BadRequest;}
    next.latitude=s.latitude;next.longitude=s.longitude;next.weatherEnabled=(s.flags&ps::IntFlagWeather)?1:0;
  }else if(s.type==ps::IntSetToken){memset(next.homeAssistantToken,0,sizeof(next.homeAssistantToken));memcpy(next.homeAssistantToken,s.text,s.textLength);
  }else{memset(next.calendarUrl,0,sizeof(next.calendarUrl));memcpy(next.calendarUrl,s.text,s.textLength);
    if(next.calendarUrl[0]&&!roboexternal::httpsUrl(next.calendarUrl,sizeof(next.calendarUrl),true))status=ps::BadRequest;}
  if(status==ps::Ok&&!roboexternal::save(next))status=ps::StorageFailed;
  if(status==ps::Ok){roboexternal::Snapshot empty{};portENTER_CRITICAL(&roboexternal::stateLock);roboexternal::snapshot=empty;portEXIT_CRITICAL(&roboexternal::stateLock);roboexternal::requestRefresh();}
  gatewayWipeBytes(&next,sizeof(next));return status;
}
inline bool gatewayRunIntegrations(uint8_t kind){
  namespace ps=PhoneSettingsProtocol;auto&x=gatewayBle.settingsExchange();uint8_t*b=x.buffer();uint8_t*body=b+ps::ResponseHeader;
  if(kind==ps::KindIntegrationsSet)return x.complete(gatewayApplyIntegrationsSet(x.request(),x.requestSize()),0);
  if(kind==ps::KindIntegrationsAction){const uint8_t sub=x.requestSize()==1?x.request()[0]:0;
    if(sub==ps::IntActionRefresh){roboexternal::requestRefresh();return x.complete(ps::Ok,0);}  // queued, as the dashboard's 202
    if(sub!=ps::IntActionClear)return x.complete(ps::BadRequest,0);
    if(roboexternal::busy())return x.complete(ps::Busy,0);
    if(!roboexternal::save(roboexternal::Config{}))return x.complete(ps::StorageFailed,0);
    roboexternal::Snapshot empty{};portENTER_CRITICAL(&roboexternal::stateLock);roboexternal::snapshot=empty;portEXIT_CRITICAL(&roboexternal::stateLock);return x.complete(ps::Ok,0);}
  size_t doc=0;
  if(kind==ps::KindIntegrationsGet){const auto&c=roboexternal::config;const char*entities[ps::IntEntities]={c.entities[0],c.entities[1],c.entities[2]};
    doc=ps::encodeIntegrationsConfig(body+ps::PageHeader,ps::Exchange::Capacity-ps::ResponseHeader-ps::PageHeader,c.weatherEnabled!=0,c.homeAssistantToken[0]!=0,c.calendarUrl[0]!=0,c.latitude,c.longitude,c.homeAssistantBase,entities);
  }else{const uint32_t now=millis();roboexternal::Snapshot s;portENTER_CRITICAL(&roboexternal::stateLock);s=roboexternal::snapshot;portEXIT_CRITICAL(&roboexternal::stateLock);
    const auto&c=roboexternal::config;ps::IntegrationsStatusData d;d.busy=roboexternal::busy();d.weatherConfigured=roboexternal::configuredWeather();d.weatherValid=s.weatherValid;
    d.homeAssistantConfigured=roboexternal::configuredHomeAssistant();d.calendarConfigured=roboexternal::configuredCalendar();d.calendarValid=s.calendarValid;
    d.weatherError=s.weatherError;d.homeAssistantError=s.homeAssistantError;d.calendarError=s.calendarError;
    d.weatherAgeMs=s.weatherAt?uint32_t(now-s.weatherAt):0;d.calendarAgeMs=s.calendarAt?uint32_t(now-s.calendarAt):0;d.weather=s.weather;d.calendar=s.calendar;
    for(uint8_t i=0;i<ps::IntEntities;++i)if(roboexternal::validEntity(c.entities[i],sizeof(c.entities[i]))){auto&e=d.entity[d.entityCount++];e.slot=i;e.valid=s.entities[i].valid;e.ageMs=s.entities[i].updatedAt?uint32_t(now-s.entities[i].updatedAt):0;e.state=s.entities[i].valid?s.entities[i].value:"";}
    doc=ps::encodeIntegrationsStatus(body+ps::PageHeader,ps::Exchange::Capacity-ps::ResponseHeader-ps::PageHeader,d);}
  const size_t n=ps::sealOnePage(body,doc);return x.complete(n?ps::Ok:ps::NoMemory,n);
}
inline bool gatewayDispatchSettings(uint32_t sessionId,uint32_t requestCounter,uint8_t kind){
  namespace ps=PhoneSettingsProtocol;auto&x=gatewayBle.settingsExchange();
  if(kind>=ps::KindIntegrationsGet&&kind<=ps::KindIntegrationsStatus)return gatewayRunIntegrations(kind);
  if(kind==ps::KindSecretSet){const uint8_t status=gatewayApplyBleSecret(x.request(),x.requestSize());return x.complete(status,0);}
  if(kind==ps::KindCommand&&!(x.requestSize()&&ps::isLiveCommand(x.request()[0]))){const uint8_t status=gatewayRunBleCommand(x.request(),x.requestSize(),sessionId);return x.complete(status,0);}
  if(!gatewayPhoneCommandQueue)return false;
  GatewayPhoneCommand command{};command.sessionId=sessionId;command.requestCounter=requestCounter;command.receivedAt=millis();command.size=0;command.payload[0]=kind;
  command.payload[1]=uint8_t((gatewayValidKey()?1:0)|(gatewaySettings.wifiNetworkConfigured(0)?2:0)|(gatewaySettings.wifiNetworkConfigured(1)?4:0)|(gatewaySettings.wifiNetworkConfigured(2)?8:0));
  command.payload[2]=uint8_t((gatewayBleReady?1:0)|(gatewayBle.connected()?2:0)|(gatewayBle.pairingOpen(millis())?4:0)|(gatewaySettings.phoneBlePeer[0]?8:0));
  command.payload[3]=gatewayDashboard.apStarted()?1:0;gatewayOtaStatus(command.payload+4);
  return xQueueSend(gatewayPhoneCommandQueue,&command,0)==pdTRUE;
}
inline bool gatewayMigrateCredentials(){
  const auto reply=gatewayRpc("/_migration/get",HTTP_GET);if(reply.code==204)return true;if(reply.code!=200)return false;JsonDocument doc;if(deserializeJson(doc,reply.body))return false;const uint32_t id=doc["id"]|0u;if(!id)return false;
  Preferences p;if(!p.begin("robodesk_pair",false))return false;const bool already=p.getUInt("imported",0)==id;
  if(!already){RuntimeSettings next=gatewaySettings;
    for(unsigned i=0;i<3;++i){if(gatewaySettings.wifiNetworkConfigured(i))continue;
      const char*sk=i==0?"wifiSsid":(i==1?"wifiSsid2":"wifiSsid3");const char*pk=i==0?"wifiPassword":(i==1?"wifiPassword2":"wifiPassword3");
      char*ssid=i==0?next.wifiSsid:(i==1?next.wifiSsid2:next.wifiSsid3);char*password=i==0?next.wifiPassword:(i==1?next.wifiPassword2:next.wifiPassword3);
      RuntimeSettings::copy(ssid,33,doc[sk]|"");RuntimeSettings::copy(password,65,doc[pk]|"");}
    if(!next.geminiConfigured())RuntimeSettings::copy(next.geminiApiKey,sizeof(next.geminiApiKey),doc["geminiApiKey"]|"");
    if(!strcmp(next.adminPin,"robodesk")&&strlen(doc["adminPin"]|"")>=6)RuntimeSettings::copy(next.adminPin,sizeof(next.adminPin),doc["adminPin"]|"");
    if(!gatewayStore.save(next)||p.putUInt("imported",id)!=4){p.end();return false;}gatewaySettings=next;gatewayWifiAt=0;
  }p.end();JsonDocument args;args["id"]=String(id);return gatewayRpc("/_migration/commit",HTTP_POST,args.as<JsonObjectConst>()).code==200;
}
inline bool gatewaySyncSettings(bool forcePersist=false){const auto reply=gatewayRpc("/_settings/get",HTTP_GET);if(reply.code!=200)return false;JsonDocument doc;RuntimeSettings next=gatewaySettings;if(deserializeJson(doc,reply.body)||!roboBehaviorImport(doc.as<JsonObjectConst>(),next))return false;
  bool changed=forcePersist;if(!changed)changed=!roboBehaviorSettingsEqual(gatewaySettings,next);
  if(changed&&!gatewayStore.save(next))return false;gatewaySettings=next;return true;}
#if !ROBODESK_NO_DASHBOARD
inline bool gatewaySave(void*,WebServer&server,RuntimeSettings&next){
  // Wi-Fi and gateway credentials are intentionally absent from the behavior
  // RPC schema. Skip the synchronous S3 request when only C3-owned settings
  // changed, so saving fallback networks does not wait for the paired S3 app.
  const bool ownerChanged=server.hasArg("owner")&&server.arg("owner").length()>0;
  const bool behaviorSame=roboBehaviorSettingsEqual(gatewaySettings,next);
  esp_rom_printf("[CFG_SAVE] owner-changed=%u behavior-same=%u\n",unsigned(ownerChanged),unsigned(behaviorSame));
  if(!ownerChanged&&behaviorSame){
    gatewayWifiAt=0;
    esp_rom_printf("[CFG_SAVE] s3-rpc-skipped\n");
    return true;
  }
  if(!RoboDual.link.connected()){
    // The dashboard already persisted next. Apply C3 connectivity even when
    // the robot is absent; its behavior snapshot will refresh after reconnect.
    gatewaySettings=next;gatewayForceSettingsSync=true;gatewayRebootAt=millis()+1200;
    static const char localSaved[] PROGMEM="<!doctype html><html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>RoboDesk</title><body><h1>C3 settings saved</h1><p>C3 will restart and apply the Wi-Fi settings.</p><p>S3 is disconnected. Robot settings have not been applied and will refresh from S3 when it reconnects.</p><a href='/'>Return to dashboard</a></body></html>";
    server.send_P(200,PSTR("text/html; charset=utf-8"),localSaved);return false;
  }
  JsonDocument behavior,args;roboBehaviorExport(next,behavior.to<JsonObject>());String settings;serializeJson(behavior,settings);args["settings"]=settings;if(server.hasArg("owner"))args["owner"]=server.arg("owner");
  esp_rom_printf("[CFG_SAVE] s3-rpc-start\n");
  const auto reply=gatewayRpc("/_settings/apply",HTTP_POST,args.as<JsonObjectConst>(),12000);esp_rom_printf("[CFG_SAVE] s3-rpc-code=%d body=%s\n",reply.code,reply.body.c_str());if(reply.code!=200){gatewayForceSettingsSync=true;const bool reconciled=gatewaySyncSettings(true);if(reconciled)gatewayForceSettingsSync=false;server.send(reply.code?reply.code:503,"text/plain",reconciled?reply.body:"Settings are saved on the gateway; robot state is not confirmed and will be reconciled after the link recovers");return false;}return true;
}
inline bool gatewayPhonePlatformChanged(void*,uint8_t,uint8_t){
#if ROBODESK_PHONE_BLE_ROBOT
  return gatewayRpc("/_phone/forget",HTTP_POST).code==200&&gatewayClearLocalPhoneCredential();
#else
  const bool erased=gatewayBle.clearPeer(true);const bool saved=gatewayStore.save(gatewaySettings);return erased&&saved;
#endif
}
#endif
inline void gatewayLinkJson(JsonObject root){const auto h=RoboDual.link.health();auto link=root["boardLink"].to<JsonObject>();link["connected"]=h.connected;link["protocol"]=1;link["baud"]=921600;link["ageMs"]=h.ageMs;link["received"]=h.received;link["sent"]=h.sent;link["retries"]=h.retries;link["crcErrors"]=h.corrupt;link["disconnects"]=h.disconnects;link["queuedFrames"]=RoboDual.link.queuedFrames();link["queuedFramesHighWater"]=RoboDual.link.queuedFramesHighWater();link["streamBackpressure"]=RoboDual.streamRejected.load();
  auto board=root["gateway"].to<JsonObject>();board["role"]="c3-gateway";board["uptimeMs"]=millis();board["resetReason"]=int(esp_reset_reason());uint32_t version=0;RoboSignedImage::version(&version);board["version"]=version;board["heap"]=ESP.getFreeHeap();board["minimumHeap"]=ESP.getMinFreeHeap();board["largestBlock"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);board["phoneBleReady"]=gatewayBleReady;board["boardLinkStackMinimumFree"]=RoboDual.link.stackMinimumFree();board["phoneCommandStackMinimumFree"]=gatewayPhoneCommandTask?uxTaskGetStackHighWaterMark(gatewayPhoneCommandTask):0;
  board["heapReserveHealthy"]=ESP.getFreeHeap()>=49152&&heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>=24576;
  board["phoneBleStartupStage"]=gatewayBle.startupStage();board["phoneBleFailureReason"]=gatewayBle.startupFailureReason();board["phoneBleStartupReserve"]=GatewayBleStartupReserve;board["phoneBleStartupLargestBlockReserve"]=GatewayBleStartupLargestBlock;board["phoneBleHostStackMinimumFree"]=gatewayBle.hostStackMinimumFree();
#if defined(ROBODESK_BLE_DIAGNOSTIC) && ROBODESK_BLE_DIAGNOSTIC
  board["phoneBleDiagnostic"]=true;
#else
  board["phoneBleDiagnostic"]=false;
#endif
}
inline void gatewayLogLinkDiagnostics(uint32_t now){static uint32_t lastAt=0;if(uint32_t(now-lastAt)<5000u)return;lastAt=now;const auto h=RoboDual.link.health();esp_rom_printf("[RoboDeskLink] connected=%u ageMs=%u rx=%u tx=%u retries=%u crc=%u disconnects=%u wifi=%u ap=%u ble=%u heap=%u largest=%u min=%u bleStack=%u q=%u qmax=%u lnkStack=%u cmdStack=%u logStack=%u logTrunc=%u logDrop=%u\n",unsigned(h.connected),unsigned(h.ageMs),unsigned(h.received),unsigned(h.sent),unsigned(h.retries),unsigned(h.corrupt),unsigned(h.disconnects),unsigned(WiFi.status()==WL_CONNECTED),unsigned(gatewayDashboard.apStarted()),unsigned(gatewayBleReady),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(gatewayBle.hostStackMinimumFree()),unsigned(RoboDual.link.queuedFrames()),unsigned(RoboDual.link.queuedFramesHighWater()),unsigned(RoboDual.link.stackMinimumFree()),unsigned(gatewayPhoneCommandTask?uxTaskGetStackHighWaterMark(gatewayPhoneCommandTask):0),unsigned(RoboLog.stackHighWaterBytes()),unsigned(RoboLog.lineTruncations()),unsigned(RoboLog.dropped()));}
#if !ROBODESK_NO_DASHBOARD
inline bool gatewayProxy(void*,WebServer&server){
  const String uri=server.uri();
  if(uri=="/api/status"&&ESP.getFreeHeap()<GatewayDashboardMinFreeHeap){server.sendHeader("Retry-After","3");server.send(503,"application/json","{\"error\":\"Gateway memory is low; retrying\"}");return true;}
  if(uri=="/factory"){
    if(gatewayUpdateBusy()){server.send(409,"text/plain","Wait for the firmware update to finish");return true;}
    if(!gatewayFactoryState&&!gatewaySetFactoryState(1)){server.send(500,"text/plain","Could not persist factory reset request");return true;}
    const bool ok=gatewayCompleteFactory();server.send(ok?200:202,"text/plain",ok?"Both boards cleared; rebooting":"Factory reset pending; reconnect both boards. Reset resumes automatically, including after power loss.");return true;
  }
  
  if(!ROBODESK_PHONE_BLE_ROBOT&&uri=="/companion/action"&&server.arg("action").startsWith("ble_")){
    const String action=server.arg("action");bool ok=gatewayBleReady;
    if(ok&&action=="ble_pair")ok=gatewayOpenPairing();
    else if(ok&&action=="ble_approve")ok=gatewayApproveCandidate();
    else ok=false;server.send(ok?200:409,"application/json",ok?"{\"accepted\":true}":"{\"accepted\":false}");return true;
  }
  JsonDocument args;for(int i=0;i<server.args();++i){const String name=server.argName(i);if(name=="ssid"||name=="ssid2"||name=="ssid3"||name=="wpass"||name=="wpass2"||name=="wpass3"||name=="gkey"||name=="pin")continue;args[name]=server.arg(i);}
  const auto reply=gatewayRpc(uri,server.method(),args.as<JsonObjectConst>());
  if(uri=="/api/status"){
    JsonDocument doc;if(reply.code==200&&deserializeJson(doc,reply.body)){server.send(502,"application/json","{\"error\":\"Invalid robot status\"}");return true;}
    auto root=reply.code==200?doc.as<JsonObject>():doc.to<JsonObject>();
    root["wifi"]=WiFi.status()==WL_CONNECTED;root["setupAp"]=gatewayDashboard.apStarted();root["rssi"]=WiFi.RSSI();root["robotConnected"]=reply.code==200;root["robotStatusStale"]=reply.code!=200;root["heap"]=ESP.getFreeHeap();
    if(reply.code==200)root["robot"]["memory"].set(root["memory"]);
    auto memory=root["memory"].to<JsonObject>();const auto snap=roboMemorySnapshot();memory["internalFree"]=snap.internalFree;memory["internalMinimumFree"]=snap.internalMinimumFree;memory["internalLargestBlock"]=snap.internalLargestBlock;memory["psramFree"]=0;
    root["ssid"]=gatewaySettings.wifiSsid;root["activeSsid"]=WiFi.SSID();root["model"]=gatewaySettings.geminiModel;root["voice"]=gatewaySettings.geminiVoice;
    auto phone=root["phoneBridge"].as<JsonObject>();if(phone.isNull())phone=root["phoneBridge"].to<JsonObject>();
#if ROBODESK_PHONE_BLE_ROBOT
    phone["owner"]="s3";phone["ready"]=reply.code==200&&bool(phone["available"]|false)&&gatewayBleOwnerSafe;
#else
    phone["owner"]="c3";phone["ready"]=gatewayBleReady;phone["connected"]=gatewayBle.connected();phone["pairing"]=gatewayBle.pairingOpen(millis());phone["candidate"]=gatewayBle.candidatePeer();phone["ancs"]=gatewayBle.ancsStatus();phone["paired"]=gatewaySettings.phoneBlePeer[0]!=0;
#endif
    phone["platform"]=gatewaySettings.phonePlatform?"iphone":"android";
    auto nav=root["navigation"].as<JsonObject>();if(nav.isNull())nav=root["navigation"].to<JsonObject>();nav["supported"]=gatewayNavigationSupported;nav["enabled"]=gatewaySettings.navigationEnabled!=0;nav["source"]=gatewaySettings.phonePlatform?"iphone-unavailable":"android-google-maps";
    gatewayLinkJson(root);roboexternal::appendStatus(root,millis());String body;serializeJson(doc,body);server.send(200,"application/json",body);return true;
  }
  if(uri=="/reboot"&&reply.code==200)gatewayRebootAt=millis()+1200;
  if(reply.code==302)server.sendHeader("Location","/");server.send(reply.code,reply.type.c_str(),reply.body);return true;
}
#endif
inline void gatewayRelayConnectTask(void*){
  gatewayRelay.stop();gatewayRelay.setTimeout(3);const bool ok=gatewayRelay.connect(gatewayRelayHost,gatewayRelayPort,3000);
  RoboDual.tcpConnected=ok&&RoboDual.tcpGeneration.load()==gatewayConnectGeneration&&RoboDual.link.connected();if(!RoboDual.tcpConnected)gatewayRelay.stop();gatewayConnectDone=true;gatewayConnecting=false;vTaskDelete(nullptr);
}
inline void gatewayPublishTcpState(){uint8_t state[5];robolink::put32(state,RoboDual.tcpGeneration.load());state[4]=RoboDual.tcpConnected?1:0;RoboDual.link.send(robolink::TcpState,state,sizeof(state),0,10);}
inline void gatewayStopTcp(){if(gatewayConnecting)return;gatewayRelay.stop();gatewayRawMode=false;RoboDual.tcpConnected=false;RoboDual.clearTcp();gatewayReceiveSize=gatewayReceiveOffset=0;gatewayPublishTcpState();}
inline bool gatewayValidKey(){if(!gatewaySettings.geminiConfigured())return false;for(const char*p=gatewaySettings.geminiApiKey;*p;++p)if(!isalnum(static_cast<unsigned char>(*p))&&*p!='-'&&*p!='_'&&*p!='.')return false;return true;}
// Integrations fetch over HTTPS on the S3 (async: POST starts, GET polls); the C3 only parses the body.
inline uint8_t gatewayExternalFetch(const String&url,const char*bearer,char*out,size_t cap,size_t&size){
  size=0;JsonDocument args;args["url"]=url;if(bearer&&*bearer)args["bearer"]=bearer;
  RoboGatewayReply r=gatewayRpc("/_external/fetch",HTTP_POST,args.as<JsonObjectConst>(),3000);if(r.code!=202)return 3;
  const uint32_t started=millis();
  while(uint32_t(millis()-started)<15000u){
    vTaskDelay(pdMS_TO_TICKS(150));if(!RoboDual.link.connected())return 3;
    r=gatewayRpc("/_external/fetch/result",HTTP_GET,JsonObjectConst(),3000);
    if(r.code==204)continue;
    if(r.code==200){if(r.body.length()>=cap)return 2;memcpy(out,r.body.c_str(),r.body.length());out[r.body.length()]=0;size=r.body.length();return 0;}
    if(r.code==413)return 2;
    if(r.code==422){const int st=r.body.toInt();return st==401||st==403?4:(st>=400&&st<500?5:6);}
    return 3;
  }
  return 3;
}
// Send the Gemini key to the S3 once per link session and whenever it changes; length 0 wipes it.
inline void gatewayServiceKey(){
  static RoboGeminiKeySync sync;
  const auto h=RoboDual.link.health();if(!h.connected){sync.sent=false;return;}
  const uint32_t session=h.peerSession;const bool valid=gatewayValidKey()&&strlen(gatewaySettings.geminiApiKey)<=96;
  const size_t n=valid?strlen(gatewaySettings.geminiApiKey):0;const uint32_t crc=n?robolink::crc32(reinterpret_cast<const uint8_t*>(gatewaySettings.geminiApiKey),n):0;
  if(!sync.due(h,crc))return;
  {const char*k=gatewaySettings.geminiApiKey;unsigned bad=0;for(unsigned i=0;k[i];++i)if(!isalnum(static_cast<unsigned char>(k[i]))&&k[i]!='-'&&k[i]!='_'){bad=i+1;break;}
   esp_rom_printf("[Key] valid=%u len=%u raw=%u badAt=%u session=%u\n",unsigned(valid),unsigned(n),unsigned(strlen(k)),bad,unsigned(session));}
  if(RoboDual.link.send(robolink::GeminiKey,reinterpret_cast<const uint8_t*>(gatewaySettings.geminiApiKey),n,0,10))sync.mark(h,crc);
}
inline void gatewayServiceTcp(){
  if(gatewayConnecting)return;
  if(gatewayConnectDone.exchange(false))gatewayPublishTcpState();
  if(RoboDual.closePending.exchange(false))gatewayStopTcp();
  if(RoboDual.openPending.exchange(false)){
    gatewayStopTcp();robotunnel::OpenRequest req;const bool haveRequest=RoboDual.takeOpenRequest(req);
    if(haveRequest&&!req.legacy&&(req.flags&robotunnel::RawTcp)){
      if(gatewaySuspended||roboexternal::busy()||WiFi.status()!=WL_CONNECTED||!gatewayTunnelAllowed(req.host,req.port)){gatewayPublishTcpState();return;}
      if(ESP.getFreeHeap()<GatewayRelayReserve||heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<GatewayRelayLargestBlock){gatewayPublishTcpState();RoboLog.println("LEV,PAIR,RELAY,heap_reserve=low");return;}
      memcpy(gatewayRelayHost,req.host,sizeof(gatewayRelayHost));gatewayRelayPort=req.port;gatewayRawMode=true;gatewayConnectGeneration=RoboDual.tcpGeneration;gatewayConnecting=true;
      if(xTaskCreatePinnedToCore(gatewayRelayConnectTask,"rdGatewayRelay",5120,nullptr,1,nullptr,0)!=pdPASS){gatewayConnecting=false;gatewayRawMode=false;gatewayPublishTcpState();}return;
    }
    gatewayPublishTcpState();return; // Legacy C3-terminated TLS was removed; the S3 ends TLS and sends RawTcp.
  }
  if(gatewaySuspended||!RoboDual.link.connected()){if(RoboDual.tcpConnected)gatewayStopTcp();return;}
  if(!RoboDual.tcpConnected)return;
  if(gatewayRawMode){
    uint8_t chunk[512];const size_t n=RoboDual.readTcp(chunk,sizeof(chunk));
    if(n&&gatewayRelay.write(chunk,n)!=n){gatewayStopTcp();return;}
    if(gatewayReceiveOffset==gatewayReceiveSize){gatewayReceiveSize=gatewayReceiveOffset=0;const int a=gatewayRelay.available();if(a>0){const int got=gatewayRelay.read(gatewayReceive,size_t(a)<sizeof(gatewayReceive)?size_t(a):sizeof(gatewayReceive));if(got>0)gatewayReceiveSize=size_t(got);}else if(!gatewayRelay.connected()){gatewayStopTcp();return;}}
    if(gatewayReceiveSize>gatewayReceiveOffset)gatewayReceiveOffset+=RoboDual.writeTcp(gatewayReceive+gatewayReceiveOffset,gatewayReceiveSize-gatewayReceiveOffset,1);
    return;
  }
}
inline bool gatewayRemoteVersion(void*,uint32_t*v){const auto reply=gatewayRpc("/_ota/status",HTTP_GET);JsonDocument doc;if(reply.code!=200||deserializeJson(doc,reply.body)||!(doc["bootHealthy"]|false)||(doc["protocol"]|0)!=1)return false;*v=doc["version"]|0u;return *v!=0;}
inline bool gatewayRemoteBegin(void*,uint32_t v,size_t n,const char*sha,const char*sig){JsonDocument doc,args;doc["version"]=v;doc["size"]=n;doc["sha256"]=sha;doc["signature"]=sig;String manifest;serializeJson(doc,manifest);args["manifest"]=manifest;gatewayRemoteOffset=0;return gatewayRpc("/_ota/begin",HTTP_POST,args.as<JsonObjectConst>(),15000).code==200;}
inline size_t gatewayRemoteWrite(void*,const uint8_t*p,size_t n){size_t off=0;while(off<n){uint8_t chunk[773];const size_t k=n-off>768?768:n-off;chunk[0]=1;robolink::put32(chunk+1,gatewayRemoteOffset);memcpy(chunk+5,p+off,k);if(!RoboDual.request(chunk,k+5,15000))return off;const bool ok=RoboDual.messageSize()>=3&&robolink::get16(RoboDual.message())==200;RoboDual.finishRequest();if(!ok)return off;off+=k;gatewayRemoteOffset+=k;}return off;}
inline bool gatewayRemoteFinish(void*){return gatewayRpc("/_ota/finish",HTTP_POST).code==200;}
inline void gatewayRemoteAbort(void*){gatewayRpc("/_ota/abort",HTTP_POST);}
inline const GitHubOtaUpdate::RemoteImage gatewayRemoteImage={"esp32s3-robot","ESP32-S3/robot-link-v1","https://github.com/akh-211/robodesk/releases/latest/download/manifest-s3-robot.txt","RoboDesk-s3-robot.bin",0x300000,nullptr,gatewayRemoteVersion,gatewayRemoteBegin,gatewayRemoteWrite,gatewayRemoteFinish,gatewayRemoteAbort};
inline GitHubOtaUpdate::Preparation gatewayPrepareOta(void*,bool){gatewaySuspended=true;RoboDual.internet=false;if(gatewayBleReady){gatewayBle.end();gatewayBleReady=false;RoboLog.println("LEV,PAIR,BLE,stopped_for_ota=1");}if(gatewayConnecting||roboexternal::busy())return GitHubOtaUpdate::Preparation::Pending;gatewayStopTcp();return GitHubOtaUpdate::Preparation::Ready;}
inline bool gatewayOtaControl(void*,bool start){if(start){gatewaySuspended=true;return !gatewayConnecting&&RoboDual.link.connected();}return true;}
inline void gatewayUpdateFailed(const char*reason){gatewayUpdate=GatewayUpdatePhase::Failed;gatewayUpdateError=reason;gatewaySuspended=false;}
inline void gatewayBleOtaSave(uint8_t state,uint8_t error,uint32_t target){uint8_t v[6]={state,error};robolink::put32(v+2,target);memcpy(gatewayBleOtaLast,v,6);Preferences p;if(!p.begin("robodesk_pair",false))return;p.putBytes("ota_ble",v,6);p.end();}
// A BLE-started C3 install is marked first: the C3 restarts into the new image
// and gatewayServiceBleOta() resolves the marker against the running version.
inline GitHubOtaUpdate::StartResult gatewayInstallGateway(){if(gatewayBleOtaAuto)gatewayBleOtaSave(PhoneSettingsProtocol::OtaDownloadingC3,0,gatewayPairVersion);return gatewayLocalOta.requestInstall();}
inline void gatewayServiceUpdate(){
  gatewayRobotOta.service();gatewayLocalOta.service();const auto r=gatewayRobotOta.snapshot(),g=gatewayLocalOta.snapshot();
  switch(gatewayUpdate){
    case GatewayUpdatePhase::CheckRobot:if(!r.active){if(r.state==GitHubOtaUpdate::Failed)gatewayUpdateFailed(r.failure);else if(r.state==GitHubOtaUpdate::UpToDate||r.state==GitHubOtaUpdate::UpdateAvailable){if(gatewayLocalOta.requestCheck()!=GitHubOtaUpdate::StartResult::Accepted)gatewayUpdateFailed("gateway_check");else gatewayUpdate=GatewayUpdatePhase::CheckGateway;}}break;
    case GatewayUpdatePhase::CheckGateway:if(!g.active){if(g.state==GitHubOtaUpdate::Failed)gatewayUpdateFailed(g.failure);else if(g.state==GitHubOtaUpdate::UpToDate||g.state==GitHubOtaUpdate::UpdateAvailable){if(g.version!=r.version)gatewayUpdateFailed("release_pair_mismatch");else{gatewayPairVersion=g.version;gatewayUpdate=GatewayUpdatePhase::Ready;gatewaySuspended=false;}}}break;
    case GatewayUpdatePhase::InstallRobot:if(!r.active){if(r.state==GitHubOtaUpdate::Failed)gatewayUpdateFailed(r.failure);else if(r.state==GitHubOtaUpdate::Rebooting){gatewayUpdate=GatewayUpdatePhase::WaitRobot;gatewayUpdateAt=millis();}}break;
    case GatewayUpdatePhase::WaitRobot:{uint32_t v=0;if(uint32_t(millis()-gatewaySyncAt)>1000){gatewaySyncAt=millis();if(gatewayRemoteVersion(nullptr,&v)&&v==gatewayPairVersion){if(g.state==GitHubOtaUpdate::UpdateAvailable&&gatewayInstallGateway()==GitHubOtaUpdate::StartResult::Accepted)gatewayUpdate=GatewayUpdatePhase::InstallGateway;else if(g.state==GitHubOtaUpdate::UpToDate){gatewayUpdate=GatewayUpdatePhase::Done;gatewaySuspended=false;}else gatewayUpdateFailed("gateway_install");}}if(uint32_t(millis()-gatewayUpdateAt)>60000)gatewayUpdateFailed("robot_boot_not_confirmed");break;}
    case GatewayUpdatePhase::InstallGateway:if(!g.active&&g.state==GitHubOtaUpdate::Failed)gatewayUpdateFailed(g.failure);break;
    default:break;
  }
}
inline bool gatewayUpdateBusy(){return gatewayUpdate==GatewayUpdatePhase::CheckRobot||gatewayUpdate==GatewayUpdatePhase::CheckGateway||gatewayUpdate==GatewayUpdatePhase::InstallRobot||gatewayUpdate==GatewayUpdatePhase::WaitRobot||gatewayUpdate==GatewayUpdatePhase::InstallGateway;}
// End of a BLE-started run: persist the result, then restart the C3 so BLE
// (stopped by gatewayPrepareOta for the TLS work) comes back.
inline void gatewayBleOtaFinish(uint8_t state,uint8_t error){gatewayBleOtaAuto=false;gatewayBleOtaSave(state,error,gatewayPairVersion);gatewaySuspended=false;gatewayRebootAt=millis()+2000;RoboLog.printf("LEV,OTA,BLE,result=%u,error=%u\n",unsigned(state),unsigned(error));}
inline void gatewayServiceBleOta(){
  namespace ps=PhoneSettingsProtocol;const uint32_t now=millis();static bool loaded=false;
  if(!loaded&&now>10000){loaded=true;Preferences p;uint8_t v[6]{};if(p.begin("robodesk_pair",false)){if(p.getBytes("ota_ble",v,6)!=6)v[0]=0;p.end();}
    if(v[0]==ps::OtaDownloadingC3){uint32_t running=0;RoboSignedImage::version(&running);const uint32_t target=robolink::get32(v+2);
      if(running==target)gatewayBleOtaSave(ps::OtaDone,0,target);else gatewayBleOtaSave(ps::OtaFailed,ps::OtaErrGatewayNotUpdated,target);}
    else if(!gatewayBleOtaLast[0]&&!gatewayBleOtaStart.pending()&&!gatewayBleOtaAuto)memcpy(gatewayBleOtaLast,v,6);}
  if(gatewayBleOtaStart.due(now,gatewayBle.settingsExchange().state()==ps::Exchange::Idle)){
    gatewayUpdateError="";gatewayPairVersion=0;
    if(gatewayUpdateBusy()||!RoboDual.link.connected()||gatewayRobotOta.requestCheck()!=GitHubOtaUpdate::StartResult::Accepted){gatewayBleOtaFinish(ps::OtaFailed,RoboDual.link.connected()?ps::OtaErrRobotBusy:ps::OtaErrRobotUnavailable);return;}
    gatewayUpdate=GatewayUpdatePhase::CheckRobot;gatewayBleOtaAuto=true;RoboLog.println("LEV,OTA,BLE,started=1");return;}
  if(!gatewayBleOtaAuto)return;
  if(gatewayUpdate==GatewayUpdatePhase::Ready){gatewaySuspended=true;
    if(gatewayRobotOta.snapshot().state==GitHubOtaUpdate::UpdateAvailable){if(gatewayRobotOta.requestInstall()==GitHubOtaUpdate::StartResult::Accepted)gatewayUpdate=GatewayUpdatePhase::InstallRobot;else gatewayUpdateFailed("robot_busy");}
    else if(gatewayLocalOta.snapshot().state==GitHubOtaUpdate::UpdateAvailable){if(gatewayInstallGateway()==GitHubOtaUpdate::StartResult::Accepted)gatewayUpdate=GatewayUpdatePhase::InstallGateway;else gatewayUpdateFailed("gateway_install");}
    else{gatewayUpdate=GatewayUpdatePhase::Idle;gatewayBleOtaFinish(ps::OtaUpToDate,0);}}
  else if(gatewayUpdate==GatewayUpdatePhase::Done){gatewayUpdate=GatewayUpdatePhase::Idle;gatewayBleOtaFinish(ps::OtaDone,0);}
  else if(gatewayUpdate==GatewayUpdatePhase::Failed)gatewayBleOtaFinish(ps::OtaFailed,ps::otaErrorCode(gatewayUpdateError.c_str()));
}
// Main loop only: live pair-flow phase, else the last persisted BLE result.
inline void gatewayOtaStatus(uint8_t out[7]){
  namespace ps=PhoneSettingsProtocol;uint8_t state=ps::OtaIdle,error=0,percent=0;uint32_t target=gatewayPairVersion;
  switch(gatewayUpdate){
    case GatewayUpdatePhase::CheckRobot:case GatewayUpdatePhase::CheckGateway:state=ps::OtaChecking;break;
    case GatewayUpdatePhase::InstallRobot:{const auto r=gatewayRobotOta.snapshot();state=strcmp(r.stage,"image")?ps::OtaDownloadingS3:ps::OtaFlashingS3;percent=gatewayRobotOta.percent();if(r.version)target=r.version;break;}
    case GatewayUpdatePhase::WaitRobot:state=ps::OtaWaitingS3Boot;percent=100;break;
    case GatewayUpdatePhase::InstallGateway:state=ps::OtaDownloadingC3;percent=gatewayLocalOta.percent();break;
    case GatewayUpdatePhase::Done:state=ps::OtaDone;break;
    case GatewayUpdatePhase::Failed:state=ps::OtaFailed;error=ps::otaErrorCode(gatewayUpdateError.c_str());break;
    default:if(gatewayBleOtaStart.pending())state=ps::OtaChecking;else if(gatewayBleOtaLast[0]){state=gatewayBleOtaLast[0];error=gatewayBleOtaLast[1];target=robolink::get32(gatewayBleOtaLast+2);}break;
  }
  out[0]=state;out[1]=error;out[2]=percent;robolink::put32(out+3,target);
}
#if !ROBODESK_NO_DASHBOARD
inline void gatewayConfigureUpdateRoutes(){auto&s=gatewayDashboard.server();
  s.on("/api/ota",HTTP_GET,[](){auto&s=gatewayDashboard.server();if(!s.authenticate("admin",gatewaySettings.adminPin)){s.requestAuthentication();return;}const auto r=gatewayRobotOta.snapshot(),g=gatewayLocalOta.snapshot();JsonDocument doc;const bool available=r.state==GitHubOtaUpdate::UpdateAvailable||g.state==GitHubOtaUpdate::UpdateAvailable;doc["active"]=gatewayUpdateBusy();doc["state"]=gatewayUpdate==GatewayUpdatePhase::Failed?int(GitHubOtaUpdate::Failed):(gatewayUpdateBusy()?int(GitHubOtaUpdate::Downloading):(available?int(GitHubOtaUpdate::UpdateAvailable):int(GitHubOtaUpdate::UpToDate)));doc["version"]=gatewayPairVersion;doc["failure"]=gatewayUpdateError;doc["stage"]=int(gatewayUpdate);doc["message"]=gatewayUpdateError.length()?gatewayUpdateError:(gatewayUpdateBusy()?String("Updating robot/gateway pair"):String("Signed update for S3 robot and C3 gateway"));doc["robot"]["state"]=int(r.state);doc["robot"]["version"]=r.version;doc["robot"]["stage"]=r.stage;doc["robot"]["failure"]=r.failure;doc["robot"]["written"]=gatewayRemoteOffset;doc["gateway"]["state"]=int(g.state);doc["gateway"]["version"]=g.version;doc["gateway"]["stage"]=g.stage;doc["gateway"]["failure"]=g.failure;String out;serializeJson(doc,out);s.send(200,"application/json",out);});
  s.on("/ota/s3",HTTP_POST,[](){auto&s=gatewayDashboard.server();if(!s.authenticate("admin",gatewaySettings.adminPin)){s.requestAuthentication();return;}if(!roboSameOriginHttp(s.header("Host").c_str(),s.header("Origin").c_str())){s.send(403,"text/plain","Origin rejected");return;}const bool ok=gatewayLocalS3Ok;gatewayLocalS3Active=false;gatewaySuspended=false;s.send(ok?200:(gatewayLocalS3Code?gatewayLocalS3Code:500),"text/plain",ok?"S3 image verified; robot is rebooting":gatewayLocalS3Error);},[](){auto&s=gatewayDashboard.server();auto&u=s.upload();auto fail=[&](uint16_t c,const char*m){if(gatewayLocalS3Active)gatewayRemoteAbort(nullptr);gatewayLocalS3Active=false;gatewayLocalS3Ok=false;gatewayLocalS3Code=c;gatewayLocalS3Error=m;};
    if(u.status==UPLOAD_FILE_START){gatewayLocalS3Ok=false;gatewayLocalS3Active=false;gatewayLocalS3Code=0;gatewayLocalS3Error="";
      if(!s.authenticate("admin",gatewaySettings.adminPin)){fail(401,"Authentication required");return;}
      if(!roboSameOriginHttp(s.header("Host").c_str(),s.header("Origin").c_str())){fail(403,"Origin rejected");return;}
      if(gatewayUpdateBusy()||!RoboDual.link.connected()){fail(503,"Robot unavailable or update busy");return;}
      const uint32_t v=strtoul(s.arg("version").c_str(),nullptr,10),n=strtoul(s.arg("size").c_str(),nullptr,10);const String sha=s.arg("sha256"),sig=s.arg("sig");
      if(!v||!n||n>0x300000||sha.length()!=64||sig.length()<128||sig.length()>160){fail(400,"Missing or malformed version/size/sha256/sig");return;}
      if(gatewayPrepareOta(nullptr,true)!=GitHubOtaUpdate::Preparation::Ready){fail(503,"Gateway busy, retry");return;}
      gatewayLocalS3Active=true;if(!gatewayRemoteBegin(nullptr,v,n,sha.c_str(),sig.c_str())){fail(400,"Robot rejected image header (version/signature)");return;}gatewayLocalS3Ok=true;
    }else if(u.status==UPLOAD_FILE_WRITE){if(!gatewayLocalS3Active||!gatewayLocalS3Ok)return;if(gatewayRemoteWrite(nullptr,u.buf,u.currentSize)!=u.currentSize)fail(502,"UART transfer to robot failed");}
    else if(u.status==UPLOAD_FILE_END){if(!gatewayLocalS3Active||!gatewayLocalS3Ok)return;if(!gatewayRemoteFinish(nullptr))fail(400,"Robot rejected image (size/hash)");else gatewayLocalS3Active=false;}
    else if(u.status==UPLOAD_FILE_ABORTED)fail(400,"Upload interrupted");});
  const char*paths[]={"/ota/github/check","/ota/github/install"};for(const char*p:paths)s.on(p,HTTP_POST,[](){auto&s=gatewayDashboard.server();if(!s.authenticate("admin",gatewaySettings.adminPin)){s.requestAuthentication();return;}if(!roboSameOriginHttp(s.header("Host").c_str(),s.header("Origin").c_str())){s.send(403,"text/plain","Origin rejected");return;}if(gatewayUpdateBusy()||!RoboDual.link.connected()){s.send(503,"text/plain","Robot unavailable or update busy");return;}gatewayUpdateError="";bool ok=false;
    if(s.uri()=="/ota/github/check"){ok=gatewayRobotOta.requestCheck()==GitHubOtaUpdate::StartResult::Accepted;if(ok)gatewayUpdate=GatewayUpdatePhase::CheckRobot;}
    else if(gatewayUpdate==GatewayUpdatePhase::Ready){gatewaySuspended=true;if(gatewayRobotOta.snapshot().state==GitHubOtaUpdate::UpdateAvailable){ok=gatewayRobotOta.requestInstall()==GitHubOtaUpdate::StartResult::Accepted;if(ok)gatewayUpdate=GatewayUpdatePhase::InstallRobot;}else{ok=gatewayLocalOta.requestInstall()==GitHubOtaUpdate::StartResult::Accepted;if(ok)gatewayUpdate=GatewayUpdatePhase::InstallGateway;}}
    s.send(ok?202:409,"application/json",ok?"{\"accepted\":true}":"{\"accepted\":false}");
  });
}
#endif
inline bool gatewayStartSetupApThrottled(){if(!gatewayWifiReady)return false;if(gatewayDashboard.apStarted())return true;const uint32_t now=millis();if(gatewaySetupApAttempted&&uint32_t(now-gatewaySetupApAttemptAt)<5000u)return false;gatewaySetupApAttempted=true;gatewaySetupApAttemptAt=now;return gatewayDashboard.startSetupAp();}
inline void gatewayServiceSetupApRecovery(uint32_t now){static uint32_t stableSince=0,lastCheck=0;if(!gatewayDashboard.apStarted()){stableSince=0;return;}if(WiFi.status()!=WL_CONNECTED||!RoboDual.link.connected()){stableSince=0;return;}if(!stableSince)stableSince=now;if(uint32_t(now-stableSince)<30000u||uint32_t(now-lastCheck)<5000u)return;lastCheck=now;if(gatewayDashboard.stopSetupApIfUnused()){gatewaySetupApAttempted=false;RoboLog.println("LEV,CFG,AP,RELEASED,reason=station-connected");}}
inline void gatewayBeginWifi(uint8_t first){gatewayWifiAt=millis();if(!gatewayWifiReady)return;int8_t i=gatewaySettings.findWifiNetworkIndex(first);if(i<0){gatewayStartSetupApThrottled();return;}gatewayWifiIndex=uint8_t(i);WiFi.disconnect(false,false);WiFi.begin(gatewaySettings.wifiSsidAt(i),gatewaySettings.wifiPasswordAt(i));}
inline void gatewayBootHealth(){
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
  static bool complete=false;if(complete||millis()<8000)return;const esp_partition_t*run=esp_ota_get_running_partition();esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
  if(run&&esp_ota_get_state_partition(run,&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY){if(ESP.getFreeHeap()<24576||esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK){esp_ota_mark_app_invalid_rollback_and_reboot();return;}}
  Preferences p;if(!run||!p.begin("robodesk_ota",false))return;uint32_t staged=p.getUInt("pending",0);if(staged&&p.getUInt("pend_off",0xffffffffu)==run->address){if(p.putUInt("version",staged)!=4){p.end();return;}}p.remove("pending");p.remove("pend_off");if(!p.getUInt("version",0))p.putUInt("version",ROBODESK_OTA_INITIAL_VERSION);p.end();complete=true;
#endif
}
inline void gatewayServicePairingDisplay(){
  uint32_t windowId=0,code=0;uint16_t ttlMs=0;
  if(gatewayBleReady&&RoboDual.link.connected()&&gatewayBle.nextPairingPasskey(windowId,code,ttlMs)){
    uint8_t payload[10];robolink::put32(payload,windowId);robolink::put32(payload+4,code);robolink::put16(payload+8,ttlMs);
    if(!RoboDual.link.send(robolink::PairingPasskey,payload,sizeof(payload),robolink::Reliable,10))gatewayBle.pairingPasskeySendFailed(windowId);
  }
  uint32_t displayed=0;if(RoboDual.takePairingPasskeyPresented(displayed))gatewayBle.pairingPasskeyDisplayed(displayed);
}
// Tell the S3 screen whether the C3 BLE pairing window is really open (sent from the loop, never from BLE callbacks).
// Open is refreshed every 5 s with the remaining time so an S3 reboot or link drop still shows the window.
inline void gatewayServicePairingState(uint32_t now){
  static uint8_t pending=0;static uint32_t lastOpenSend=0;
  uint8_t st=0;if(gatewayBleReady&&gatewayBle.takePairingState(st))pending=st;
  if(!gatewayBleReady||!RoboDual.link.connected())return;
  if(!pending&&gatewayBle.pairingOpen(now)&&uint32_t(now-lastOpenSend)>=5000u)pending=robolink::PairOpen;
  if(!pending)return;
  uint16_t ttl=pending==robolink::PairOpen?gatewayBle.pairingRemainingMs(now):0;
  if(pending==robolink::PairOpen&&!ttl){pending=0;return;}
  uint8_t payload[3];robolink::encodePairingState(payload,pending,ttl);
  if(RoboDual.link.send(robolink::PairingState,payload,sizeof(payload),robolink::Reliable,10)){if(pending==robolink::PairOpen)lastOpenSend=now?now:1;RoboLog.printf("LEV,PAIR,STATE_TX,state=%u,ttl=%u\n",unsigned(pending),unsigned(ttl));pending=0;}
}
// Dashboard-free pairing and factory reset. Pairing opens on the S3 head+side hold (PairingRequest frame)
// or the USB serial line "pair"; without the dashboard the passkey shown on the S3 screen is the physical
// proof, so an authenticated bonded candidate is approved automatically. "factory_reset" then "confirm"
// within 10 s queues the persisted two-board reset (same path as the BLE command).
inline int gatewayConsoleRead(){
#if CONFIG_IDF_TARGET_ESP32C3 && !ARDUINO_USB_CDC_ON_BOOT
  if(usb_serial_jtag_ll_rxfifo_data_available()){uint8_t b=0;if(usb_serial_jtag_ll_read_rxfifo(&b,1)==1)return b;}
#endif
  return Serial.available()?Serial.read():-1;
}
inline void gatewayServiceConsole(uint32_t now){
  static char line[24];static uint8_t length=0;static bool overflow=false;static uint32_t factoryArmedAt=0;
  for(uint8_t i=0;i<32;++i){const int c=gatewayConsoleRead();if(c<0)break;if(c=='\r')continue;
    if(c!='\n'){if(length+1<sizeof(line))line[length++]=char(c);else overflow=true;continue;}
    line[length]=0;const bool valid=!overflow;length=0;overflow=false;if(!valid)continue;
#if !ROBODESK_PHONE_BLE_ROBOT
    if(!strcmp(line,"pair")){const bool ok=gatewayBleReady&&gatewayOpenPairing();RoboLog.printf("LEV,PAIR,OPEN,source=serial,ok=%u\n",unsigned(ok));}else
#endif
    if(!strcmp(line,"factory_reset")){factoryArmedAt=now?now:1;RoboLog.println("LEV,FACTORY,ARMED,type confirm within 10s");}
    else if(!strcmp(line,"confirm")&&factoryArmedAt&&uint32_t(now-factoryArmedAt)<10000u){factoryArmedAt=0;
      const bool ok=!gatewayUpdateBusy()&&(gatewayFactoryState||gatewaySetFactoryState(1));if(ok)gatewayFactoryRetryAt=now;RoboLog.printf("LEV,FACTORY,QUEUED,ok=%u\n",unsigned(ok));}
  }
}
inline void gatewayServicePhysicalPairing(){
#if !ROBODESK_PHONE_BLE_ROBOT
  if(RoboDual.pairingRequested.exchange(false)){const bool ok=gatewayBleReady&&gatewayOpenPairing();gatewayPairingPhysical=ok;RoboLog.printf("LEV,PAIR,OPEN,source=s3-touch,ok=%u\n",unsigned(ok));}
  if(gatewayPairingPhysical&&gatewayBleReady&&!gatewayBle.pairingOpen(millis()))gatewayPairingPhysical=false;
  static uint32_t lastAttempt=0;PhoneBleTransport::CandidateApproval candidate{}; // prepare persists a fresh key; at most one try per second.
  if((ROBODESK_NO_DASHBOARD||gatewayPairingPhysical)&&gatewayBleReady&&uint32_t(millis()-lastAttempt)>=1000u&&gatewayBle.snapshotCandidateApproval(candidate)){lastAttempt=millis();const bool ok=gatewayApproveCandidate();RoboLog.printf("LEV,PAIR,APPROVE,source=auto,ok=%u\n",unsigned(ok));}
#endif
}
#if !ROBODESK_PHONE_BLE_ROBOT
// BLE allocates ~50 KB; setup() starts it right after Wi-Fi init so dashboard/RPC boot allocations cannot fragment the heap first (device 2026-10-08).
// The deadline keeps BLE reachable when S3 never connects. Production guards: 96 KiB heap / 80 KiB largest block.
inline constexpr uint32_t GatewayBleStartDeadlineMs=60000u;
inline bool gatewayBleStartAttempted=false;
inline void gatewayStartPhoneBle(uint32_t now){
  if(gatewayBleStartAttempted||(!gatewayPeer&&now<GatewayBleStartDeadlineMs))return;
  gatewayBleStartAttempted=true;
  if(!gatewayWifiReady)gatewayBle.startupBlocked("wifi-unavailable");
  else if(ESP.getFreeHeap()<GatewayBleStartupReserve)gatewayBle.startupBlocked("startup-heap-reserve");
  else if(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<GatewayBleStartupLargestBlock)gatewayBle.startupBlocked("startup-largest-block");
  else gatewayBleReady=gatewayBle.begin(&gatewayNotifications,&gatewaySettings,&gatewayNavigation);
  RoboLog.printf("LEV,BLE,INIT,ready=%u,stage=%s,fail=%s,platform=%u,peer=%u\n",unsigned(gatewayBleReady),gatewayBle.startupStage(),gatewayBle.startupFailureReason(),unsigned(gatewaySettings.phonePlatform),unsigned(gatewaySettings.phoneBlePeer[0]!=0));
  esp_rom_printf("[RoboDeskBoot] ble-init=%u heap=%u largest=%u\n",unsigned(gatewayBleReady),unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
}
#endif
void setup(){Serial.begin(115200);
#if defined(ROBODESK_BLE_DIAGNOSTIC) && ROBODESK_BLE_DIAGNOSTIC
  esp_rom_install_channel_putc(1,gatewayDiagnosticUsbPutc);
  esp_rom_printf("[RoboDeskBoot] diagnostic-usb-console=1\n");
#endif
  RoboLog.begin();RuntimeSettings defaults;RuntimeSettings::copy(defaults.wifiSsid,sizeof(defaults.wifiSsid),WIFI_SSID);RuntimeSettings::copy(defaults.wifiPassword,sizeof(defaults.wifiPassword),WIFI_PASSWORD);RuntimeSettings::copy(defaults.geminiApiKey,sizeof(defaults.geminiApiKey),GEMINI_API_KEY);
  if(!gatewayStore.load(gatewaySettings,defaults))RoboLog.println("LEV,PAIR,GATEWAY,settings=defaults");
#if ROBODESK_PHONE_BLE_ROBOT
  gatewayBleOwnerSafe=gatewayClearLocalPhoneCredential();
#endif
  {Preferences p;if(p.begin("robodesk_pair",true)){gatewayFactoryState=p.getUChar("factory",0);p.end();}}
  esp_rom_printf("[RoboDeskBoot] before-wifi heap=%u largest=%u\n",unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
  gatewayWifiReady=WiFi.mode(WIFI_STA);
  if(!gatewayWifiReady){WiFi.mode(WIFI_OFF);delay(20);gatewayWifiReady=WiFi.mode(WIFI_AP_STA);}
  esp_rom_printf("[RoboDeskBoot] wifi-init=%u heap=%u largest=%u\n",unsigned(gatewayWifiReady),unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
  if(gatewayWifiReady){WiFi.setSleep(true);WiFi.setTxPower(WIFI_POWER_8_5dBm);WiFi.setAutoReconnect(false);WiFi.onEvent([](arduino_event_id_t,arduino_event_info_t info){esp_rom_printf("[RoboDeskWifi] disconnected reason=%u rssi=%d\n",unsigned(info.wifi_sta_disconnected.reason),int(info.wifi_sta_disconnected.rssi));},ARDUINO_EVENT_WIFI_STA_DISCONNECTED);}
#if !ROBODESK_PHONE_BLE_ROBOT
  gatewayBle.setCompanionCommandDispatch(gatewayDispatchPhoneCommand);gatewayBle.setRemoteRevokePersist(gatewayPhonePersistRemoteRevoke);gatewayBle.setSettingsDispatch(gatewayDispatchSettings);
#endif
  gatewayStartPhoneBle(GatewayBleStartDeadlineMs); // Start BLE while heap is still unfragmented (131 KB / 114 KB block after Wi-Fi), before dashboard/RPC allocate.
#if !ROBODESK_NO_DASHBOARD
  gatewayDashboard.begin(&gatewaySettings,&gatewayStore,nullptr,SETUP_AP_PASSWORD,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&gatewayNotifications);
  gatewayBeginWifi(0);gatewayDashboard.startHttp();
#else
  gatewayBeginWifi(0);
#endif
  if(!RoboDual.begin()){RoboLog.println("LEV,PAIR,FATAL,link_allocation");return;}
  roboexternal::begin();roboexternal::remoteFetch=gatewayExternalFetch;
#if !ROBODESK_NO_DASHBOARD
  roboexternal::configureDashboardRoutes(gatewayDashboard.server(),&gatewaySettings);
  gatewayDashboard.setRobotProxy(gatewayProxy,nullptr);gatewayDashboard.setRemoteSave(gatewaySave,nullptr);gatewayDashboard.setPhonePlatformChange(gatewayPhonePlatformChanged,nullptr);
#endif
  gatewayLocalOta.begin(gatewayOtaControl,nullptr,gatewayPrepareOta);gatewayRobotOta.begin(gatewayOtaControl,nullptr,gatewayPrepareOta);gatewayRobotOta.setRemoteImage(&gatewayRemoteImage);
#if !ROBODESK_NO_DASHBOARD
  gatewayConfigureUpdateRoutes();
#endif
#if !ROBODESK_PHONE_BLE_ROBOT
  gatewayPhoneCommandQueue=xQueueCreate(1,sizeof(GatewayPhoneCommand));if(gatewayPhoneCommandQueue&&xTaskCreatePinnedToCore(gatewayPhoneCommandWorker,"rdPhoneCmd",4096,nullptr,1,&gatewayPhoneCommandTask,0)!=pdPASS){vQueueDelete(gatewayPhoneCommandQueue);gatewayPhoneCommandQueue=nullptr;}
#endif
  enableLoopWDT();RoboLog.println("LEV,PAIR,GATEWAY,uart=921600,tx=4,rx=5,peripherals=none");
}
void loop(){RoboDual.service();gatewayTraceMin("service");gatewayDashboard.service(millis());gatewayTraceMin("dashboard");
#if !ROBODESK_PHONE_BLE_ROBOT
if(!gatewayBleStartAttempted)gatewayStartPhoneBle(millis());
#endif
if(gatewayBleReady)gatewayBle.service(millis());gatewayTraceMin("ble");gatewayServicePairingDisplay();gatewayServicePairingState(millis());gatewayServicePhysicalPairing();gatewayServiceConsole(millis());
  gatewayBootHealth();
  gatewayLogLinkDiagnostics(millis());
  if(gatewayFactoryState){if(uint32_t(millis()-gatewayFactoryRetryAt)>=5000){gatewayFactoryRetryAt=millis();gatewayCompleteFactory();}delay(1);return;}
  if(gatewayRebootAt&&int32_t(millis()-gatewayRebootAt)>=0)ESP.restart();
  gatewayTraceMin("loop-pre");gatewayServiceKey();gatewayTraceMin("key");gatewayServiceTcp();gatewayTraceMin("tcp");gatewayServiceUpdate();gatewayServiceBleOta();gatewayTraceMin("update");
  const bool externalSafe=WiFi.status()==WL_CONNECTED&&!gatewaySuspended&&!gatewayUpdateBusy()&&!gatewayConnecting&&!RoboDual.tcpConnected;
  roboexternal::service(millis(),externalSafe);
  const bool wifi=WiFi.status()==WL_CONNECTED;if(wifi&&!gatewayWifiPrevious)configTime(long(gatewaySettings.timezoneOffsetMin)*60,0,"pool.ntp.org","time.google.com");gatewayWifiPrevious=wifi;{static uint32_t ntpAt=0;if(wifi&&time(nullptr)<1700000000&&uint32_t(millis()-ntpAt)>=15000){ntpAt=millis();configTime(long(gatewaySettings.timezoneOffsetMin)*60,0,"pool.ntp.org","time.google.com");IPAddress ntpIp;const bool ntpDns=WiFi.hostByName("pool.ntp.org",ntpIp)==1;RoboLog.printf("LEV,CLOCK,NTP_RETRY,rssi=%d,dns=%d,ip=%s,gw=%s,dnsSrv=%s,sync=%d\n",int(WiFi.RSSI()),int(ntpDns),ntpIp.toString().c_str(),WiFi.gatewayIP().toString().c_str(),WiFi.dnsIP().toString().c_str(),int(esp_sntp_get_sync_status()));}}gatewayServiceSetupApRecovery(millis());
  static uint8_t wifiRounds=0;if(wifi||!gatewayWifiAt)wifiRounds=0;if(!wifi&&uint32_t(millis()-gatewayWifiAt)>(wifiRounds<5?12000u:wifiRounds<10?30000u:60000u)){if(wifiRounds<255)++wifiRounds;gatewayBeginWifi(uint8_t((gatewayWifiIndex+1)%3));}if(!wifi&&millis()>15000)gatewayStartSetupApThrottled();
  if(uint32_t(millis()-gatewayClockAt)>=1000){gatewayClockAt=millis();uint8_t clock[10]{};robolink::put32(clock,uint32_t(time(nullptr)));clock[4]=wifi&&!gatewaySuspended;clock[5]=gatewayValidKey()&&!gatewaySuspended;clock[6]=uint8_t((gatewayBleReady?1:0)|(gatewayBle.connected()?2:0)|(gatewayBle.pairingOpen(millis())?4:0)|((ROBODESK_PHONE_BLE_ROBOT&&gatewayBleOwnerSafe)?8:0));RoboDual.link.send(robolink::Clock,clock,sizeof(clock));}
  if(!roboexternal::busy()&&wifi&&!gatewayUpdateBusy()&&RoboDual.link.connected()&&!RoboDual.tcpConnected&&uint32_t(millis()-gatewayExternalContextAt)>=30000u&&gatewayHeapAllowsOptional()){char weather[180]{},homeAssistant[400]{},calendar[240]{};const uint32_t now=millis();roboexternal::contextText(weather,sizeof(weather),now,"weather");roboexternal::contextText(homeAssistant,sizeof(homeAssistant),now,"home_assistant");roboexternal::contextText(calendar,sizeof(calendar),now,"calendar");JsonDocument args;args["weather"]=weather;args["home_assistant"]=homeAssistant;args["calendar"]=calendar;gatewayExternalContextAt=millis();gatewayRpc("/_external/context",HTTP_POST,args.as<JsonObjectConst>(),1000);}
  const uint32_t peer=RoboDual.link.health().peerSession;if(RoboDual.link.connected()&&!gatewayUpdateBusy()&&(gatewayForceSettingsSync||peer!=gatewayPeer||(uint32_t(millis()-gatewaySyncAt)>30000u&&gatewayHeapAllowsOptional()))){gatewaySyncAt=millis();if(peer!=gatewayPeer)gatewayMigrationComplete=false;if(!gatewayMigrationComplete)gatewayMigrationComplete=gatewayMigrateCredentials();if(gatewaySyncSettings(gatewayForceSettingsSync)){gatewayPeer=peer;gatewayForceSettingsSync=false;}}
  static uint32_t forwarded[PhoneNotificationBridge::Capacity]{};
  static char forwardedKeys[PhoneNotificationBridge::Capacity][33]{},forwardedApps[PhoneNotificationBridge::Capacity][64]{};
  if(!gatewayUpdateBusy()&&RoboDual.link.connected()){
    const uint32_t session=RoboDual.link.health().peerSession;
    if(session!=gatewayPhonePeer){gatewayNavigationSupported=false;gatewayNavRevision=0;memset(forwarded,0,sizeof(forwarded));
      if(uint32_t(millis()-gatewayPhoneProbeAt)>=5000u&&gatewayHeapAllowsOptional()){gatewayPhoneProbeAt=millis();auto r=gatewayRpc("/_phone/capabilities",HTTP_GET,JsonObjectConst(),1000);if(r.code==200){JsonDocument d;if(!deserializeJson(d,r.body)){gatewayNavigationSupported=d["navigationV1"]==true;gatewayPhonePeer=session;}}else if(r.code==404)gatewayPhonePeer=session;}}
    const auto&nav=gatewayNavigation.current();
    if(gatewayNavigationSupported&&nav.revision&&nav.revision!=gatewayNavRevision&&gatewayNavigation.age(millis())<PhoneNavigationBridge::ExpireMs){char wire[PhoneNavigationBridge::WireMax+1],distance[16];if(nav.distanceKnown)snprintf(distance,sizeof(distance),"%lu",(unsigned long)nav.distanceMeters);else strcpy(distance,"?");snprintf(wire,sizeof(wire),"NAV1\n%s\n%s\n%s\n%s\n%s",nav.state,nav.turn,distance,nav.road,nav.eta);JsonDocument a;a["wire"]=wire;a["ageMs"]=gatewayNavigation.age(millis());auto r=gatewayRpc("/_phone/navigation",HTTP_POST,a.as<JsonObjectConst>(),700);if(r.code==200||r.code==409)gatewayNavRevision=nav.revision;}
    for(unsigned i=0;i<PhoneNotificationBridge::Capacity;++i){const auto&n=gatewayNotifications.at(i);char key[33]{};if(n.id){if(n.key[0])strcpy(key,n.key);else snprintf(key,sizeof(key),"legacy-%lu",(unsigned long)n.id);}
      if(forwardedKeys[i][0]&&(!n.id||strcmp(key,forwardedKeys[i])||strcmp(n.appId,forwardedApps[i]))){JsonDocument a;a["op"]="remove";a["key"]=forwardedKeys[i];a["appId"]=forwardedApps[i];if(gatewayRpc("/_phone/push",HTTP_POST,a.as<JsonObjectConst>(),700).code==200){forwardedKeys[i][0]=0;forwarded[i]=0;}break;}
      if(n.id&&forwarded[i]!=n.id){JsonDocument a;a["appId"]=n.appId;a["app"]=n.app;a["title"]=n.title;a["snippet"]=n.snippet;a["key"]=key;a["ageMs"]=uint32_t(millis()-n.receivedAt);const auto r=gatewayRpc("/_phone/push",HTTP_POST,a.as<JsonObjectConst>(),700);if(r.code==200||r.code==409){forwarded[i]=n.id;strcpy(forwardedKeys[i],key);strcpy(forwardedApps[i],n.appId);}break;}}
  }

  if(gatewayRebootAt&&int32_t(millis()-gatewayRebootAt)>=0)ESP.restart();
  delay(1);
}
