#pragma once

#include "PhoneNotificationBridge.h"
#include "PhoneNavigationBridge.h"
#include "PhoneBridgeProtocol.h"
#include "PhoneBridgeAuthRecord.h"
#include "PhoneCompanionCommand.h"
#include "RuntimeSettings.h"

#include "RoboBuildRole.h"
#if (ROBODESK_DUAL_GATEWAY && !ROBODESK_PHONE_BLE_ROBOT) || (ROBODESK_DUAL_ROBOT && ROBODESK_PHONE_BLE_ROBOT) || (!ROBODESK_DUAL_BOARD && __has_include(<BLEDevice.h>))
#include "PhoneBleBackend.h"
#if defined(CONFIG_NIMBLE_ENABLED) && !defined(ROBODESK_NIMBLE_EXTERNAL)
#include <host/ble_hs.h>
#endif
#include <strings.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#include <new>
#include "PhoneAncsClient.h"
#define ROBODESK_PHONE_BLE_AVAILABLE 1

class PhoneBleTransport {
 public:
  using CandidateApproval=PhoneBridgeProtocol::CandidateLease;
  using RemoteRevokePersist=bool(*)(void*);
  using CompanionCommandDispatch=bool(*)(uint32_t,uint32_t,uint32_t,const uint8_t*,size_t);
  static constexpr const char* ServiceUuid="93de0001-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* WriteUuid="93de0002-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* AllowlistUuid="93de0003-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* NavigationUuid="93de0004-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* ConfigUuid="93de0005-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* NotificationV2Uuid="93de0006-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* IdentityV2Uuid="93de0007-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* ControlV2Uuid="93de0008-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* EnvelopeV2Uuid="93de0009-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* AckV2Uuid="93de000a-2c7d-4a52-9f1c-6f4b4f424c45";
  const char* startupStage()const{return startupStage_;}
  const char* startupFailureReason()const{return startupFailure_;}
  uint32_t hostStackMinimumFree()const{
#if INCLUDE_uxTaskGetStackHighWaterMark
    return hostTask_?uint32_t(uxTaskGetStackHighWaterMark(hostTask_)):0;
#else
    return 0;
#endif
  }
  void startupBlocked(const char*reason){startupStage_="preflight";startupFailure_=reason;}
  bool begin(PhoneNotificationBridge* bridge,RuntimeSettings* settings,PhoneNavigationBridge* navigation=nullptr){
    startupStage_="auth-storage";startupFailure_="";hostTask_=nullptr;
    phoneBleMemory("before-queues");
    auto initialize=[&](){
    bridge_=bridge;settings_=settings;navigation_=navigation;if(!loadAuthRecord_())return startupFailed_("auth-storage");startupStage_="queues";packets_=xQueueCreate(4,sizeof(Packet));companionResults_=xQueueCreate(4,sizeof(CommandResult));if(!packets_||!companionResults_)return startupFailed_("queue-allocation");cacheSettings_();
#if defined(CONFIG_NIMBLE_ENABLED)
    ble_hs_cfg.sm_sc_only=1;
#else
    return startupFailed_("backend-unavailable");
#endif
    startupStage_="crypto-self-test";
    if(!PhoneBridgeProtocol::hmacKnownAnswerSelfTest())return startupFailed_("crypto-self-test");
    startupStage_="host-init";
    #if defined(ROBODESK_NIMBLE_EXTERNAL)
    if(!BLEDevice::init("RoboDesk"))return startupFailed_("host-init");
    BLEDevice::setMTU(247);
    phoneBleMemory("host-init");
// Diagnostic builds may continue below the production reserve to capture
// later allocation stages; use only on a recoverable bench image.
#if !defined(ROBODESK_BLE_DIAGNOSTIC) || !ROBODESK_BLE_DIAGNOSTIC
    if(ESP.getFreeHeap()<InternalStartupFloor)return startupFailed_("heap-reserve");
#endif
#else
    BLEDevice::init("RoboDesk");BLEDevice::setMTU(512);
#endif
    BLEDevice::setSecurityCallbacks(&securityCallbacks_);
    BLESecurity::setAuthenticationMode(true,true,true);
#if defined(CONFIG_NIMBLE_ENABLED)
    ble_hs_cfg.sm_sc_only=1; // Reaffirm policy after host initialization.
#endif
    BLESecurity::setCapability(0); // DisplayOnly in both NimBLE and the Arduino wrapper.
    BLESecurity::setPassKey(true,freshPasskey_());
    startupStage_="service";
    server_=BLEDevice::createServer();if(!server_)return startupFailed_("server-allocation");
    phoneBleServerCallbacks(server_,&serverCallbacks_);
    BLEService* service=server_->createService(ServiceUuid);if(!service)return startupFailed_("service-allocation");
    startupStage_="characteristics";
    auto*identity=service->createCharacteristic(IdentityV2Uuid,PhoneBleProperties::READ|PhoneBleProperties::READ_ENC);if(!identity)return false;phoneBleReadEncrypted(identity);identity->setCallbacks(&identityCallbacks_);
    BLECharacteristic* rx=service->createCharacteristic(WriteUuid,PhoneBleProperties::WRITE|PhoneBleProperties::WRITE_NR|PhoneBleProperties::WRITE_ENC);
    if(!rx)return false;
    phoneBleWriteEncrypted(rx);
    rx->setCallbacks(&rxCallbacks_);
    BLECharacteristic* allowlist=service->createCharacteristic(AllowlistUuid,PhoneBleProperties::READ|PhoneBleProperties::READ_ENC);
    if(!allowlist)return false;
    phoneBleReadEncrypted(allowlist);
    allowlist->setValue(settings_->notificationAllowlist);
    allowlist->setCallbacks(&allowlistCallbacks_);
    for(const char*uuid:{NavigationUuid,NotificationV2Uuid}){auto*c=service->createCharacteristic(uuid,PhoneBleProperties::WRITE|PhoneBleProperties::WRITE_ENC);if(!c)return false;phoneBleWriteEncrypted(c);c->setCallbacks(&rxCallbacks_);}
    auto*config=service->createCharacteristic(ConfigUuid,PhoneBleProperties::READ|PhoneBleProperties::READ_ENC);if(!config)return false;phoneBleReadEncrypted(config);config->setCallbacks(&configCallbacks_);
    controlCharacteristic_=service->createCharacteristic(ControlV2Uuid,PhoneBleProperties::WRITE|PhoneBleProperties::WRITE_ENC|PhoneBleProperties::NOTIFY);if(!controlCharacteristic_)return false;phoneBleWriteEncrypted(controlCharacteristic_);phoneBleCccd(controlCharacteristic_);controlCharacteristic_->setCallbacks(&controlCallbacks_);
    auto*envelope=service->createCharacteristic(EnvelopeV2Uuid,PhoneBleProperties::WRITE|PhoneBleProperties::WRITE_ENC);if(!envelope)return false;phoneBleWriteEncrypted(envelope);envelope->setCallbacks(&envelopeCallbacks_);
    ackCharacteristic_=service->createCharacteristic(AckV2Uuid,PhoneBleProperties::NOTIFY);if(!ackCharacteristic_)return false;phoneBleCccd(ackCharacteristic_);
    phoneBleMemory("characteristics");
#if defined(ROBODESK_NIMBLE_EXTERNAL)
#if !defined(ROBODESK_BLE_DIAGNOSTIC) || !ROBODESK_BLE_DIAGNOSTIC
    if(ESP.getFreeHeap()<InternalStartupFloor+4096)return startupFailed_("heap-reserve");
#endif
#endif
    startupStage_="advertising";
    service->start();
    advertising_=BLEDevice::getAdvertising();if(!advertising_)return startupFailed_("advertising-allocation");advertising_->addServiceUUID(ServiceUuid);
    advertising_->setScanFilter(false,false);
    if(settings_->phonePlatform==1){BLEAdvertisementData scan;const uint8_t solicitation[]={17,0x15,0xd0,0x00,0x2d,0x12,0x1e,0x4b,0x0f,0xa4,0x99,0x4e,0xce,0xb5,0x31,0xf4,0x05,0x79};phoneBleScanData(scan,solicitation,sizeof(solicitation));scan.setName("RoboDesk");advertising_->setScanResponseData(scan);}
    else{BLEAdvertisementData scan;scan.setName("RoboDesk");advertising_->setScanResponseData(scan);}
    startupStage_="ancs-init";
    ancsAvailable_=settings_->phonePlatform==1&&ancs_.begin();
    startupStage_="advertising";
#if defined(ROBODESK_NIMBLE_EXTERNAL)
    if(!advertising_->start())return startupFailed_("advertising-start");
#else
    advertising_->start();
#endif
    phoneBleMemory("advertising");
#if defined(ROBODESK_NIMBLE_EXTERNAL)
#if !defined(ROBODESK_BLE_DIAGNOSTIC) || !ROBODESK_BLE_DIAGNOSTIC
    if(ESP.getFreeHeap()<InternalStartupFloor)return startupFailed_("heap-reserve");
#endif
#endif
    return true;
    };
    bool ready=false;
#if defined(ROBODESK_NIMBLE_EXTERNAL) && defined(__cpp_exceptions)
    try{ready=initialize();}catch(const std::bad_alloc&){startupFailure_="allocation-failed";phoneBleMemory("allocation-failed");}
#else
    ready=initialize();
#endif
    if(!ready){if(!startupFailure_[0])startupFailure_="characteristic-allocation";end();phoneBleMemory("startup-failed");}
    else {startupStage_="ready";
#if defined(ROBODESK_NIMBLE_EXTERNAL) && INCLUDE_xTaskGetHandle
      hostTask_=xTaskGetHandle("nimble_host");
#endif
    }
    return ready;
  }
  bool end(){
      hostTask_=nullptr; // Gateway samples only after setup, before any teardown.
      if(candidateAuthenticated_||candidateBonded_)deleteCandidateBond_();
      candidateAuthenticated_=candidateBonded_=false;
      pairingUntil_=0;enrollmentUntil_=0;trustedConnId_=-1;candidateConnId_=-1;
      portENTER_CRITICAL(&peerMux_);peerCache_[0]=0;candidateLease_.invalidate();portEXIT_CRITICAL(&peerMux_);
      ancs_.end();ancsAvailable_=false;
      bool stopped=true;
#if defined(ROBODESK_NIMBLE_EXTERNAL)
      stopped=NimBLEDevice::deinit(true);PhoneBleBackend::security=nullptr;ancs_.hostStopped(stopped);
#endif
      server_=nullptr;advertising_=nullptr;controlCharacteristic_=ackCharacteristic_=nullptr;
      if(packets_){vQueueDelete(packets_);packets_=nullptr;}
      if(companionResults_){vQueueDelete(companionResults_);companionResults_=nullptr;}
      resetSession_();authStoreReady_=false;bridgeKeyReady_=false;bridgeEnrollmentActive_=false;memset(authRecord_,0,sizeof(authRecord_));
      memset(outbound_,0,sizeof(outbound_));outboundReady_=false;outboundSize_=outboundOffset_=0;
      pendingPasskey_=0;passkeyWindowId_=0;passkeyUiPending_=passkeyUiQueued_=passkeyUiDisplayed_=false;
      return stopped;
  }
  void setRemoteRevokePersist(RemoteRevokePersist callback,void*context=nullptr){remoteRevokePersist_=callback;remoteRevokeContext_=context;}
  void setCompanionCommandDispatch(CompanionCommandDispatch callback){companionCommandDispatch_=callback;}
  bool publishCompanionResult(uint32_t sessionId,uint32_t requestCounter,uint8_t status,uint8_t reason){if(!sessionId||!requestCounter||!companionResults_)return false;CommandResult result{sessionId,requestCounter,status,reason};return xQueueSend(companionResults_,&result,0)==pdTRUE;}
  bool sessionActive(uint32_t sessionId)const{return sessionId&&activeSessionId_.load()==sessionId;}
  bool connected()const{return server_&&server_->getConnectedCount()>0;}
  const char* ancsStatus()const{return ancsAvailable_?ancs_.status():"unsupported";}
  void openPairingWindow(uint32_t now){const uint32_t code=freshPasskey_();pendingPasskey_=0;passkeyWindowId_=0;enrollmentUntil_=0;BLESecurity::setPassKey(true,code);portENTER_CRITICAL(&peerMux_);candidateLease_.invalidate();candidatePeer_[0]=0;candidateKeyPrepared_=false;preparedCandidate_={};portEXIT_CRITICAL(&peerMux_);candidateBonded_=false;candidateAuthenticated_=false;passkey_=code;pairingWindowId_=esp_random();if(!pairingWindowId_)pairingWindowId_=1;passkeyUiPending_=passkeyUiQueued_=passkeyUiDisplayed_=false;pairingUntil_=now+60000u;if(advertising_)advertising_->setScanFilter(false,false);}
  bool pairingOpen(uint32_t now)const{return int32_t(pairingUntil_-now)>0;}
  bool activePairingPasskey(uint32_t now,uint32_t&windowId,uint32_t&passkey)const{if(!passkeyUiPending_||candidateConnId_<0||!pairingOpen(now))return false;windowId=passkeyWindowId_.load();passkey=passkey_.load();return windowId&&windowId==pairingWindowId_.load()&&passkey<=999999u;}
  
  bool hasCandidate()const{if(!candidateBonded_)return false;portENTER_CRITICAL(&peerMux_);bool yes=candidatePeer_[0]!=0;portEXIT_CRITICAL(&peerMux_);return yes;}
  bool snapshotCandidateApproval(CandidateApproval&out)const{if(!pairingOpen(millis())||!candidateBonded_)return false;portENTER_CRITICAL(&peerMux_);const bool ok=candidateLease_.capture(out);portEXIT_CRITICAL(&peerMux_);return ok;}
  bool prepareCandidateApproval(const CandidateApproval&candidate){CandidateApproval current{};if(!settings_||!sameCandidate_(candidate,current))return false;if(settings_->phonePlatform!=0){portENTER_CRITICAL(&peerMux_);const bool valid=pairingOpen(millis())&&candidateLease_.matches(candidate)&&candidateBonded_;if(valid){preparedCandidate_=candidate;candidateKeyPrepared_=true;}portEXIT_CRITICAL(&peerMux_);return valid;}uint8_t next[AuthRecordSize];memcpy(next,authRecord_,sizeof(next));esp_fill_random(next+AuthKeyOffset,AuthKeySize);next[4]=1u;if(!persistAuthRecord_(next)){memset(next,0,sizeof(next));return false;}portENTER_CRITICAL(&peerMux_);bridgeKeyReady_=true;bridgeEnrollmentActive_=false;const bool valid=pairingOpen(millis())&&candidateBonded_&&candidateLease_.matches(candidate)&&candidateConnId_==candidate.connection;if(valid){memcpy(authRecord_,next,sizeof(authRecord_));preparedCandidate_=candidate;candidateKeyPrepared_=true;}portEXIT_CRITICAL(&peerMux_);memset(next,0,sizeof(next));if(!valid){clearAuthKey_();return false;}return true;}
  bool nextPairingPasskey(uint32_t&windowId,uint32_t&passkey,uint16_t&ttlMs){if(!passkeyUiPending_||passkeyUiQueued_||candidateConnId_<0)return false;const uint32_t left=pairingUntil_.load()-millis();if(!left||left>60000u){passkeyUiPending_=false;return false;}windowId=passkeyWindowId_.load();passkey=passkey_.load();ttlMs=uint16_t(left);if(!windowId||passkey>999999u)return false;passkeyUiQueued_=true;return true;}
  void pairingPasskeySendFailed(uint32_t id){if(pairingWindowId_==id&&!passkeyUiDisplayed_)passkeyUiQueued_=false;}
  void pairingPasskeyDisplayed(uint32_t id){if(id&&pairingWindowId_==id)passkeyUiDisplayed_=true;}
  const char* candidatePeer()const{portENTER_CRITICAL(&peerMux_);memcpy(candidateView_,candidatePeer_,sizeof(candidateView_));portEXIT_CRITICAL(&peerMux_);return candidateView_;}
  bool approveCandidate(const CandidateApproval&candidate){if(!settings_)return false;portENTER_CRITICAL(&peerMux_);const bool valid=pairingOpen(millis())&&candidateKeyPrepared_&&sameApproval_(preparedCandidate_,candidate)&&candidateBonded_&&candidateLease_.matches(candidate)&&candidateConnId_==candidate.connection&&!strcmp(candidatePeer_,candidate.address);if(valid){RuntimeSettings::copy(settings_->phoneBlePeer,sizeof(settings_->phoneBlePeer),candidate.address);memcpy(peerCache_,candidate.address,sizeof(peerCache_));candidatePeer_[0]=0;candidateKeyPrepared_=false;preparedCandidate_={};candidateBondAddrValid_=false;}portEXIT_CRITICAL(&peerMux_);if(!valid)return false;trustedConnId_=candidateConnId_.exchange(-1);pairingUntil_=0;if(bridgeKeyReady_&&!bridgeEnrollmentActive_&&settings_->phonePlatform==0)enrollmentUntil_=millis()+60000u;passkey_=0;pendingPasskey_=0;passkeyWindowId_=0;passkeyUiPending_=passkeyUiQueued_=passkeyUiDisplayed_=false;BLESecurity::setPassKey(true,freshPasskey_());if(advertising_)advertising_->setScanFilter(false,false);return true;}
  bool clearPeer(bool allowUninitializedHost=false){const bool keyCleared=clearAuthKey_();resetSession_();memset(outbound_,0,sizeof(outbound_));outboundReady_=false;outboundSize_=outboundOffset_=0;if(settings_)settings_->phoneBlePeer[0]=0;enrollmentUntil_=0;if(bridge_)bridge_->clear();if(navigation_)navigation_->clear();if(packets_)xQueueReset(packets_);if(companionResults_)xQueueReset(companionResults_);candidateAuthenticated_=candidateBonded_=false;passkeyUiPending_=passkeyUiQueued_=passkeyUiDisplayed_=false;passkey_=0;
#if defined(CONFIG_NIMBLE_ENABLED)
    const bool bondsCleared=(allowUninitializedHost&&!server_)||ble_store_clear()==0;
#else
    const bool bondsCleared=false;
#endif
portENTER_CRITICAL(&peerMux_);candidateLease_.invalidate();candidatePeer_[0]=0;peerCache_[0]=0;candidateKeyPrepared_=false;preparedCandidate_={};portEXIT_CRITICAL(&peerMux_);pairingUntil_=0;passkey_=0;pendingPasskey_=0;passkeyWindowId_=0;passkeyUiPending_=passkeyUiQueued_=passkeyUiDisplayed_=false;BLESecurity::setPassKey(true,freshPasskey_());int trusted=trustedConnId_.exchange(-1);if(trusted>=0)rejectConnId_=trusted;else if(candidateConnId_>=0)rejectConnId_=candidateConnId_.exchange(-1);if(advertising_)advertising_->setScanFilter(false,false);return keyCleared&&bondsCleared;}
  void service(uint32_t now){cacheSettings_();if(sessionResetPending_.exchange(false)){resetSession_();memset(outbound_,0,sizeof(outbound_));outboundReady_=false;outboundSize_=outboundOffset_=0;if(companionResults_)xQueueReset(companionResults_);}uint8_t control[PhoneBridgeProtocol::MaxMessage]{};size_t controlSize=0;uint16_t controlConnection=0;portENTER_CRITICAL(&peerMux_);if(pendingControlReady_){controlSize=pendingControlSize_;controlConnection=pendingControlConnection_;memcpy(control,pendingControl_,controlSize);memset(pendingControl_,0,sizeof(pendingControl_));pendingControlSize_=0;pendingControlReady_=false;}portEXIT_CRITICAL(&peerMux_);if(controlSize)processControl_(control,controlSize,controlConnection,now);memset(control,0,sizeof(control));if(handshakePending_&&uint32_t(now-handshakeAt_)>10000u)resetSession_();pumpCommandResults_(now);pumpOutbound_(now);
    if(bridge_&&settings_){bridge_->filter(settings_->notificationAllowlist);bridge_->expire(now);}
    char nav[PhoneNavigationBridge::WireMax+1]{};uint32_t navAt=0;int navConn=-1;portENTER_CRITICAL(&peerMux_);if(navPending_){memcpy(nav,navWire_,sizeof(nav));navAt=navAt_;navConn=navConnection_;navPending_=false;}portEXIT_CRITICAL(&peerMux_);
    if(nav[0]&&navigation_&&settings_->navigationEnabled&&settings_->phonePlatform==0&&isTrustedConnection_(uint16_t(navConn)))navigation_->accept(nav,now,uint32_t(now-navAt));
    if(navigation_)navigation_->expire(now);
    if(navigation_&&settings_&&(!settings_->navigationEnabled||settings_->phonePlatform))navigation_->clear();
    Packet packet;while(packets_&&xQueueReceive(packets_,&packet,0)==pdTRUE){if(packet.kind==4&&isTrustedConnection_(packet.connection)&&!settings_->phonePlatform)processEnvelope_(packet,now);memset(&packet,0,sizeof(packet));}
    if(ancsAvailable_)ancs_.service(trustedConnId_,settings_&&settings_->phonePlatform==1,bridge_,settings_?settings_->notificationAllowlist:"",now);
    uint32_t notifiedCode=pendingPasskey_.exchange(0);const uint32_t notifiedWindow=passkeyWindowId_.exchange(0);if(notifiedCode&&notifiedCode<=999999u&&notifiedWindow==pairingWindowId_.load()&&pairingOpen(now)&&candidateConnId_>=0){passkey_=notifiedCode;passkeyUiPending_=true;passkeyUiQueued_=passkeyUiDisplayed_=false;passkeyWindowId_=notifiedWindow;}
    if(candidateAuthenticated_.load()&&candidateConnId_>=0&&passkeyUiDisplayed_&&authenticatedWindowId_==pairingWindowId_&&pairingOpen(now)){candidateBonded_=true;candidateAuthenticated_=false;passkeyUiPending_=passkeyUiQueued_=false;}
    if(pairingUntil_&&int32_t(now-pairingUntil_)>=0){if(candidateAuthenticated_||candidateBonded_)deleteCandidateBond_();pairingUntil_=0;candidateAuthenticated_=candidateBonded_=false;passkey_=0;pendingPasskey_=0;passkeyWindowId_=0;passkeyUiPending_=passkeyUiQueued_=passkeyUiDisplayed_=false;BLESecurity::setPassKey(true,freshPasskey_());portENTER_CRITICAL(&peerMux_);candidateLease_.invalidate();candidatePeer_[0]=0;candidateBondAddrValid_=false;candidateKeyPrepared_=false;preparedCandidate_={};int expiredCandidate=candidateConnId_.exchange(-1);portEXIT_CRITICAL(&peerMux_);if(expiredCandidate>=0)rejectConnId_=expiredCandidate;}if(enrollmentUntil_&&int32_t(now-enrollmentUntil_)>=0)enrollmentUntil_=0;
    int id=rejectConnId_.exchange(-1);if(id>=0&&server_)server_->disconnect(uint16_t(id));if(advertisingRestartPending_.exchange(false)&&advertising_)advertising_->start();}
 private:
  const char*startupStage_="not-started";const char*startupFailure_="";
  TaskHandle_t hostTask_=nullptr;
  bool startupFailed_(const char*reason){startupFailure_=reason;return false;}
  static constexpr size_t InternalStartupFloor=ROBODESK_DUAL_ROBOT?32768:49152;
  static constexpr size_t AuthRecordSize=53,AuthIdOffset=5,AuthKeyOffset=21,AuthIdSize=16,AuthKeySize=32;
  static constexpr const char* AuthNamespace="robodesk_v2auth";
  static constexpr const char* AuthRecordName="record";
  uint8_t authRecord_[AuthRecordSize]{};std::atomic<bool>authStoreReady_{false},bridgeKeyReady_{false},bridgeEnrollmentActive_{false};bool candidateKeyPrepared_=false;CandidateApproval preparedCandidate_{};PhoneBridgeProtocol::CandidateLeaseState candidateLease_{};
  std::atomic<uint32_t> enrollmentUntil_{0};
  struct Packet{uint16_t connection=0;uint8_t kind=0;uint16_t size=0;uint8_t data[PhoneBridgeProtocol::MaxMessage]={};};
  struct CommandResult{uint32_t sessionId=0,requestCounter=0;uint8_t status=1,reason=0;};
  QueueHandle_t packets_=nullptr,companionResults_=nullptr;
  CompanionCommandDispatch companionCommandDispatch_=nullptr;
#if defined(CONFIG_NIMBLE_ENABLED)
  using PeerEvent=ble_gap_conn_desc;
#else
  using PeerEvent=esp_ble_gatts_cb_param_t;
#endif
  PhoneNotificationBridge* bridge_=nullptr;RuntimeSettings* settings_=nullptr;
  PhoneNavigationBridge* navigation_=nullptr;PhoneAncsClient ancs_;bool ancsAvailable_=false;
  BLECharacteristic* controlCharacteristic_=nullptr;BLECharacteristic* ackCharacteristic_=nullptr;
  PhoneBridgeProtocol::FragmentReassembler controlReassembler_{},envelopeReassembler_{};
  uint8_t pendingControl_[PhoneBridgeProtocol::MaxMessage]{};uint16_t pendingControlSize_=0,pendingControlConnection_=0;bool pendingControlReady_=false;
  uint8_t sessionKey_[PhoneBridgeProtocol::MacSize]{};uint8_t phoneNonce_[32]{},robotNonce_[32]{};uint32_t sessionId_=0,lastRxCounter_=0,nextTxCounter_=0;uint16_t sessionConnection_=0;bool sessionActive_=false,handshakePending_=false,enrollmentHandshake_=false;uint32_t handshakeAt_=0;std::atomic<uint32_t>activeSessionId_{0};std::atomic<bool>sessionResetPending_{false};RemoteRevokePersist remoteRevokePersist_=nullptr;void*remoteRevokeContext_=nullptr;bool revokeFinishPending_=false;uint32_t revokeFinishAt_=0;
  uint8_t outbound_[PhoneBridgeProtocol::MaxMessage]{};uint16_t outboundSize_=0,outboundOffset_=0,outboundId_=0;uint8_t outboundChannel_=0;uint32_t outboundAt_=0;bool outboundReady_=false;
  char navWire_[PhoneNavigationBridge::WireMax+1]{};uint32_t navAt_=0;uint16_t navConnection_=0;bool navPending_=false;std::atomic<int>candidateConnId_{-1};std::atomic<bool>candidateBonded_{false};std::atomic<bool>candidateAuthenticated_{false},passkeyUiPending_{false},passkeyUiQueued_{false},passkeyUiDisplayed_{false};std::atomic<uint32_t> pairingWindowId_{0},passkeyWindowId_{0},authenticatedWindowId_{0},passkey_{0},pendingPasskey_{0};std::atomic<uint8_t>platform_{0},navEnabled_{1};
  bool candidateBondAddrValid_=false;
#if defined(CONFIG_NIMBLE_ENABLED)
  ble_addr_t candidateBondAddr_{};
#endif
  BLEServer* server_=nullptr;BLEAdvertising* advertising_=nullptr;
  std::atomic<uint32_t> pairingUntil_{0};std::atomic<int> rejectConnId_{-1},trustedConnId_{-1};std::atomic<bool> advertisingRestartPending_{false};char candidatePeer_[18]={},peerCache_[18]={},allowlistCache_[192]={};mutable char candidateView_[18]={};mutable portMUX_TYPE peerMux_=portMUX_INITIALIZER_UNLOCKED;
  void cacheSettings_(){if(!settings_)return;platform_=settings_->phonePlatform;navEnabled_=settings_->navigationEnabled;portENTER_CRITICAL(&peerMux_);memcpy(peerCache_,settings_->phoneBlePeer,sizeof(peerCache_));memcpy(allowlistCache_,settings_->notificationAllowlist,sizeof(allowlistCache_));portEXIT_CRITICAL(&peerMux_);}
  bool loadAuthRecord_(){Preferences prefs;if(!prefs.begin(AuthNamespace,false))return false;const size_t size=prefs.getBytesLength(AuthRecordName);bool ok=false;if(size==0){memset(authRecord_,0,sizeof(authRecord_));memcpy(authRecord_,"RDB2",4);esp_fill_random(authRecord_+AuthIdOffset,AuthIdSize);uint8_t any=0;for(size_t i=0;i<AuthIdSize;++i)any|=authRecord_[AuthIdOffset+i];if(!any)authRecord_[AuthIdOffset]=1;ok=prefs.putBytes(AuthRecordName,authRecord_,sizeof(authRecord_))==sizeof(authRecord_);}else if(size==sizeof(authRecord_)&&prefs.getBytes(AuthRecordName,authRecord_,sizeof(authRecord_))==sizeof(authRecord_)){const uint8_t flags=authRecord_[4];ok=!memcmp(authRecord_,"RDB2",4)&&(flags&~3u)==0&&(!(flags&2u)||(flags&1u));if(ok&&(flags&1u)){uint8_t any=0;for(size_t i=0;i<AuthKeySize;++i)any|=authRecord_[AuthKeyOffset+i];ok=any!=0;}else if(ok){for(size_t i=0;i<AuthKeySize;++i)if(authRecord_[AuthKeyOffset+i])ok=false;}if(ok&&(flags&1u)&&!(flags&2u)){authRecord_[4]=0;memset(authRecord_+AuthKeyOffset,0,AuthKeySize);uint8_t check[AuthRecordSize]{};ok=prefs.putBytes(AuthRecordName,authRecord_,sizeof(authRecord_))==sizeof(authRecord_)&&prefs.getBytes(AuthRecordName,check,sizeof(check))==sizeof(check)&&!memcmp(check,authRecord_,sizeof(check));memset(check,0,sizeof(check));}if(ok){bridgeKeyReady_=(authRecord_[4]&1u)!=0;bridgeEnrollmentActive_=(authRecord_[4]&2u)!=0;}}prefs.end();authStoreReady_=ok;return ok;}
  bool persistAuthRecord_(const uint8_t*record){if(!record||!authStoreReady_)return false;Preferences prefs;if(!prefs.begin(AuthNamespace,false))return false;uint8_t check[AuthRecordSize]{};const bool ok=prefs.putBytes(AuthRecordName,record,AuthRecordSize)==AuthRecordSize&&prefs.getBytes(AuthRecordName,check,AuthRecordSize)==AuthRecordSize&&!memcmp(check,record,AuthRecordSize);prefs.end();memset(check,0,sizeof(check));return ok;}
  static bool sameApproval_(const CandidateApproval&a,const CandidateApproval&b){return a.generation&&a.generation==b.generation&&a.connection==b.connection&&!strcmp(a.address,b.address);}
  bool sameCandidate_(const CandidateApproval&candidate,CandidateApproval&current)const{return snapshotCandidateApproval(current)&&sameApproval_(candidate,current);}
  bool clearAuthKey_(){
    Preferences prefs;if(!prefs.begin(AuthNamespace,false))return false;
    PhoneBridgeAuthRecord::Store store{&prefs,readAuthRecord_,writeAuthRecordVerified_,removeAuthRecordVerified_};
    uint8_t updated[AuthRecordSize]{};bool present=false;
    const bool ok=PhoneBridgeAuthRecord::clearCredential(store,updated,present);prefs.end();
    if(ok){portENTER_CRITICAL(&peerMux_);memcpy(authRecord_,updated,sizeof(authRecord_));authStoreReady_=present;bridgeKeyReady_=bridgeEnrollmentActive_=false;portEXIT_CRITICAL(&peerMux_);}
    memset(updated,0,sizeof(updated));return ok;
  }
  static bool readAuthRecord_(void*context,uint8_t*out,size_t capacity,size_t*size){auto*p=static_cast<Preferences*>(context);if(!p||!size)return false;*size=p->getBytesLength(AuthRecordName);if(!*size)return true;return out&&*size<=capacity&&p->getBytes(AuthRecordName,out,*size)==*size;}
  static bool writeAuthRecordVerified_(void*context,const uint8_t*record,size_t size){auto*p=static_cast<Preferences*>(context);if(!p||!record||size!=AuthRecordSize||p->putBytes(AuthRecordName,record,size)!=size)return false;uint8_t check[AuthRecordSize]{};const bool ok=p->getBytes(AuthRecordName,check,sizeof(check))==sizeof(check)&&!memcmp(check,record,sizeof(check));memset(check,0,sizeof(check));return ok;}
  static bool removeAuthRecordVerified_(void*context){auto*p=static_cast<Preferences*>(context);return p&&p->remove(AuthRecordName)&&p->getBytesLength(AuthRecordName)==0;}
  static uint32_t freshPasskey_(){uint32_t raw;const uint64_t ceiling=uint64_t(1)<<32,limit=ceiling-(ceiling%1000000u);do{esp_fill_random(&raw,sizeof(raw));}while(uint64_t(raw)>=limit);uint32_t code=raw%1000000u;return code?code:1;}
  static bool envelopeAgeLimit_(uint8_t kind,uint32_t*age,void*){if(!age)return false;if(kind==1||kind==2){*age=300000u;return true;}if(kind==3){*age=120000u;return true;}if(kind==6){*age=30000u;return true;}if(kind==4||kind==5||kind==7){*age=0;return true;}return false;}
  void pumpCommandResults_(uint32_t now){if(outboundReady_||!sessionActive_||!companionResults_)return;CommandResult result{};while(xQueueReceive(companionResults_,&result,0)==pdTRUE){if(result.sessionId!=sessionId_){memset(&result,0,sizeof(result));continue;}uint8_t payload[6]{};PhoneBridgeProtocol::write32le(payload,result.requestCounter);payload[4]=result.status;payload[5]=result.reason;uint8_t wire[PhoneBridgeProtocol::MaxEnvelope]{};size_t size=0;if(nextTxCounter_!=0xffffffffu&&PhoneBridgeProtocol::encodeEnvelope(sessionKey_,sizeof(sessionKey_),PhoneBridgeProtocol::RobotToPhone,0x7e,sessionId_,++nextTxCounter_,0,payload,sizeof(payload),wire,sizeof(wire),&size))queueOutbound_(1,wire,size,now);memset(&result,0,sizeof(result));memset(payload,0,sizeof(payload));memset(wire,0,sizeof(wire));return;}}
  static bool sameMac_(const uint8_t*a,const uint8_t*b,size_t size){uint8_t diff=0;for(size_t i=0;i<size;++i)diff|=uint8_t(a[i]^b[i]);return diff==0;}
  static bool handshakeProof_(const uint8_t*key,const char*domain,const uint8_t*transcript,size_t transcriptSize,uint8_t out[32]){if(!key||!domain||!transcript||!out)return false;return PhoneBridgeProtocol::hmacSha256Domain(key,32,reinterpret_cast<const uint8_t*>(domain),strlen(domain)+1,transcript,transcriptSize,out);}
  bool queueOutbound_(uint8_t channel,const uint8_t*bytes,size_t size,uint32_t now){if(!bytes||!size||size>sizeof(outbound_)||outboundReady_)return false;memcpy(outbound_,bytes,size);outboundSize_=uint16_t(size);outboundOffset_=0;if(++outboundId_==0)++outboundId_;outboundChannel_=channel;outboundAt_=now;outboundReady_=true;return true;}
  void acceptControlFragment_(const uint8_t*bytes,size_t size,uint16_t connection,uint32_t now){if(!bytes||size>PhoneBridgeProtocol::MaxMessage||!isTrustedConnection_(connection))return;portENTER_CRITICAL(&peerMux_);size_t completed=0;if(pendingControlReady_){controlReassembler_.reset();portEXIT_CRITICAL(&peerMux_);return;}const bool done=controlReassembler_.accept(bytes,size,now,pendingControl_,sizeof(pendingControl_),&completed);if(done){pendingControlSize_=uint16_t(completed);pendingControlConnection_=connection;pendingControlReady_=true;}portEXIT_CRITICAL(&peerMux_);}
  void acceptEnvelopeFragment_(const uint8_t*bytes,size_t size,uint16_t connection,uint32_t now){if(!bytes||size>PhoneBridgeProtocol::MaxMessage||!isTrustedConnection_(connection)||!packets_)return;Packet packet{};portENTER_CRITICAL(&peerMux_);size_t completed=0;const bool done=envelopeReassembler_.accept(bytes,size,now,packet.data,sizeof(packet.data),&completed);portEXIT_CRITICAL(&peerMux_);if(!done)return;packet.connection=connection;packet.kind=4;packet.size=uint16_t(completed);if(xQueueSend(packets_,&packet,0)!=pdTRUE)memset(&packet,0,sizeof(packet));}
  void pumpOutbound_(uint32_t now){if(revokeFinishPending_&&int32_t(now-revokeFinishAt_)>=0){const int connection=sessionConnection_;revokeFinishPending_=false;trustedConnId_=-1;resetSession_();memset(outbound_,0,sizeof(outbound_));outboundReady_=false;outboundSize_=outboundOffset_=0;if(server_&&connection>=0)server_->disconnect(uint16_t(connection));}pumpOutboundFrames_(now);}
  void pumpOutboundFrames_(uint32_t now){if(!outboundReady_||int32_t(now-outboundAt_)<0)return;const int conn=trustedConnId_.load();if(conn<0||!sessionOrHandshakeFor_(uint16_t(conn))||!server_||server_->getConnectedCount()!=1){memset(outbound_,0,sizeof(outbound_));outboundReady_=false;outboundSize_=outboundOffset_=0;return;}const uint16_t mtu=server_->getPeerMTU(uint16_t(conn));uint8_t frame[PhoneBridgeProtocol::MaxMessage]{};const size_t frameSize=PhoneBridgeProtocol::encodeFragment(outbound_,outboundSize_,outboundId_,outboundOffset_,mtu,frame,sizeof(frame));if(!frameSize){memset(outbound_,0,sizeof(outbound_));outboundReady_=false;outboundSize_=outboundOffset_=0;return;}BLECharacteristic*characteristic=outboundChannel_?ackCharacteristic_:controlCharacteristic_;if(!characteristic){outboundReady_=false;return;}characteristic->setValue(frame,frameSize);if(!phoneBleNotify(characteristic,uint16_t(conn))){outboundAt_=now+30u;return;}outboundOffset_=uint16_t(outboundOffset_+frameSize-PhoneBridgeProtocol::FragmentHeaderSize);outboundAt_=now+15u;if(outboundOffset_>=outboundSize_){memset(outbound_,0,sizeof(outbound_));outboundReady_=false;outboundSize_=outboundOffset_=0;}}
  bool sessionOrHandshakeFor_(uint16_t conn)const{return isTrustedConnection_(conn)&&(sessionActive_&&sessionConnection_==conn||handshakePending_&&pendingControlConnection_==conn);}
  void resetSession_(){activeSessionId_=0;memset(sessionKey_,0,sizeof(sessionKey_));memset(phoneNonce_,0,sizeof(phoneNonce_));memset(robotNonce_,0,sizeof(robotNonce_));sessionId_=lastRxCounter_=nextTxCounter_=0;sessionConnection_=0;sessionActive_=handshakePending_=enrollmentHandshake_=false;handshakeAt_=0;}
  bool makeAck_(uint32_t requestCounter,uint8_t status,uint8_t reason,uint32_t now){uint8_t payload[6]{};PhoneBridgeProtocol::write32le(payload,requestCounter);payload[4]=status;payload[5]=reason;uint8_t wire[PhoneBridgeProtocol::MaxEnvelope]{};size_t size=0;if(!sessionActive_||nextTxCounter_==0xffffffffu||!PhoneBridgeProtocol::encodeEnvelope(sessionKey_,sizeof(sessionKey_),PhoneBridgeProtocol::RobotToPhone,0x7f,sessionId_,++nextTxCounter_,0,payload,sizeof(payload),wire,sizeof(wire),&size))return false;return queueOutbound_(1,wire,size,now);}
  void processControl_(const uint8_t*data,size_t size,uint16_t connection,uint32_t now){if(!data||size<2||data[0]!=2||settings_==nullptr||settings_->phonePlatform||!isTrustedConnection_(connection))return;const uint8_t op=data[1];if(op==1&&size==34){resetSession_();uint8_t key[32]{};bool active=false,enrolling=false;portENTER_CRITICAL(&peerMux_);if(bridgeKeyReady_){memcpy(key,authRecord_+AuthKeyOffset,sizeof(key));active=bridgeEnrollmentActive_;}portEXIT_CRITICAL(&peerMux_);enrolling=!active&&bridgeKeyReady_&&enrollmentUntil_&&int32_t(enrollmentUntil_-now)>0;if(!active&&!enrolling){memset(key,0,sizeof(key));return;}memcpy(phoneNonce_,data+2,sizeof(phoneNonce_));esp_fill_random(robotNonce_,sizeof(robotNonce_));esp_fill_random(&sessionId_,sizeof(sessionId_));if(!sessionId_)sessionId_=1;uint8_t transcript[84];portENTER_CRITICAL(&peerMux_);memcpy(transcript,authRecord_+AuthIdOffset,AuthIdSize);portEXIT_CRITICAL(&peerMux_);memcpy(transcript+16,phoneNonce_,32);memcpy(transcript+48,robotNonce_,32);PhoneBridgeProtocol::write32le(transcript+80,sessionId_);uint8_t proof[32]{};if(!handshakeProof_(key,"RoboDesk-Bridge-ServerProof-v2",transcript,sizeof(transcript),proof)){memset(key,0,sizeof(key));return;}uint8_t response[118]{};response[0]=2;response[1]=enrolling?0x82:0x81;memcpy(response+2,transcript,16);PhoneBridgeProtocol::write32le(response+18,sessionId_);memcpy(response+22,robotNonce_,32);size_t proofOffset=54;if(enrolling){memcpy(response+54,key,32);proofOffset=86;}memcpy(response+proofOffset,proof,32);memcpy(sessionKey_,key,32);sessionConnection_=connection;handshakePending_=true;sessionActive_=false;enrollmentHandshake_=enrolling;handshakeAt_=now;pendingControlConnection_=connection;queueOutbound_(0,response,proofOffset+32,now);memset(key,0,sizeof(key));memset(proof,0,sizeof(proof));memset(transcript,0,sizeof(transcript));memset(response,0,sizeof(response));return;}
    if(op==2&&size==38&&handshakePending_&&sessionConnection_==connection&&sessionId_==PhoneBridgeProtocol::read32le(data+2)&&uint32_t(now-handshakeAt_)<=10000u){uint8_t transcript[84]{};portENTER_CRITICAL(&peerMux_);memcpy(transcript,authRecord_+AuthIdOffset,AuthIdSize);portEXIT_CRITICAL(&peerMux_);memcpy(transcript+16,phoneNonce_,32);memcpy(transcript+48,robotNonce_,32);PhoneBridgeProtocol::write32le(transcript+80,sessionId_);uint8_t expected[32]{};const bool proofOk=handshakeProof_(sessionKey_,"RoboDesk-Bridge-ClientProof-v2",transcript,sizeof(transcript),expected)&&sameMac_(expected,data+6,32);memset(expected,0,sizeof(expected));if(!proofOk){resetSession_();return;}if(enrollmentHandshake_){uint8_t updated[AuthRecordSize];portENTER_CRITICAL(&peerMux_);memcpy(updated,authRecord_,sizeof(updated));portEXIT_CRITICAL(&peerMux_);updated[4]=3u;if(!persistAuthRecord_(updated)){memset(updated,0,sizeof(updated));resetSession_();return;}portENTER_CRITICAL(&peerMux_);memcpy(authRecord_,updated,sizeof(authRecord_));bridgeKeyReady_=bridgeEnrollmentActive_=true;portEXIT_CRITICAL(&peerMux_);memset(updated,0,sizeof(updated));enrollmentUntil_=0;}sessionActive_=true;sessionConnection_=connection;activeSessionId_=sessionId_;lastRxCounter_=0;nextTxCounter_=0;handshakePending_=false;uint8_t proof[32]{};if(!handshakeProof_(sessionKey_,"RoboDesk-Bridge-SessionAck-v2",transcript,sizeof(transcript),proof)){resetSession_();return;}uint8_t response[38]{2,0x83};PhoneBridgeProtocol::write32le(response+2,sessionId_);memcpy(response+6,proof,32);queueOutbound_(0,response,sizeof(response),now);memset(proof,0,sizeof(proof));memset(transcript,0,sizeof(transcript));return;}
    resetSession_();}
  void processEnvelope_(const Packet&packet,uint32_t now){if(!sessionActive_||packet.connection!=sessionConnection_)return;PhoneBridgeProtocol::HmacKey key{sessionKey_,sizeof(sessionKey_)};PhoneBridgeProtocol::EnvelopeView message{};if(!PhoneBridgeProtocol::verifyEnvelope(packet.data,packet.size,PhoneBridgeProtocol::PhoneToRobot,sessionId_,envelopeAgeLimit_,nullptr,PhoneBridgeProtocol::verifyHmacSha256,&key,&message))return;if(message.counter<=lastRxCounter_){makeAck_(message.counter,1,2,now);return;}lastRxCounter_=message.counter;if(message.kind==6){PhoneCompanionCommand::Command command{};const bool valid=PhoneCompanionCommand::decode(message.payload,message.payloadSize,command);const bool queued=valid&&companionCommandDispatch_&&companionCommandDispatch_(message.sessionId,message.counter,message.ageMs,message.payload,message.payloadSize);makeAck_(message.counter,queued?0:1,queued?0:(valid?6:3),now);return;}char text[PhoneBridgeProtocol::MaxPayload+1]{};if(message.payloadSize>=sizeof(text)||memchr(message.payload,0,message.payloadSize)){makeAck_(message.counter,1,3,now);return;}memcpy(text,message.payload,message.payloadSize);if(message.kind==4&&message.payloadSize==0){const bool revoked=prepareRemoteRevocation_();const bool ackQueued=makeAck_(message.counter,revoked?0:1,revoked?0:5,now);revokeFinishPending_=true;revokeFinishAt_=now+(ackQueued?1000u:0u);return;}if(message.kind==5&&message.payloadSize==0){if(bridge_)bridge_->clear();if(navigation_)navigation_->clear();makeAck_(message.counter,0,0,now);return;}if(message.kind==7){const bool accepted=bridge_&&message.payloadSize<64&&bridge_->removeApp(text);makeAck_(message.counter,accepted?0:1,accepted?0:3,now);return;}bool accepted=false;char*fields[5]={text};unsigned count=1;if(message.kind!=3){for(size_t i=0;i<message.payloadSize;++i)if(text[i]=='\n'){if(count>=5){count=0;break;}text[i]=0;fields[count++]=text+i+1;}}if(message.kind==1&&count==5&&bridge_&&settings_)accepted=bridge_->accept(fields[1],fields[2],fields[3],fields[4],settings_->notificationAllowlist,now,fields[0],message.ageMs);else if(message.kind==2&&count==2&&bridge_)accepted=bridge_->remove(fields[1],fields[0]);else if(message.kind==3&&settings_&&settings_->navigationEnabled&&navigation_)accepted=navigation_->accept(text,now,message.ageMs);makeAck_(message.counter,accepted?0:1,accepted?0:4,now);}
  bool prepareRemoteRevocation_(){const bool configSaved=remoteRevokePersist_&&remoteRevokePersist_(remoteRevokeContext_);if(settings_)settings_->phoneBlePeer[0]=0;const bool keyCleared=clearAuthKey_();
#if defined(CONFIG_NIMBLE_ENABLED)
    const bool bondsCleared=ble_store_clear()==0;
#else
    const bool bondsCleared=false;
#endif
    portENTER_CRITICAL(&peerMux_);peerCache_[0]=0;candidateLease_.invalidate();candidatePeer_[0]=0;candidateKeyPrepared_=false;preparedCandidate_={};portEXIT_CRITICAL(&peerMux_);enrollmentUntil_=0;pairingUntil_=0;if(bridge_)bridge_->clear();if(navigation_)navigation_->clear();if(packets_)xQueueReset(packets_);return configSaved&&keyCleared&&bondsCleared;}
  void offerCandidate_(const String&address,uint16_t connection){portENTER_CRITICAL(&peerMux_);candidatePeer_[0]=0;address.toCharArray(candidatePeer_,sizeof(candidatePeer_));candidateLease_.connected(connection,candidatePeer_);candidateConnId_=connection;portEXIT_CRITICAL(&peerMux_);}
  void deleteCandidateBond_(){
#if defined(CONFIG_NIMBLE_ENABLED)
    ble_addr_t address{};bool valid=false;portENTER_CRITICAL(&peerMux_);if(candidateBondAddrValid_&&(candidateAuthenticated_||candidateBonded_)){address=candidateBondAddr_;valid=true;}portEXIT_CRITICAL(&peerMux_);if(valid)ble_store_util_delete_peer(&address);
#endif
  }
  bool addressTrusted_(const String&address)const{char peer[18];portENTER_CRITICAL(&peerMux_);memcpy(peer,peerCache_,sizeof(peer));portEXIT_CRITICAL(&peerMux_);return peer[0]&&address.equals(peer);}
  bool isTrustedConnection_(uint16_t id)const{return trustedConnId_.load()==int(id);}
  class ServerCallbacks:public BLEServerCallbacks{
   public:explicit ServerCallbacks(PhoneBleTransport* owner):owner_(owner){}
    void onConnect(BLEServer*server,PeerEvent* param)override{esp_rom_printf("[BleHeap] connect free=%u min=%u largest=%u\n",unsigned(ESP.getFreeHeap()),unsigned(ESP.getMinFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
      
      if(!owner_||!param||!owner_->settings_)return;
#if defined(CONFIG_NIMBLE_ENABLED)
      char mac[18];const auto&v=param->peer_id_addr.val;snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",v[5],v[4],v[3],v[2],v[1],v[0]);String address(mac);const uint16_t connection=param->conn_handle;
#else
      String address=BLEAddress(param->connect.remote_bda).toString();address.toUpperCase();const uint16_t connection=param->connect.conn_id;
#endif
      const bool trusted=owner_->addressTrusted_(address);
      if(trusted&&owner_->trustedConnId_<0){owner_->trustedConnId_=connection;
#if defined(CONFIG_NIMBLE_ENABLED)
        BLESecurity::startSecurity(connection);
#endif
        return;}
      if(owner_->pairingOpen(millis())&&owner_->candidateConnId_<0){owner_->offerCandidate_(address,connection);owner_->candidateBonded_=false;
#if defined(CONFIG_NIMBLE_ENABLED)
        portENTER_CRITICAL(&owner_->peerMux_);owner_->candidateBondAddr_=param->peer_id_addr;owner_->candidateBondAddrValid_=true;portEXIT_CRITICAL(&owner_->peerMux_);
        BLESecurity::startSecurity(connection);
#endif
        return;}
      owner_->rejectConnId_=connection;if(server)server->disconnect(connection);
    }
    void onDisconnect(BLEServer*,PeerEvent* param)override{esp_rom_printf("[BleHeap] disconnect free=%u min=%u largest=%u\n",unsigned(ESP.getFreeHeap()),unsigned(ESP.getMinFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));if(owner_&&param){
#if defined(CONFIG_NIMBLE_ENABLED)
      const uint16_t connection=param->conn_handle;
#else
      const uint16_t connection=param->disconnect.conn_id;
#endif
    if(owner_->trustedConnId_==connection){owner_->trustedConnId_=-1;owner_->sessionResetPending_=true;portENTER_CRITICAL(&owner_->peerMux_);owner_->controlReassembler_.reset();owner_->envelopeReassembler_.reset();owner_->pendingControlReady_=false;owner_->pendingControlSize_=0;memset(owner_->pendingControl_,0,sizeof(owner_->pendingControl_));portEXIT_CRITICAL(&owner_->peerMux_);}if(owner_->candidateConnId_==connection){if(owner_->candidateAuthenticated_||owner_->candidateBonded_)owner_->deleteCandidateBond_();portENTER_CRITICAL(&owner_->peerMux_);owner_->candidateLease_.disconnected(connection);owner_->candidatePeer_[0]=0;owner_->candidateConnId_=-1;owner_->candidateBonded_=owner_->candidateAuthenticated_=false;owner_->candidateBondAddrValid_=false;owner_->candidateKeyPrepared_=false;owner_->preparedCandidate_={};owner_->controlReassembler_.reset();owner_->envelopeReassembler_.reset();portEXIT_CRITICAL(&owner_->peerMux_);owner_->passkeyUiPending_=owner_->passkeyUiQueued_=owner_->passkeyUiDisplayed_=false;}if(owner_->packets_)xQueueReset(owner_->packets_);owner_->advertisingRestartPending_=true;}}
   private:PhoneBleTransport* owner_;
  };
  class IdentityCallbacks:public BLECharacteristicCallbacks{
   public:explicit IdentityCallbacks(PhoneBleTransport* owner):owner_(owner){}
    void onRead(BLECharacteristic* characteristic,PeerEvent* param)override{
#if defined(CONFIG_NIMBLE_ENABLED)
      const bool trusted=owner_&&param&&param->sec_state.encrypted&&param->sec_state.authenticated&&param->sec_state.key_size>=16&&owner_->isTrustedConnection_(param->conn_handle);
#else
      const bool trusted=owner_&&param&&owner_->isTrustedConnection_(param->read.conn_id);
#endif
      if(!trusted||!owner_->authStoreReady_){characteristic->setValue("");return;}
      uint8_t value[26]{};value[0]=2;value[1]=1;portENTER_CRITICAL(&owner_->peerMux_);const bool active=owner_->bridgeEnrollmentActive_;memcpy(value+6,owner_->authRecord_+AuthIdOffset,AuthIdSize);portEXIT_CRITICAL(&owner_->peerMux_);const bool enrolling=owner_->bridgeKeyReady_&&!active&&owner_->enrollmentUntil_&&int32_t(owner_->enrollmentUntil_-millis())>0;uint32_t flags=1u|(active?2u:0u)|(enrolling?4u:0u);
      value[2]=uint8_t(flags);value[3]=uint8_t(flags>>8);value[4]=uint8_t(flags>>16);value[5]=uint8_t(flags>>24);
      const uint16_t maxEnvelope=PhoneBridgeProtocol::MaxEnvelope,maxPayload=PhoneBridgeProtocol::MaxPayload;
      value[22]=uint8_t(maxEnvelope);value[23]=uint8_t(maxEnvelope>>8);value[24]=uint8_t(maxPayload);value[25]=uint8_t(maxPayload>>8);
      characteristic->setValue(value,sizeof(value));
    }
   private:PhoneBleTransport* owner_;
  };
  class AllowlistCallbacks:public BLECharacteristicCallbacks{
   public:explicit AllowlistCallbacks(PhoneBleTransport* owner,bool config=false):owner_(owner),config_(config){}
    void onRead(BLECharacteristic* characteristic,PeerEvent* param)override{
#if defined(CONFIG_NIMBLE_ENABLED)
      const bool trusted=owner_&&param&&param->sec_state.encrypted&&param->sec_state.authenticated&&param->sec_state.key_size>=16&&owner_->isTrustedConnection_(param->conn_handle);
#else
      const bool trusted=owner_&&param&&owner_->isTrustedConnection_(param->read.conn_id);
#endif
      if(!trusted){characteristic->setValue("");return;}if(config_){char value[32];snprintf(value,sizeof(value),"1\n%s\n%u",owner_->platform_==1?"iphone":"android",unsigned(owner_->navEnabled_.load()));characteristic->setValue(value);return;}char allowlist[192];portENTER_CRITICAL(&owner_->peerMux_);memcpy(allowlist,owner_->allowlistCache_,sizeof(allowlist));portEXIT_CRITICAL(&owner_->peerMux_);characteristic->setValue(allowlist);}
   private:PhoneBleTransport* owner_;bool config_;
  };
  class RxCallbacks:public BLECharacteristicCallbacks{
   public:explicit RxCallbacks(PhoneBleTransport* owner,uint8_t kind=0):owner_(owner),kind_(kind){}
    void onWrite(BLECharacteristic*,PeerEvent*)override{/* Legacy payload writes are intentionally not dispatched. */}
   private:PhoneBleTransport* owner_;uint8_t kind_;
  };
  class ControlCallbacks:public BLECharacteristicCallbacks{
   public:explicit ControlCallbacks(PhoneBleTransport*owner):owner_(owner){}
    void onWrite(BLECharacteristic*characteristic,PeerEvent*param)override{
#if defined(CONFIG_NIMBLE_ENABLED)
      if(!owner_||!param||!param->sec_state.encrypted||!param->sec_state.authenticated||param->sec_state.key_size<16)return;const uint16_t connection=param->conn_handle;
#else
      if(!owner_||!param)return;const uint16_t connection=param->write.conn_id;
#endif
      if(!owner_->isTrustedConnection_(connection)||owner_->settings_&&owner_->settings_->phonePlatform)return;auto value=characteristic->getValue();if(!value.length())return;owner_->acceptControlFragment_(reinterpret_cast<const uint8_t*>(value.c_str()),value.length(),connection,millis());
    }
   private:PhoneBleTransport*owner_;
  };
  class EnvelopeCallbacks:public BLECharacteristicCallbacks{
   public:explicit EnvelopeCallbacks(PhoneBleTransport*owner):owner_(owner){}
    void onWrite(BLECharacteristic*characteristic,PeerEvent*param)override{
#if defined(CONFIG_NIMBLE_ENABLED)
      if(!owner_||!param||!param->sec_state.encrypted||!param->sec_state.authenticated||param->sec_state.key_size<16)return;const uint16_t connection=param->conn_handle;
#else
      if(!owner_||!param)return;const uint16_t connection=param->write.conn_id;
#endif
      if(!owner_->isTrustedConnection_(connection)||owner_->settings_&&owner_->settings_->phonePlatform)return;auto value=characteristic->getValue();if(!value.length())return;owner_->acceptEnvelopeFragment_(reinterpret_cast<const uint8_t*>(value.c_str()),value.length(),connection,millis());
    }
   private:PhoneBleTransport*owner_;
  };
  class SecurityCallbacks:public BLESecurityCallbacks{
   public:explicit SecurityCallbacks(PhoneBleTransport*o):owner_(o){}
#if defined(CONFIG_NIMBLE_ENABLED)
    void onPassKeyNotify(uint32_t passkey)override{esp_rom_printf("[BleHeap] passkey free=%u min=%u largest=%u\n",unsigned(ESP.getFreeHeap()),unsigned(ESP.getMinFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));if(owner_&&passkey<=999999u){owner_->passkeyWindowId_=owner_->pairingWindowId_.load();owner_->pendingPasskey_=passkey;}}
    void onAuthenticationComplete(ble_gap_conn_desc*d)override{esp_rom_printf("[BleHeap] auth free=%u min=%u largest=%u\n",unsigned(ESP.getFreeHeap()),unsigned(ESP.getMinFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));if(!d||owner_->candidateConnId_!=d->conn_handle)return;if(!d->sec_state.encrypted||!d->sec_state.bonded||!d->sec_state.authenticated||d->sec_state.key_size<16){if(d->sec_state.bonded)ble_store_util_delete_peer(&d->peer_id_addr);owner_->rejectConnId_=d->conn_handle;portENTER_CRITICAL(&owner_->peerMux_);if(owner_->candidateConnId_==d->conn_handle){owner_->candidateLease_.disconnected(d->conn_handle);owner_->candidateConnId_=-1;owner_->candidatePeer_[0]=0;owner_->candidateAuthenticated_=owner_->candidateBonded_=false;owner_->candidateBondAddrValid_=false;owner_->candidateKeyPrepared_=false;owner_->preparedCandidate_={};}portEXIT_CRITICAL(&owner_->peerMux_);owner_->passkeyUiPending_=owner_->passkeyUiQueued_=owner_->passkeyUiDisplayed_=false;return;}const auto&v=d->peer_id_addr.val;char mac[18];snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",v[5],v[4],v[3],v[2],v[1],v[0]);portENTER_CRITICAL(&owner_->peerMux_);const bool current=owner_->candidateConnId_==d->conn_handle&&owner_->candidateLease_.authenticated(d->conn_handle,mac);if(current){memcpy(owner_->candidatePeer_,mac,sizeof(mac));owner_->candidateBondAddr_=d->peer_id_addr;owner_->candidateBondAddrValid_=true;owner_->authenticatedWindowId_=owner_->pairingWindowId_.load();owner_->candidateAuthenticated_=true;}portEXIT_CRITICAL(&owner_->peerMux_);if(!current){ble_store_util_delete_peer(&d->peer_id_addr);owner_->rejectConnId_=d->conn_handle;}}
#endif
   private:PhoneBleTransport*owner_;
  };
  ServerCallbacks serverCallbacks_{this};
  SecurityCallbacks securityCallbacks_{this};
  IdentityCallbacks identityCallbacks_{this};
  RxCallbacks rxCallbacks_{this};
  AllowlistCallbacks allowlistCallbacks_{this},configCallbacks_{this,true};
  ControlCallbacks controlCallbacks_{this};
  EnvelopeCallbacks envelopeCallbacks_{this};
};

#else
#define ROBODESK_PHONE_BLE_AVAILABLE 0
class PhoneBleTransport {public:uint32_t hostStackMinimumFree()const{return 0;}const char*startupStage()const{return "disabled";}const char*startupFailureReason()const{return "not-ble-owner";}void startupBlocked(const char*){}using RemoteRevokePersist=bool(*)(void*);using CompanionCommandDispatch=bool(*)(uint32_t,uint32_t,uint32_t,const uint8_t*,size_t);struct CandidateApproval{uint32_t generation=0;uint16_t connection=0;char address[18]{};};bool end(){return true;}bool begin(PhoneNotificationBridge*,RuntimeSettings*,PhoneNavigationBridge* =nullptr){return false;}void setRemoteRevokePersist(RemoteRevokePersist,void* =nullptr){}void setCompanionCommandDispatch(CompanionCommandDispatch){}bool publishCompanionResult(uint32_t,uint32_t,uint8_t,uint8_t){return false;}bool sessionActive(uint32_t)const{return false;}bool connected()const{return false;}const char*ancsStatus()const{return "unsupported";}void openPairingWindow(uint32_t){}bool pairingOpen(uint32_t)const{return false;}bool activePairingPasskey(uint32_t,uint32_t&,uint32_t&)const{return false;}bool hasCandidate()const{return false;}bool snapshotCandidateApproval(CandidateApproval&)const{return false;}bool prepareCandidateApproval(const CandidateApproval&){return false;}bool nextPairingPasskey(uint32_t&,uint32_t&,uint16_t&){return false;}void pairingPasskeySendFailed(uint32_t){}void pairingPasskeyDisplayed(uint32_t){}const char*candidatePeer()const{return "";}bool approveCandidate(const CandidateApproval&){return false;}bool clearPeer(bool=false){return false;}void service(uint32_t){}};
#endif
