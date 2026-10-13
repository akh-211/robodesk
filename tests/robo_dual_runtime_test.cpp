#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <vector>
#include "Arduino.h"

bool mockHeapAllocationOK = true;
unsigned mockHeapAllocationCalls = 0;
size_t mockHeapAllocationBytes = 0;
unsigned mockHeapAllocationCaps = 0;

#define private public
#include "RoboDualRuntime.h"
#undef private

static void setMessage(robolink::Frame& frame, uint8_t type, uint8_t flags,
                       uint32_t id, const char* value, size_t length) {
  frame = {};
  frame.type = type;
  frame.flags = flags;
  frame.length = uint16_t(length + 4);
  robolink::put32(frame.payload, id);
  if (length) std::memcpy(frame.payload + 4, value, length);
}

static size_t setMessageFragment(robolink::Frame& frame,uint8_t type,uint32_t id,
                                 const std::vector<uint8_t>& data,size_t offset) {
  constexpr size_t MaxFragment=robolink::PayloadMax-4;
  const size_t length=std::min(MaxFragment,data.size()-offset);
  frame={};frame.type=type;frame.flags=offset==0?robolink::First:0;
  if(offset+length==data.size())frame.flags|=robolink::Last;
  frame.length=uint16_t(length+4);robolink::put32(frame.payload,id);
  if(length)std::memcpy(frame.payload+4,data.data()+offset,length);
  return length;
}

static void sendFullMessage(RoboDualRuntime& runtime,uint8_t type,uint32_t id,
                            const std::vector<uint8_t>& data,size_t startOffset=0) {
  size_t offset=startOffset;robolink::Frame frame;
  while(offset<data.size()){
    const size_t length=setMessageFragment(frame,type,id,data,offset);
    assert(runtime.receive(frame));offset+=length;
  }
}

static std::vector<uint8_t> fullMessage() {
  std::vector<uint8_t> data(RoboDualRuntime::MessageMax);
  for(size_t i=0;i<data.size();++i)data[i]=uint8_t(i*37u+11u);
  return data;
}

static void cleanup(RoboDualRuntime& runtime) {
  std::free(runtime.message_);
  runtime.message_ = nullptr;
  if (runtime.link.tx_) {
    vQueueDelete(runtime.link.tx_);
    runtime.link.tx_ = nullptr;
  }
  vSemaphoreDelete(runtime.tcpMutex_);
  vSemaphoreDelete(runtime.requestMutex_);
  vSemaphoreDelete(runtime.keyMutex_);
}

#if defined(CONFIG_IDF_TARGET_ESP32C3)
static RoboDualRuntime* settingsRuntime=nullptr;
static void injectSettingsReply(){
  robolink::Frame reply;reply.type=robolink::SettingsReply;reply.length=8;robolink::put32(reply.payload,settingsRuntime->settingsExpect_);
  reply.payload[4]=3;reply.payload[5]=0;reply.payload[6]=0xab;reply.payload[7]=0xcd;assert(settingsRuntime->receive(reply));
}
#endif

int main() {
  RoboDualRuntime runtime;
  assert(runtime.begin());
  assert(runtime.message_ == nullptr);
  assert(mockHeapAllocationCalls == 0);

  robolink::Frame openFrame;openFrame.type=robolink::TcpOpen;
  {const size_t n=robotunnel::encodeOpen(openFrame.payload,sizeof(openFrame.payload),42,"api.open-meteo.com",443,robotunnel::RawTcp);assert(n);openFrame.length=uint16_t(n);
   assert(runtime.receive(openFrame));assert(runtime.openPending.exchange(false));robotunnel::OpenRequest req;assert(runtime.takeOpenRequest(req));assert(!req.legacy&&req.generation==42&&req.port==443&&req.flags==robotunnel::RawTcp&&!strcmp(req.host,"api.open-meteo.com"));
   openFrame.length=4;robolink::put32(openFrame.payload,43);assert(runtime.receive(openFrame));assert(runtime.openPending.exchange(false));assert(runtime.takeOpenRequest(req)&&req.legacy&&req.generation==43);
   openFrame.length=9;assert(runtime.receive(openFrame));assert(!runtime.openPending.load());}
  {robolink::Frame k;k.type=robolink::GeminiKey;char out[RoboDualRuntime::GeminiKeyMax+1];
   assert(!runtime.geminiKeyAvailable()&&!runtime.copyGeminiKey(out,sizeof(out)));
   const char key[]="AIzaSy-test_KEY123";k.length=uint16_t(sizeof(key)-1);memcpy(k.payload,key,k.length);
   assert(runtime.receive(k)&&runtime.geminiKeyAvailable()&&runtime.copyGeminiKey(out,sizeof(out))&&!strcmp(out,key));
   assert(!runtime.copyGeminiKey(out,4));
   k.payload[3]='/';assert(runtime.receive(k)&&!runtime.geminiKeyAvailable());
   k.length=RoboDualRuntime::GeminiKeyMax+1;memset(k.payload,'a',k.length);assert(runtime.receive(k)&&!runtime.geminiKeyAvailable());
   k.length=3;memcpy(k.payload,"abc",3);assert(runtime.receive(k)&&runtime.geminiKeyAvailable());
   k.length=0;assert(runtime.receive(k)&&!runtime.geminiKeyAvailable());
   k.length=3;assert(runtime.receive(k));runtime.wipeGeminiKey();assert(!runtime.geminiKeyAvailable());}
  {// Key synced -> link reset the C3 never polls -> S3 wipes -> C3 resends -> S3 has the key again.
   auto& link=runtime.link;const char key[]="AIzaSy-test_KEY123";const uint32_t crc=robolink::crc32(reinterpret_cast<const uint8_t*>(key),sizeof(key)-1);
   robolink::Frame k;k.type=robolink::GeminiKey;k.length=uint16_t(sizeof(key)-1);memcpy(k.payload,key,k.length);char out[RoboDualRuntime::GeminiKeyMax+1];
   auto up=[&](uint32_t peer){link.peer_=peer;link.peerPublished_=peer;link.helloAck_=true;link.connected_=true;};
   RoboGeminiKeySync c3;RoboBoardLink::Health c3h{true,0x5300A,0,0,0,0,0,0};
   up(0xC300A);runtime.service();assert(c3.due(c3h,crc));assert(runtime.receive(k));c3.mark(c3h,crc);assert(!c3.due(c3h,crc));
   assert(runtime.copyGeminiKey(out,sizeof(out))&&!strcmp(out,key));
   link.lost(false);up(0xC300B);c3h.disconnects=1;// C3 rotated its session; S3 session (C3's peer) is unchanged.
   runtime.service();assert(!runtime.geminiKeyAvailable());
   assert(c3.due(c3h,crc));assert(runtime.receive(k));c3.mark(c3h,crc);assert(!c3.due(c3h,crc));
   runtime.service();assert(runtime.copyGeminiKey(out,sizeof(out))&&!strcmp(out,key));
   // A resend that lands before the S3 polls the reset survives the late wipe.
   link.lost(false);up(0xC300C);c3h.disconnects=2;assert(c3.due(c3h,crc));assert(runtime.receive(k));c3.mark(c3h,crc);
   runtime.service();assert(runtime.copyGeminiKey(out,sizeof(out))&&!strcmp(out,key));
   // Real loss with no resend still wipes.
   link.lost(false);runtime.service();assert(!runtime.geminiKeyAvailable()&&!runtime.copyGeminiKey(out,sizeof(out)));
   c3h.connected=false;assert(!c3.due(c3h,crc));
   // No key lock: the reliable frame is refused (the link retries it), never half-written.
   up(0xC300D);runtime.service();vSemaphoreDelete(runtime.keyMutex_);runtime.keyMutex_=nullptr;assert(!runtime.receive(k)&&!runtime.geminiKeyAvailable());runtime.keyMutex_=xSemaphoreCreateMutex();
   link.lost(false);runtime.service();}
  robolink::Frame frame;
  setMessage(frame, robolink::Reply, robolink::First | robolink::Last, 11, "ok", 2);
  assert(runtime.receive(frame));
  assert(mockHeapAllocationCalls == 1);
  assert(mockHeapAllocationBytes == RoboDualRuntime::MessageMax + 1);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  assert(mockHeapAllocationCaps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  assert(mockHeapAllocationCaps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
  assert(runtime.messageReady(robolink::Reply));
  assert(runtime.messageId() == 11 && runtime.messageSize() == 2);
  assert(std::memcmp(runtime.message(), "ok", 2) == 0 && runtime.message()[2] == 0);

  uint8_t* const retainedBuffer = runtime.message_;
  runtime.releaseMessage();
  setMessage(frame, robolink::Reply, robolink::First, 12, "hel", 3);
  assert(runtime.receive(frame));
  setMessage(frame, robolink::Reply, robolink::Last, 12, "lo", 2);
  assert(runtime.receive(frame));
  assert(runtime.message_ == retainedBuffer && mockHeapAllocationCalls == 1);
  assert(runtime.messageId() == 12 && runtime.messageSize() == 5);
  assert(std::memcmp(runtime.message(), "hello", 5) == 0 && runtime.message()[5] == 0);

  const auto maximum=fullMessage();
  runtime.releaseMessage();
  sendFullMessage(runtime,robolink::Reply,13,maximum);
  assert(runtime.messageReady(robolink::Reply));
  assert(runtime.messageId()==13&&runtime.messageSize()==RoboDualRuntime::MessageMax);
  assert(std::memcmp(runtime.message(),maximum.data(),maximum.size())==0);
  assert(runtime.message()[RoboDualRuntime::MessageMax]==0);
  cleanup(runtime);

  RoboDualRuntime outOfMemory;
  assert(outOfMemory.begin());
  robolink::Frame maximumFirst;setMessageFragment(maximumFirst,robolink::Reply,21,maximum,0);
  mockHeapAllocationOK = false;
  assert(!outOfMemory.receive(maximumFirst));
  assert(outOfMemory.message_ == nullptr && !outOfMemory.messageReady(robolink::Reply));

  auto continuation=maximumFirst;continuation.flags=robolink::Last;
  assert(!outOfMemory.receive(continuation));
  mockHeapAllocationOK = true;
  assert(outOfMemory.receive(maximumFirst)); // The reliable first fragment is retried.
  sendFullMessage(outOfMemory,robolink::Reply,21,maximum,robolink::PayloadMax-4);
  assert(outOfMemory.messageReady(robolink::Reply));
  assert(outOfMemory.messageId()==21&&outOfMemory.messageSize()==RoboDualRuntime::MessageMax);
  assert(std::memcmp(outOfMemory.message(),maximum.data(),maximum.size())==0);
  assert(outOfMemory.message()[RoboDualRuntime::MessageMax]==0);
  cleanup(outOfMemory);

  {// BLE settings mailbox: no storage of its own, bounded, refuses (retry) when full.
   RoboDualRuntime settings;assert(settings.begin());uint8_t box[RoboDualRuntime::SettingsFrameMax]{};
   robolink::Frame f;f.length=20;robolink::put32(f.payload,77);f.payload[4]=1;memset(f.payload+5,0x5a,15);
#if defined(CONFIG_IDF_TARGET_ESP32C3)
   f.type=robolink::SettingsReply;assert(settings.receive(f)&&!settings.settingsPending()); // no request pending: dropped
   settings.settingsBox_=box;settings.settingsCap_=sizeof(box);settings.settingsExpect_=76;
   assert(settings.receive(f)&&!settings.settingsPending()&&box[0]==0);                    // stale id: dropped
   settings.settingsExpect_=77;f.type=robolink::Settings;assert(settings.receive(f)&&!settings.settingsPending()); // C3 ignores requests
   {robolink::Frame pair;pair.type=robolink::PairingRequest;pair.length=1;assert(settings.receive(pair)&&!settings.pairingRequested); // malformed: dropped
    pair.length=0;assert(settings.receive(pair)&&settings.pairingRequested.exchange(false));}
   f.type=robolink::SettingsReply;assert(settings.receive(f)&&settings.settingsPending()==20&&box[5]==0x5a);
   assert(!settings.receive(f));                                                           // full: link retries
   settings.releaseSettings();settings.settingsCap_=10;assert(settings.receive(f)&&!settings.settingsPending()); // oversize for box
   settings.settingsCap_=sizeof(box);SemaphoreHandle_t m=settings.settingsMutex_;settings.settingsMutex_=nullptr;assert(!settings.receive(f));settings.settingsMutex_=m;
   settings.settingsBox_=nullptr;settings.settingsExpect_=0;
   size_t replySize=0;memset(box,0,sizeof(box));box[12+4]=3;
   assert(!settings.settingsTransact(box,sizeof(box),12,5,100,replySize)); // link down
   settings.link.peer_=0x5300F;settings.link.peerPublished_=0x5300F;settings.link.helloAck_=true;settings.link.connected_=true;
   settingsRuntime=&settings;mockBeforeQueueSend=injectSettingsReply;
   assert(settings.settingsTransact(box,sizeof(box),12,5,100,replySize)&&replySize==8&&box[6]==0xab&&box[7]==0xcd);
   assert(!settings.settingsBox_&&!settings.settingsExpect_&&!settings.settingsPending());
   const uint32_t started=millis();box[12+4]=3;assert(!settings.settingsTransact(box,sizeof(box),12,5,100,replySize)&&uint32_t(millis()-started)>=100); // no reply: bounded wait
   assert(!settings.settingsBox_&&!settings.settingsExpect_);
#else
   f.type=robolink::Settings;assert(settings.receive(f)&&!settings.settingsPending());     // no inbox yet: dropped
   settings.setSettingsInbox(box,sizeof(box));assert(settings.receive(f)&&settings.settingsPending()==20&&box[5]==0x5a);
   assert(!settings.receive(f));settings.releaseSettings();assert(!settings.settingsPending());
   f.type=robolink::SettingsReply;assert(settings.receive(f)&&!settings.settingsPending()); // S3 ignores replies
   f.type=robolink::Settings;f.length=RoboDualRuntime::SettingsFrameMax+1;assert(settings.receive(f)&&!settings.settingsPending());
   f.length=4;assert(settings.receive(f)&&!settings.settingsPending());
#endif
   cleanup(settings);vSemaphoreDelete(settings.settingsMutex_);}
}
