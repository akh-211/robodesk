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
}

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
}
