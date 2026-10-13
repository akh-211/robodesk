#include <cassert>
#include <cstdlib>
#include <new>
#include <vector>
#include <atomic>
#include <algorithm>
#include <string>
#include "Arduino.h"
static bool failFrameAllocation=false;
static unsigned frameAllocations=0,frameReleases=0;
static void* framePointers[16]{};
static constexpr std::size_t FrameAllocationBytes=1024; // Queued frames now own only their payload bytes; full-size RPC payload = PayloadMax.
static void* trackedMalloc(std::size_t n){
  if(n!=FrameAllocationBytes)return std::malloc(n);
  if(failFrameAllocation)return nullptr;
  void*p=std::malloc(n);if(p)for(void*&slot:framePointers)if(!slot){slot=p;++frameAllocations;break;}
  return p;
}
static void trackedFree(void*p){
  if(!p)return;
  for(void*&slot:framePointers)if(slot==p){slot=nullptr;++frameReleases;break;}
  std::free(p);
}
#define malloc trackedMalloc
#define free trackedFree
#define private public
#include "RoboBoardLink.h"
#undef private

static RoboBoardLink* activeLink=nullptr;
static void staleProducer(){activeLink->lost();activeLink->connected_=true;}

int main(){
  RoboBoardLink link;activeLink=&link;
  mockQueueCreateOK=false;assert(!link.begin(nullptr,nullptr));assert(link.tx_==nullptr);mockQueueCreateOK=true;
  mockTaskOK=false;assert(!link.begin(nullptr,nullptr));assert(link.tx_==nullptr);mockTaskOK=true;
  assert(link.begin(nullptr,nullptr));assert(link.tx_->capacity==8&&link.tx_->itemSize==sizeof(RoboBoardLink::QueuedFrame));assert(link.tx_->itemSize<sizeof(robolink::Frame));

  robolink::Frame source;source.type=robolink::Rpc;source.length=robolink::PayloadMax;
  for(size_t i=0;i<source.length;++i)source.payload[i]=uint8_t(i*17);
  assert(!link.send(source.type,source.payload,source.length));assert(frameAllocations==0);
  link.connected_=true;
  for(unsigned i=0;i<8;++i){source.sequence=uint16_t(i+1);assert(link.send(source.type,source.payload,source.length));}
  assert(frameAllocations==8&&frameReleases==0&&uxQueueMessagesWaiting(link.tx_)==8);assert(link.queuedFrames()==8&&link.queuedFramesHighWater()==8);
  memset(source.payload,0,source.length);
  auto* first=reinterpret_cast<RoboBoardLink::QueuedFrame*>(link.tx_->items.front().data());
  assert(first->payload[17]==uint8_t(17*17));
  assert(!link.send(source.type,source.payload,source.length));assert(frameAllocations==9&&frameReleases==1);
  link.lost();assert(!link.connected_&&uxQueueMessagesWaiting(link.tx_)==0&&frameReleases==9);

  link.connected_=true;failFrameAllocation=true;assert(!link.send(source.type,source.payload,source.length));assert(uxQueueMessagesWaiting(link.tx_)==0);failFrameAllocation=false;
  assert(link.send(source.type,source.payload,source.length));const unsigned beforeRace=frameAllocations;
  mockBeforeQueueSend=staleProducer;assert(link.send(source.type,source.payload,source.length));
  assert(uxQueueMessagesWaiting(link.tx_)==1);assert(!link.takeQueuedFrame(link.pending_));assert(uxQueueMessagesWaiting(link.tx_)==0);
  assert(frameAllocations==beforeRace+1&&frameReleases==frameAllocations);

  link.connected_=true;assert(link.send(source.type,source.payload,source.length));
  assert(link.takeQueuedFrame(link.pending_));assert(link.pending_.length==robolink::PayloadMax&&link.pending_.payload[17]==0);
  assert(frameReleases==frameAllocations);

  // Busy-but-alive peer: heartbeats keep arriving, Ack is delayed past 1.2 s.
  auto establish=[&](uint32_t now){link.lost();link.peer_=0xABCD1234u;link.helloAck_=true;link.connected_=true;link.lastRx_=link.lastHello_=link.lastHeartbeat_=now;};
  auto peerFrame=[&](uint8_t type,uint16_t seq,uint32_t now){robolink::Frame f;f.type=type;f.session=link.peer_;f.sequence=seq;link.accept(f,now);};
  uint32_t t=50000;establish(t);uint32_t disconnects=link.disconnects_;
  assert(link.send(source.type,source.payload,source.length));link.tick(t);assert(link.waiting_);
  const uint32_t retriesBefore=link.retries_;
  for(uint32_t now=t+10;now<=t+5000;now+=10){if((now-t)%1000==0)peerFrame(robolink::Heartbeat,0,now);link.tick(now);assert(link.connected_&&link.waiting_);}
  assert(link.disconnects_==disconnects&&link.retries_>retriesBefore+5);
  peerFrame(robolink::Ack,link.pending_.sequence,t+5005);assert(!link.waiting_&&link.connected_);

  // Silent peer with a pending frame: reset from the 3 s rx-silence bound, not the 1.2 s ack path.
  t=70000;establish(t);disconnects=link.disconnects_;
  assert(link.send(source.type,source.payload,source.length));link.tick(t);assert(link.waiting_);
  uint32_t now=t;while(link.connected_&&now<t+20000){now+=10;link.tick(now);}
  assert(!link.connected_&&link.disconnects_==disconnects+1&&now-t>=3000&&now-t<=3010);

  // Alive peer that never Acks: bounded 10 s stall cap still resets.
  t=100000;establish(t);disconnects=link.disconnects_;
  assert(link.send(source.type,source.payload,source.length));link.tick(t);
  now=t;while(link.connected_&&now<t+20000){now+=10;if((now-t)%1000==0)peerFrame(robolink::Heartbeat,0,now);link.tick(now);}
  assert(!link.connected_&&link.disconnects_==disconnects+1&&now-t>=10000&&now-t<=10010);
  assert(frameReleases==frameAllocations);
  std::puts("PASS: busy peer with heartbeats survives delayed Ack; silent peer resets at 3 s; never-acked frame resets at 10 s");

  link.lost();vQueueDelete(link.tx_);link.tx_=nullptr;
  std::puts("PASS: queued UART frames allocate on demand and release on send, full queue, OOM, and disconnect");
}
