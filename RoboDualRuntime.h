#pragma once
#include "RoboBoardLink.h"
#include "RoboTunnelProtocol.h"
#include <esp_heap_caps.h>
#include <time.h>

// Shared byte tunnel and bounded RPC mailbox. Network parsers stay on the S3.
class RoboDualRuntime {
public:
  static constexpr size_t MessageMax=12288,TcpCapacity=4096,GeminiKeyMax=96;
  static constexpr uint32_t ClockFreshnessMs=5000u;
  RoboBoardLink link;
  std::atomic<bool> internet{false},keyConfigured{false},tcpConnected{false},openPending{false},closePending{false},tunnelBusy{false};
  std::atomic<uint32_t> tcpGeneration{0},clockEpoch{0},clockReceivedAt{0},streamRejected{0};
  std::atomic<uint8_t> phoneState{0};
  std::atomic<uint32_t> clockOwnerReceivedAt{0};
  // Fresh ownership is independent of whether NTP has supplied a valid epoch.
  bool clockFresh()const{const uint32_t at=clockOwnerReceivedAt.load();return at&&uint32_t(millis()-at)<ClockFreshnessMs;}
  std::atomic<uint32_t> pairingPasskey{0},pairingWindowId{0},pairingUntil{0},pairingDisplayedId{0},pairingAckMarkedId{0};
  std::atomic<bool> pairingPasskeyPending{false},pairingPasskeyPresented{false},pairingPasskeyQueued{false},pairingPasskeyDisplayAckPending{false};
private:
  uint8_t* message_=nullptr;size_t assembling_=0;uint32_t assemblingId_=0;
  std::atomic<size_t> messageSize_{0};std::atomic<uint32_t> messageId_{0};std::atomic<uint8_t> messageType_{0};
  uint8_t tcp_[TcpCapacity]{};size_t head_=0,tail_=0,used_=0;
  SemaphoreHandle_t tcpMutex_=nullptr,requestMutex_=nullptr;uint32_t nextId_=1,lastPeer_=0;
  bool previousLink_=false;
  robotunnel::OpenRequest tunnelRequest_{};
  // Session key from the C3 over the wired link. RAM only; wiped on link loss, never persisted.
  char geminiKey_[GeminiKeyMax+1]{};std::atomic<uint8_t> geminiKeyLen_{0};
  static bool receive(void*ctx,const robolink::Frame&f){return static_cast<RoboDualRuntime*>(ctx)->receive(f);}
  bool receive(const robolink::Frame&f){
    using namespace robolink;
    if(f.type==PairingPasskey){if(f.length!=10)return true;const uint32_t id=get32(f.payload),code=get32(f.payload+4);const uint16_t ttl=get16(f.payload+8);if(!id||code>999999u||ttl>60000u)return true;pairingWindowId=id;pairingPasskey=code;pairingUntil=ttl?millis()+ttl:0;pairingPasskeyPresented=false;pairingPasskeyQueued=false;pairingPasskeyDisplayAckPending=false;pairingAckMarkedId=0;pairingPasskeyPending=ttl!=0;return true;}
    if(f.type==PairingPasskeyDisplayed){if(f.length==4){const uint32_t id=get32(f.payload);if(id){pairingDisplayedId=id;pairingPasskeyPresented=true;}}return true;}
    if(f.type==GeminiKey){if(f.length>GeminiKeyMax)return true;geminiKeyLen_=0;if(f.length&&!robotunnel::validKey(reinterpret_cast<const char*>(f.payload),f.length))return true;memcpy(geminiKey_,f.payload,f.length);geminiKey_[f.length]=0;geminiKeyLen_=uint8_t(f.length);return true;}
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
    tcpMutex_=xSemaphoreCreateMutex();requestMutex_=xSemaphoreCreateMutex();if(!tcpMutex_||!requestMutex_)return false;
    return link.begin(receive,this);
  }
  void service(){const auto h=link.health();if(previousLink_&&(!h.connected||h.peerSession!=lastPeer_)){tcpConnected=false;internet=false;keyConfigured=false;wipeGeminiKey();clockReceivedAt=0;clockOwnerReceivedAt=0;phoneState=0;openPending=false;closePending=true;clearTcp();messageType_=0;pairingAckMarkedId=0;pairingPasskeyPresented=false;pairingPasskeyQueued=false;pairingPasskeyPending=pairingUntil.load()!=0;}previousLink_=h.connected;lastPeer_=h.peerSession;}
  // Copy after openPending fires; a newer open replaces the request, so a generation mismatch means retry on the next open.
  bool takeOpenRequest(robotunnel::OpenRequest&r){r=tunnelRequest_;return r.generation&&r.generation==tcpGeneration.load();}
  bool geminiKeyAvailable()const{return geminiKeyLen_.load()!=0;}
  // Copies under a length check; fails if the key changes or is wiped mid-copy.
  bool copyGeminiKey(char*out,size_t cap){const uint8_t n=geminiKeyLen_.load();if(!out||!n||cap<size_t(n)+1u)return false;memcpy(out,geminiKey_,n);out[n]=0;if(geminiKeyLen_.load()!=n||memcmp(out,geminiKey_,n)){memset(out,0,n);out[0]=0;return false;}return true;}
  void wipeGeminiKey(){geminiKeyLen_=0;memset(geminiKey_,0,sizeof(geminiKey_));}
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
  time_t epoch()const{const uint32_t e=clockEpoch.load(),received=clockReceivedAt.load(),age=uint32_t(millis()-received);return e>=1700000000u&&age<=ClockFreshnessMs?time_t(e+age/1000u):0;}
  bool takePairingPasskey(uint32_t&code,uint32_t&windowId,uint32_t&remainingMs){if(!pairingPasskeyPending.load()||pairingPasskeyQueued.exchange(true))return false;code=pairingPasskey.load();windowId=pairingWindowId.load();remainingMs=pairingUntil.load()-millis();if(!remainingMs||remainingMs>60000u){pairingPasskeyQueued=false;pairingPasskeyPending=false;return false;}return true;}
  void pairingPasskeySendFailed(){pairingPasskeyQueued=false;}
  bool pairingPasskeyActive(uint32_t now,uint32_t&code,uint32_t&windowId){const uint32_t until=pairingUntil.load();if(!pairingPasskeyPending.load()||!until||int32_t(until-now)<=0)return false;code=pairingPasskey.load();windowId=pairingWindowId.load();return windowId&&code<=999999u;}
  bool takePairingPasskeyPresented(uint32_t&windowId){if(!pairingPasskeyPresented.exchange(false))return false;windowId=pairingDisplayedId.load();return windowId!=0;}
  void markPairingPasskeyPresented(uint32_t id){if(id&&id==pairingWindowId.load()&&pairingAckMarkedId.exchange(id)!=id)pairingPasskeyDisplayAckPending=true;}
  bool takePairingPasskeyDisplayAck(uint32_t&id){if(!pairingPasskeyDisplayAckPending.exchange(false))return false;id=pairingWindowId.load();return id!=0;}
  void retryPairingPasskeyDisplayAck(uint32_t id){if(id&&id==pairingWindowId.load())pairingPasskeyDisplayAckPending=true;}
  void clearPairingPasskey(){pairingPasskeyPending=false;pairingPasskeyPresented=false;pairingPasskeyQueued=false;pairingPasskeyDisplayAckPending=false;pairingUntil=0;pairingPasskey=0;pairingWindowId=0;pairingAckMarkedId=0;}
};
inline RoboDualRuntime RoboDual;
