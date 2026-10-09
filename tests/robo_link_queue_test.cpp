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
static constexpr std::size_t FrameAllocationBytes=1036; // PayloadMax 1024 plus aligned protocol header.
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{
  if(n!=FrameAllocationBytes)return std::malloc(n);
  if(failFrameAllocation)return nullptr;
  void*p=std::malloc(n);if(p)for(void*&slot:framePointers)if(!slot){slot=p;++frameAllocations;break;}
  return p;
}
void operator delete(void*p)noexcept{
  if(!p)return;
  for(void*&slot:framePointers)if(slot==p){slot=nullptr;++frameReleases;break;}
  std::free(p);
}
void operator delete(void*p,std::size_t)noexcept{::operator delete(p);}
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
  assert(first->frame->payload[17]==uint8_t(17*17));
  assert(!link.send(source.type,source.payload,source.length));assert(frameAllocations==9&&frameReleases==1);
  link.lost();assert(!link.connected_&&uxQueueMessagesWaiting(link.tx_)==0&&frameReleases==9);

  link.connected_=true;failFrameAllocation=true;assert(!link.send(source.type,source.payload,source.length));assert(uxQueueMessagesWaiting(link.tx_)==0);failFrameAllocation=false;
  assert(link.send(source.type,source.payload,source.length));const unsigned beforeRace=frameAllocations;
  mockBeforeQueueSend=staleProducer;assert(link.send(source.type,source.payload,source.length));
  assert(uxQueueMessagesWaiting(link.tx_)==1);assert(!link.takeQueuedFrame(link.pending_));assert(uxQueueMessagesWaiting(link.tx_)==0);
  assert(frameAllocations==beforeRace+1&&frameReleases==frameAllocations);

  link.connected_=true;assert(link.send(source.type,source.payload,source.length));
  assert(link.takeQueuedFrame(link.pending_));assert(link.pending_.length==robolink::PayloadMax&&link.pending_.payload[17]==0);
  assert(frameReleases==frameAllocations);link.lost();vQueueDelete(link.tx_);link.tx_=nullptr;
  std::puts("PASS: queued UART frames allocate on demand and release on send, full queue, OOM, and disconnect");
}
