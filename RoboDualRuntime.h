#pragma once
#include "RoboBoardLink.h"
#include "RoboTunnelProtocol.h"
#include <esp_heap_caps.h>
#include <time.h>

// Shared byte tunnel and bounded RPC mailbox. Network parsers stay on the S3.
class RoboDualRuntime {
public:
  // Without the dashboard the C3 only issues fixed /_* RPCs and POST /companion/action, /factory. The largest S3 reply is
  // /_external/fetch/result: body <= 8192 (roboexternal::BodyCapacity / roboextfetch::BodyMax) + 3-byte header + "text/plain" = 8205 B.
  // Settings, memory and OTA status travel in <=512 B Settings frames; /_settings/get is <=4096 (PhoneSettingsProtocol::DocMax). Gateway-only: S3 keeps 12288.
#if ROBODESK_NO_DASHBOARD
  static constexpr size_t MessageMax=8208;
#else
  static constexpr size_t MessageMax=12288;
#endif
  static constexpr size_t TcpCapacity=4096,GeminiKeyMax=96,SettingsFrameMax=512;
  static constexpr uint32_t ClockFreshnessMs=5000u;
  RoboBoardLink link;
  std::atomic<bool> internet{false},keyConfigured{false},tcpConnected{false},openPending{false},closePending{false},tunnelBusy{false};
  std::atomic<uint32_t> tcpGeneration{0},clockEpoch{0},clockReceivedAt{0},streamRejected{0};
  std::atomic<uint8_t> phoneState{0};
  std::atomic<uint32_t> clockOwnerReceivedAt{0};
  // Fresh ownership is independent of whether NTP has supplied a valid epoch.
  bool clockFresh()const{const uint32_t at=clockOwnerReceivedAt.load();return at&&uint32_t(millis()-at)<ClockFreshnessMs;}
  std::atomic<uint32_t> pairingPasskey{0},pairingWindowId{0},pairingUntil{0},pairingDisplayedId{0},pairingAckMarkedId{0};
  std::atomic<uint8_t> pairState{0},pairStateEvent{0};std::atomic<uint32_t> pairStateUntil{0}; // C3 window state shown on the S3 (PairingState frame).
  std::atomic<bool> pairingRequested{false}; // S3 physical gesture asked the C3 to open a BLE pairing window.
  std::atomic<bool> pairingPasskeyPending{false},pairingPasskeyPresented{false},pairingPasskeyQueued{false},pairingPasskeyDisplayAckPending{false};
private:
  uint8_t* message_=nullptr;size_t assembling_=0;uint32_t assemblingId_=0;
  std::atomic<size_t> messageSize_{0};std::atomic<uint32_t> messageId_{0};std::atomic<uint8_t> messageType_{0};
  uint8_t tcp_[TcpCapacity]{};size_t head_=0,tail_=0,used_=0;
  SemaphoreHandle_t tcpMutex_=nullptr,requestMutex_=nullptr,keyMutex_=nullptr;uint32_t nextId_=1,lastPeer_=0;
  bool previousLink_=false;
  robotunnel::OpenRequest tunnelRequest_{};
  // Session key from the C3 over the wired link. RAM only; wiped on link loss, never persisted.
  // keyMutex_ guards the buffer; geminiKeyEpoch_ is the link disconnect count when the key arrived.
  char geminiKey_[GeminiKeyMax+1]{};std::atomic<uint8_t> geminiKeyLen_{0};uint32_t geminiKeyEpoch_=0;
  // BLE settings mailbox (no own storage): the C3 points it at the BLE exchange buffer for one
  // reply id at a time; the S3 points it at a static request inbox. settingsMutex_ guards the pointer.
  uint8_t* settingsBox_=nullptr;size_t settingsCap_=0;uint32_t settingsExpect_=0,settingsNextId_=0;std::atomic<uint16_t> settingsSize_{0};SemaphoreHandle_t settingsMutex_=nullptr;
  static bool receive(void*ctx,const robolink::Frame&f){return static_cast<RoboDualRuntime*>(ctx)->receive(f);}
  bool receive(const robolink::Frame&f){
    using namespace robolink;
    if(f.type==PairingPasskey){if(f.length!=10)return true;const uint32_t id=get32(f.payload),code=get32(f.payload+4);const uint16_t ttl=get16(f.payload+8);if(!id||code>999999u||ttl>60000u)return true;pairingWindowId=id;pairingPasskey=code;pairingUntil=ttl?millis()+ttl:0;pairingPasskeyPresented=false;pairingPasskeyQueued=false;pairingPasskeyDisplayAckPending=false;pairingAckMarkedId=0;pairingPasskeyPending=ttl!=0;return true;}
    if(f.type==PairingState){uint8_t st=0;uint16_t ttl=0;if(!ROBODESK_DUAL_GATEWAY&&decodePairingState(f.payload,f.length,st,ttl)){pairStateUntil=st==PairOpen?millis()+ttl:0;if(pairState.exchange(st)!=st)pairStateEvent=st;}return true;}
    if(f.type==PairingRequest){if(ROBODESK_DUAL_GATEWAY&&f.length==0)pairingRequested=true;return true;}
    if(f.type==PairingPasskeyDisplayed){if(f.length==4){const uint32_t id=get32(f.payload);if(id){pairingDisplayedId=id;pairingPasskeyPresented=true;}}return true;}
    if(f.type==GeminiKey){if(f.length>GeminiKeyMax)return true;if(!keyMutex_||xSemaphoreTake(keyMutex_,0)!=pdTRUE)return false;geminiKeyLen_=0;memset(geminiKey_,0,sizeof(geminiKey_));if(!f.length||robotunnel::validKey(reinterpret_cast<const char*>(f.payload),f.length)){memcpy(geminiKey_,f.payload,f.length);geminiKeyEpoch_=link.health().disconnects;geminiKeyLen_=uint8_t(f.length);}xSemaphoreGive(keyMutex_);return true;}
    if(f.type==(ROBODESK_DUAL_GATEWAY?SettingsReply:Settings)){
      if(f.length<5||f.length>SettingsFrameMax)return true;if(!settingsMutex_||xSemaphoreTake(settingsMutex_,0)!=pdTRUE)return false;bool accepted=true;
      const bool wanted=settingsBox_&&f.length<=settingsCap_&&(!ROBODESK_DUAL_GATEWAY||(settingsExpect_&&get32(f.payload)==settingsExpect_));
      if(wanted){if(settingsSize_.load(std::memory_order_acquire))accepted=false;else{memcpy(settingsBox_,f.payload,f.length);settingsSize_.store(f.length,std::memory_order_release);}}
      xSemaphoreGive(settingsMutex_);return accepted;
    }
    if(f.type==TcpOpen){robotunnel::OpenRequest r;if(!robotunnel::decodeOpen(f.payload,f.length,r))return true;tunnelRequest_=r;tcpGeneration=r.generation;clearTcp();tcpConnected=false;openPending=true;return true;}
    if(f.type==TcpClose&&f.length==4){if(get32(f.payload)==tcpGeneration.load()){tcpConnected=false;closePending=true;clearTcp();}return true;}
    if(f.type==TcpState&&f.length==5){if(get32(f.payload)==tcpGeneration.load()){tcpConnected=f.payload[4]!=0;if(!tcpConnected)clearTcp();}return true;}
    if(f.type==TcpData){
      if(f.length<4||get32(f.payload)!=tcpGeneration.load()||!tcpConnected)return true;
      if(xSemaphoreTake(tcpMutex_,0)!=pdTRUE)return false;
      const size_t n=f.length-4;if(n>TcpCapacity-used_){xSemaphoreGive(tcpMutex_);++streamRejected;return false;}
      for(size_t i=4;i<f.length;++i){tcp_[head_]=f.payload[i];head_=(head_+1)%TcpCapacity;}used_+=n;xSemaphoreGive(tcpMutex_);return true;
    }
    if(f.type==Clock&&f.length==10){clockOwnerReceivedAt=millis();const uint32_t epoch=get32(f.payload);if(epoch>=1700000000u){clockEpoch=epoch;clockReceivedAt=millis();}internet=f.payload[4]!=0;keyConfigured=f.payload[5]!=0;phoneState=f.payload[6];return true;}
    if(f.type==Rpc||f.type==Reply){
      if(f.length<4)return true;const uint32_t id=get32(f.payload);
      if(f.flags&First){if(messageType_.load())return false;if(!message_){message_=static_cast<uint8_t*>(heap_caps_malloc(MessageMax+1,ROBODESK_DUAL_ROBOT?(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));if(!message_)return false;}assembling_=0;assemblingId_=id;}
      if(!message_)return false;
      const size_t payloadLength=size_t(f.length)-4;
      if(id!=assemblingId_||payloadLength>MessageMax-assembling_)return true;
      memcpy(message_+assembling_,f.payload+4,payloadLength);assembling_+=payloadLength;
      if(f.flags&Last){message_[assembling_]=0;messageSize_=assembling_;messageId_=id;messageType_.store(f.type,std::memory_order_release);}return true;
    }
    return true;
  }
public:
  bool begin(){
    tcpMutex_=xSemaphoreCreateMutex();requestMutex_=xSemaphoreCreateMutex();keyMutex_=xSemaphoreCreateMutex();settingsMutex_=xSemaphoreCreateMutex();if(!tcpMutex_||!requestMutex_||!keyMutex_||!settingsMutex_)return false;
    return link.begin(receive,this);
  }
  void service(){const auto h=link.health();if(previousLink_&&(!h.connected||h.peerSession!=lastPeer_)){tcpConnected=false;internet=false;keyConfigured=false;wipeGeminiKey(true);clockReceivedAt=0;clockOwnerReceivedAt=0;phoneState=0;openPending=false;closePending=true;clearTcp();messageType_=0;pairingAckMarkedId=0;pairingPasskeyPresented=false;pairingPasskeyQueued=false;pairingPasskeyPending=pairingUntil.load()!=0;}previousLink_=h.connected;lastPeer_=h.peerSession;}
  // Copy after openPending fires; a newer open replaces the request, so a generation mismatch means retry on the next open.
  bool takeOpenRequest(robotunnel::OpenRequest&r){r=tunnelRequest_;return r.generation&&r.generation==tcpGeneration.load();}
  bool geminiKeyAvailable()const{return geminiKeyLen_.load()!=0;}
  // Copies under keyMutex_ so a concurrent receive or wipe cannot tear the key.
  bool copyGeminiKey(char*out,size_t cap){if(!out||!keyMutex_||xSemaphoreTake(keyMutex_,pdMS_TO_TICKS(5))!=pdTRUE)return false;const uint8_t n=geminiKeyLen_.load();const bool ok=n&&cap>=size_t(n)+1u;if(ok){memcpy(out,geminiKey_,n);out[n]=0;}xSemaphoreGive(keyMutex_);return ok;}
  // keepFresh: keep a key received after the latest link loss (a resend that beat this poll).
  void wipeGeminiKey(bool keepFresh=false){const bool locked=keyMutex_&&xSemaphoreTake(keyMutex_,portMAX_DELAY)==pdTRUE;const auto h=link.health();if(!(keepFresh&&geminiKeyLen_.load()&&h.connected&&geminiKeyEpoch_==h.disconnects)){geminiKeyLen_=0;memset(geminiKey_,0,sizeof(geminiKey_));}if(locked)xSemaphoreGive(keyMutex_);}
  void clearTcp(){if(tcpMutex_&&xSemaphoreTake(tcpMutex_,pdMS_TO_TICKS(10))==pdTRUE){head_=tail_=used_=0;xSemaphoreGive(tcpMutex_);}}
  size_t tcpAvailable(){if(xSemaphoreTake(tcpMutex_,pdMS_TO_TICKS(5))!=pdTRUE)return 0;const size_t n=used_;xSemaphoreGive(tcpMutex_);return n;}
  size_t readTcp(uint8_t*out,size_t cap){if(!out||xSemaphoreTake(tcpMutex_,pdMS_TO_TICKS(5))!=pdTRUE)return 0;const size_t n=used_<cap?used_:cap;for(size_t i=0;i<n;++i){out[i]=tcp_[tail_];tail_=(tail_+1)%TcpCapacity;}used_-=n;xSemaphoreGive(tcpMutex_);return n;}
  size_t writeTcp(const uint8_t*p,size_t n,uint32_t budgetMs){size_t sent=0;uint32_t started=millis();uint8_t chunk[robolink::PayloadMax];robolink::put32(chunk,tcpGeneration.load());
    while(sent<n&&tcpConnected&&link.connected()){size_t k=n-sent;if(k>sizeof(chunk)-4)k=sizeof(chunk)-4;memcpy(chunk+4,p+sent,k);if(link.send(robolink::TcpData,chunk,k+4,0,1))sent+=k;else if(uint32_t(millis()-started)>=budgetMs)break;else delay(1);}return sent;
  }
  bool sendMessage(uint8_t type,uint32_t id,const uint8_t*p,size_t n,uint32_t budgetMs=5000){if(n>MessageMax)return false;size_t off=0;uint32_t started=millis();do{uint8_t chunk[robolink::PayloadMax];robolink::put32(chunk,id);size_t k=n-off;if(k>sizeof(chunk)-4)k=sizeof(chunk)-4;if(k)memcpy(chunk+4,p+off,k);const uint8_t flags=(off==0?robolink::First:0)|((off+k==n)?robolink::Last:0);while(!link.send(type,chunk,k+4,flags,1)){if(!link.connected()||uint32_t(millis()-started)>=budgetMs)return false;feedLoopWDT();delay(1);}off+=k;}while(off<n);return true;}
  bool messageReady(uint8_t type)const{return messageType_.load(std::memory_order_acquire)==type;}
  const uint8_t* message()const{return message_;}size_t messageSize()const{return messageSize_.load();}uint32_t messageId()const{return messageId_.load();}
  void releaseMessage(){messageType_.store(0,std::memory_order_release);}
  bool request(const uint8_t*p,size_t n,uint32_t timeout=5000){if(xSemaphoreTake(requestMutex_,pdMS_TO_TICKS(timeout))!=pdTRUE)return false;releaseMessage();nextId_=nextId_==0xffffffffu?1:nextId_+1;const uint32_t id=nextId_;const uint32_t peer=link.health().peerSession;bool ok=sendMessage(robolink::Rpc,id,p,n,timeout);const uint32_t started=millis();while(ok){if(!link.connected()||link.health().peerSession!=peer||uint32_t(millis()-started)>=timeout){ok=false;break;}if(messageReady(robolink::Reply)){if(messageId()==id)break;releaseMessage();}feedLoopWDT();delay(1);}if(!ok){releaseMessage();xSemaphoreGive(requestMutex_);}return ok;}
  void finishRequest(){releaseMessage();
#if !ROBODESK_DUAL_ROBOT
    if(message_){free(message_);message_=nullptr;assembling_=0;}
#endif
    xSemaphoreGive(requestMutex_);}
  // S3: requests from the C3 land in this inbox; process from the main loop, then releaseSettings().
  void setSettingsInbox(uint8_t*box,size_t cap){if(!settingsMutex_||xSemaphoreTake(settingsMutex_,portMAX_DELAY)!=pdTRUE)return;settingsBox_=box;settingsCap_=box?cap:0;settingsExpect_=0;settingsSize_=0;xSemaphoreGive(settingsMutex_);}
  size_t settingsPending()const{return settingsSize_.load(std::memory_order_acquire);}
  void releaseSettings(){settingsSize_.store(0,std::memory_order_release);}
  // C3: send box[offset..offset+size) (id u32 is assigned here, op at +4) and wait for the reply,
  // which is written to box[0..replySize). Bypasses the 12 KB RPC buffer entirely.
  bool settingsTransact(uint8_t*box,size_t cap,size_t offset,size_t size,uint32_t timeoutMs,size_t&replySize){
    replySize=0;if(!box||size<5||offset+size>cap||cap>SettingsFrameMax||!settingsMutex_||!link.connected())return false;const uint8_t op=box[offset+4];const uint32_t peer=link.health().peerSession;
    if(xSemaphoreTake(settingsMutex_,pdMS_TO_TICKS(50))!=pdTRUE)return false;settingsNextId_=settingsNextId_==0xffffffffu?1:settingsNextId_+1;const uint32_t id=settingsNextId_;robolink::put32(box+offset,id);
    settingsBox_=box;settingsCap_=cap;settingsExpect_=id;settingsSize_=0;xSemaphoreGive(settingsMutex_);
    bool ok=link.send(robolink::Settings,box+offset,size,0,50);const uint32_t started=millis();
    while(ok&&!settingsSize_.load(std::memory_order_acquire)){if(!link.connected()||link.health().peerSession!=peer||uint32_t(millis()-started)>=timeoutMs){ok=false;break;}delay(1);}
    xSemaphoreTake(settingsMutex_,portMAX_DELAY);const size_t n=settingsSize_.load();settingsBox_=nullptr;settingsCap_=0;settingsExpect_=0;settingsSize_=0;xSemaphoreGive(settingsMutex_);
    if(!ok||n<6||robolink::get32(box)!=id||box[4]!=op)return false;replySize=n;return true;
  }
  time_t epoch()const{const uint32_t e=clockEpoch.load(),received=clockReceivedAt.load(),age=uint32_t(millis()-received);return e>=1700000000u&&age<=ClockFreshnessMs?time_t(e+age/1000u):0;}
  bool takePairingPasskey(uint32_t&code,uint32_t&windowId,uint32_t&remainingMs){if(!pairingPasskeyPending.load()||pairingPasskeyQueued.exchange(true))return false;code=pairingPasskey.load();windowId=pairingWindowId.load();remainingMs=pairingUntil.load()-millis();if(!remainingMs||remainingMs>60000u){pairingPasskeyQueued=false;pairingPasskeyPending=false;return false;}return true;}
  void pairingPasskeySendFailed(){pairingPasskeyQueued=false;}
  bool pairingPasskeyActive(uint32_t now,uint32_t&code,uint32_t&windowId){const uint32_t until=pairingUntil.load();if(!pairingPasskeyPending.load()||!until||int32_t(until-now)<=0)return false;code=pairingPasskey.load();windowId=pairingWindowId.load();return windowId&&code<=999999u;}
  bool takePairingStateEvent(uint8_t&state){state=pairStateEvent.exchange(0);return state!=0;}
  uint32_t pairingOpenRemainingMs(uint32_t now)const{if(pairState.load()!=robolink::PairOpen)return 0;const int32_t left=int32_t(pairStateUntil.load()-now);return left>0?uint32_t(left):0;}
  bool takePairingPasskeyPresented(uint32_t&windowId){if(!pairingPasskeyPresented.exchange(false))return false;windowId=pairingDisplayedId.load();return windowId!=0;}
  void markPairingPasskeyPresented(uint32_t id){if(id&&id==pairingWindowId.load()&&pairingAckMarkedId.exchange(id)!=id)pairingPasskeyDisplayAckPending=true;}
  bool takePairingPasskeyDisplayAck(uint32_t&id){if(!pairingPasskeyDisplayAckPending.exchange(false))return false;id=pairingWindowId.load();return id!=0;}
  void retryPairingPasskeyDisplayAck(uint32_t id){if(id&&id==pairingWindowId.load())pairingPasskeyDisplayAckPending=true;}
  void clearPairingPasskey(){pairingPasskeyPending=false;pairingPasskeyPresented=false;pairingPasskeyQueued=false;pairingPasskeyDisplayAckPending=false;pairingUntil=0;pairingPasskey=0;pairingWindowId=0;pairingAckMarkedId=0;}
};
inline RoboDualRuntime RoboDual;
// C3 key sync state: resend after any link loss (even one too short to poll), peer change or key change.
struct RoboGeminiKeySync {
  bool sent=false;uint32_t session=0,crc=0,disconnects=0;
  bool due(const RoboBoardLink::Health&h,uint32_t keyCrc)const{return h.connected&&(!sent||h.peerSession!=session||h.disconnects!=disconnects||keyCrc!=crc);}
  void mark(const RoboBoardLink::Health&h,uint32_t keyCrc){sent=true;session=h.peerSession;crc=keyCrc;disconnects=h.disconnects;}
};
