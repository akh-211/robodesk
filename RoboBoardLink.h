#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>
#include <new>
#include "RoboBoardProfile.h"
#include "RoboLinkProtocol.h"

// This task is the sole UART owner. Callbacks must be bounded and nonblocking.
class RoboBoardLink {
public:
  using Receiver=bool(*)(void*,const robolink::Frame&);
  struct Health {bool connected;uint32_t peerSession,ageMs,received,sent,retries,corrupt,disconnects;};
private:
  HardwareSerial uart_{1};TaskHandle_t taskHandle_=nullptr;QueueHandle_t tx_=nullptr;Receiver receiver_=nullptr;void* context_=nullptr;
  robolink::Parser parser_;uint32_t session_=0,peer_=0,lastHello_=0,lastHeartbeat_=0,lastRx_=0,waitingAt_=0,waitingSince_=0;
  uint16_t sequence_=1,expected_=1,lastAccepted_=0;uint8_t attempts_=0;std::atomic<bool> waiting_{false};bool helloAck_=false;
  robolink::Frame pending_;std::atomic<bool> connected_{false};std::atomic<uint32_t> peerPublished_{0},rxPublished_{0},received_{0},sent_{0},retries_{0},disconnects_{0},corrupt_{0};
  std::atomic<uint32_t> txQueueHighWater_{0};
  // Queued frames own only their payload bytes (a 10-byte Clock costs 10 B, not a 1036 B Frame): cuts heap churn/fragmentation on the C3.
  struct QueuedFrame { uint8_t* payload; uint32_t generation; uint16_t length; uint8_t type, flags; };
  std::atomic<uint32_t> txGeneration_{1};
  void discardQueuedFrames(){QueuedFrame item{};while(tx_&&xQueueReceive(tx_,&item,0)==pdTRUE)free(item.payload);}
  bool takeQueuedFrame(robolink::Frame& frame){QueuedFrame item{};while(tx_&&xQueueReceive(tx_,&item,0)==pdTRUE){const bool current=item.generation==txGeneration_.load();if(current){frame.type=item.type;frame.flags=item.flags;frame.length=item.length;if(item.length)memcpy(frame.payload,item.payload,item.length);}free(item.payload);if(current)return true;}return false;}
  uint8_t wire_[robolink::WireMax]{};
  void writeFrame(const robolink::Frame&f){const size_t n=robolink::encode(f,wire_,sizeof(wire_));if(n){uart_.write(wire_,n);++sent_;}}
  void control(uint8_t type,const uint8_t*p=nullptr,size_t n=0,uint16_t seq=0){robolink::Frame f;f.type=type;f.session=session_;f.sequence=seq;f.length=uint16_t(n);if(n)memcpy(f.payload,p,n);writeFrame(f);}
  void lost(bool rotate=true){if(connected_.exchange(false))++disconnects_;++txGeneration_;peerPublished_=0;helloAck_=false;peer_=0;waiting_=false;expected_=sequence_=1;lastAccepted_=0;parser_.reset();discardQueuedFrames();if(rotate){session_=esp_random();if(!session_)session_=1;}lastHello_=0;}
  void accept(const robolink::Frame&f,uint32_t now){
    if(f.type==robolink::Hello){
      if(f.length!=2||f.payload[0]!=(ROBODESK_DUAL_GATEWAY?2:1)||f.payload[1]!=robolink::Version||!f.session)return;
      if(peer_&&peer_!=f.session)lost(false);peer_=f.session;peerPublished_=peer_;lastRx_=now;rxPublished_=now;
      uint8_t id[4];robolink::put32(id,f.session);control(robolink::HelloAck,id,4);return;
    }
    if(f.type==robolink::HelloAck){if(f.length==4&&robolink::get32(f.payload)==session_&&peer_==f.session){helloAck_=true;connected_=true;lastRx_=now;rxPublished_=now;}return;}
    if(!helloAck_||f.session!=peer_)return;
    lastRx_=now;rxPublished_=now;
    if(f.type==robolink::Heartbeat)return;
    if(f.type==robolink::Ack){if(waiting_&&f.sequence==pending_.sequence){waiting_=false;attempts_=0;sequence_=robolink::nextSequence(sequence_);}return;}
    if(f.flags&robolink::Reliable){
      if(f.sequence==lastAccepted_){control(robolink::Ack,nullptr,0,f.sequence);return;}
      if(f.sequence!=expected_)return;
    }
    if(receiver_&&!receiver_(context_,f))return;
    ++received_;
    if(f.flags&robolink::Reliable){lastAccepted_=f.sequence;expected_=robolink::nextSequence(expected_);control(robolink::Ack,nullptr,0,f.sequence);}
  }
  static void task(void*p){auto*self=static_cast<RoboBoardLink*>(p);robolink::Frame incoming;
    for(;;){unsigned budget=4096;while(budget--&&self->uart_.available()){if(self->parser_.feed(uint8_t(self->uart_.read()),incoming))self->accept(incoming,millis());}self->corrupt_=self->parser_.corrupt+self->parser_.oversize;
      self->tick(millis());vTaskDelay(1);
    }
  }
  // Liveness = any valid peer frame (heartbeat/data/Ack) within 3 s. A missing Ack
  // alone only backs off retries (200 ms x5, then 1 s) up to a 10 s stall cap.
  void tick(uint32_t now){
      if(peer_&&uint32_t(now-lastRx_)>=3000u)lost();
      if(uint32_t(now-lastHello_)>=1000u){lastHello_=now;uint8_t hello[]={uint8_t(ROBODESK_DUAL_GATEWAY?1:2),robolink::Version};control(robolink::Hello,hello,sizeof(hello));}
      if(!connected_)discardQueuedFrames();
      if(connected_){
        if(uint32_t(now-lastHeartbeat_)>=1000u){lastHeartbeat_=now;control(robolink::Heartbeat);}
        if(waiting_&&uint32_t(now-waitingAt_)>=(attempts_<5?200u:1000u)){if(uint32_t(now-waitingSince_)>=10000u){lost();}else{if(attempts_<255)++attempts_;waitingAt_=now;++retries_;writeFrame(pending_);}}
        if(!waiting_&&takeQueuedFrame(pending_)){pending_.session=session_;pending_.sequence=sequence_;waiting_=true;waitingAt_=waitingSince_=now;attempts_=0;writeFrame(pending_);}
      }
  }
public:
  bool begin(Receiver receiver,void*context){receiver_=receiver;context_=context;session_=esp_random();if(!session_)session_=1;tx_=xQueueCreate(8,sizeof(QueuedFrame));if(!tx_)return false;uart_.setRxBufferSize(4096);uart_.setTxBufferSize(2048);uart_.begin(921600,SERIAL_8N1,ROBODESK_LINK_RX,ROBODESK_LINK_TX);if(xTaskCreatePinnedToCore(task,"rdBoardLink",6144,this,3,&taskHandle_,0)==pdPASS)return true;vQueueDelete(tx_);tx_=nullptr;return false;}
  uint32_t stackMinimumFree()const{return taskHandle_?uint32_t(uxTaskGetStackHighWaterMark(taskHandle_)):0;}
  bool connected()const{return connected_.load();}
  bool send(uint8_t type,const uint8_t*p,size_t n,uint8_t flags=0,uint32_t waitMs=0){const uint32_t generation=txGeneration_.load();if(!connected()||n>robolink::PayloadMax||(!p&&n))return false;uint8_t*copy=nullptr;if(n){copy=static_cast<uint8_t*>(malloc(n));if(!copy)return false;memcpy(copy,p,n);}const QueuedFrame item{copy,generation,uint16_t(n),type,uint8_t(flags|robolink::Reliable)};if(connected()&&generation==txGeneration_.load()&&xQueueSend(tx_,&item,pdMS_TO_TICKS(waitMs))==pdTRUE){const uint32_t depth=uxQueueMessagesWaiting(tx_);uint32_t high=txQueueHighWater_.load();while(depth>high&&!txQueueHighWater_.compare_exchange_weak(high,depth)){}return true;}free(copy);return false;}
  uint32_t queuedFrames()const{return tx_?uint32_t(uxQueueMessagesWaiting(tx_)):0;}
  uint32_t queuedFramesHighWater()const{return txQueueHighWater_.load();}
  bool idle()const{return tx_&&uxQueueMessagesWaiting(tx_)==0&&!waiting_;}
  Health health()const{return {connected(),peerPublished_.load(),uint32_t(millis()-rxPublished_.load()),received_.load(),sent_.load(),retries_.load(),corrupt_.load(),disconnects_.load()};}
};
