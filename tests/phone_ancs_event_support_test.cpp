#include "PhoneAncsEventSupport.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
void* failAllocation(size_t){return nullptr;}
}

int main(){
  PhoneAncsEventGate gate;
  assert(!gate.enter());
  gate.open();
  assert(gate.enter());

  std::atomic<bool> closed{false};
  std::thread closer([&]{
    gate.closeAndWait([]{std::this_thread::yield();});
    closed.store(true,std::memory_order_release);
  });
  while(gate.accepting())std::this_thread::yield();
  assert(!closed.load(std::memory_order_acquire));
  assert(!gate.enter());
  gate.leave();
  closer.join();
  assert(closed.load(std::memory_order_acquire));

  gate.open();
  assert(gate.enter());
  gate.leave();
  gate.closeAndWait([]{std::this_thread::yield();});

  PhoneAncsSessionGuard sessions;
  uint32_t oldGeneration=0,newGeneration=0,snapshotGeneration=0;
  uint16_t snapshotConnection=PhoneAncsSessionGuard::InvalidConnection;
  assert(!sessions.snapshot(snapshotGeneration,snapshotConnection));
  oldGeneration=sessions.begin(0); // Connection handle zero is valid.
  assert(sessions.accepts(oldGeneration,0));
  PhoneAncsOperationTokenPool operations;
  int owner=1;
  auto*oldOperation=operations.acquire(&owner,oldGeneration,0);
  assert(oldOperation&&operations.active(oldOperation));
  assert(oldOperation->owner==&owner&&oldOperation->generation==oldGeneration&&oldOperation->connection==0);

  sessions.invalidate();
  newGeneration=sessions.begin(0); // Simulate rapid reuse of the same handle.
  assert(newGeneration!=oldGeneration);
  assert(!sessions.accepts(oldOperation->generation,oldOperation->connection));
  auto*newOperation=operations.acquire(&owner,newGeneration,0);
  assert(newOperation&&newOperation!=oldOperation);
  assert(sessions.accepts(newOperation->generation,newOperation->connection));

  operations.release(oldOperation); // A stale GATT callback has now completed.
  auto*reused=operations.acquire(&owner,newGeneration,0);
  assert(reused==oldOperation);
  operations.release(reused);
  operations.release(newOperation);
  sessions.invalidate();
  assert(!sessions.accepts(newGeneration,0));
  auto*neverCompleted=operations.acquire(&owner,sessions.begin(0),0);
  assert(neverCompleted);
  operations.hostStopped(); // Safe to reclaim callbacks that NimBLE never delivered after shutdown.
  auto*afterStop=operations.acquire(&owner,sessions.begin(7),7);
  assert(afterStop==neverCompleted);
  operations.release(afterStop);

  PhoneAncsEventPayload payload;
  assert(payload.data==nullptr);
  assert(!payload.allocate(PhoneAncsEventPayload::Capacity+1));
  assert(!payload.allocate(PhoneAncsEventPayload::Capacity,failAllocation));
  assert(payload.data==nullptr);
  assert(payload.allocate(PhoneAncsEventPayload::Capacity));
  for(size_t i=0;i<PhoneAncsEventPayload::Capacity;++i)payload.data[i]=uint8_t(i*29u);

  // FreeRTOS queues copy the record; ownership moves to the copied queue item.
  PhoneAncsEventPayload queued=payload;
  payload.data=nullptr;
  assert(queued.data!=nullptr);
  for(size_t i=0;i<PhoneAncsEventPayload::Capacity;++i)assert(queued.data[i]==uint8_t(i*29u));
  queued.release();
  assert(queued.data==nullptr);

  PhoneAncsEventPayload empty;
  assert(empty.allocate(0));
  assert(empty.data!=nullptr);
  empty.release();
  std::cout<<"PASS: ANCS event/session gates; stale same-handle callbacks, token reuse, payload bounds and ownership transfer\n";
}
