#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

class PhoneAncsEventGate {
 public:
  bool enter(){
    if(!accepting_.load(std::memory_order_acquire))return false;
    inFlight_.fetch_add(1,std::memory_order_acq_rel);
    if(!accepting_.load(std::memory_order_acquire)){
      inFlight_.fetch_sub(1,std::memory_order_release);
      return false;
    }
    return true;
  }
  void leave(){inFlight_.fetch_sub(1,std::memory_order_release);}
  template<class Yield> void closeAndWait(Yield yield){
    accepting_.store(false,std::memory_order_release);
    while(inFlight_.load(std::memory_order_acquire)!=0)yield();
  }
  void open(){accepting_.store(true,std::memory_order_release);}
  bool accepting()const{return accepting_.load(std::memory_order_acquire);}
 private:
  std::atomic<bool> accepting_{false};
  std::atomic<uint32_t> inFlight_{0};
};

class PhoneAncsSessionGuard {
 public:
  static constexpr uint16_t InvalidConnection=0xffffu;
  uint32_t begin(uint16_t connection){
    sequence_.fetch_add(1,std::memory_order_acq_rel);
    connection_.store(connection,std::memory_order_relaxed);
    return sequence_.fetch_add(1,std::memory_order_release)+1;
  }
  uint32_t invalidate(){
    sequence_.fetch_add(1,std::memory_order_acq_rel);
    connection_.store(InvalidConnection,std::memory_order_relaxed);
    return sequence_.fetch_add(1,std::memory_order_release)+1;
  }
  bool snapshot(uint32_t&generation,uint16_t&connection)const{
    const uint32_t before=sequence_.load(std::memory_order_acquire);if(!before||(before&1u))return false;
    const uint16_t current=connection_.load(std::memory_order_relaxed);const uint32_t after=sequence_.load(std::memory_order_acquire);
    if(before!=after||current==InvalidConnection)return false;
    generation=after;connection=current;return true;
  }
  bool accepts(uint32_t generation,uint16_t connection)const{
    uint32_t currentGeneration=0;uint16_t currentConnection=InvalidConnection;
    return generation&&snapshot(currentGeneration,currentConnection)&&generation==currentGeneration&&connection==currentConnection;
  }
 private:
  std::atomic<uint32_t> sequence_{0};
  std::atomic<uint16_t> connection_{InvalidConnection};
};

class PhoneAncsOperationTokenPool {
 public:
  static constexpr size_t Capacity=8;
  struct Token{void*owner=nullptr;uint32_t generation=0;uint16_t connection=PhoneAncsSessionGuard::InvalidConnection;std::atomic<uint8_t>state{0};};
  Token* acquire(void*owner,uint32_t generation,uint16_t connection){
    if(!owner||!generation||connection==PhoneAncsSessionGuard::InvalidConnection)return nullptr;
    for(auto&token:tokens_){uint8_t expected=0;if(!token.state.compare_exchange_strong(expected,1,std::memory_order_acquire))continue;token.owner=owner;token.generation=generation;token.connection=connection;token.state.store(2,std::memory_order_release);return &token;}
    return nullptr;
  }
  bool active(const Token*token)const{return token&&token->state.load(std::memory_order_acquire)==2;}
  void release(Token*token){if(!active(token))return;token->owner=nullptr;token->generation=0;token->connection=PhoneAncsSessionGuard::InvalidConnection;token->state.store(0,std::memory_order_release);}
  void hostStopped(){for(auto&token:tokens_){token.owner=nullptr;token.generation=0;token.connection=PhoneAncsSessionGuard::InvalidConnection;token.state.store(0,std::memory_order_release);}}
 private:
  Token tokens_[Capacity]{};
};

class PhoneAncsEventPayload {
 public:
  static constexpr size_t Capacity=384;
  uint8_t* data=nullptr;
  using Allocator=void*(*)(size_t);

  bool allocate(size_t size,Allocator allocator=::malloc){
    if(data||size>Capacity||!allocator)return false;
    data=static_cast<uint8_t*>(allocator(size?size:1));
    return data!=nullptr;
  }
  void release(){free(data);data=nullptr;}
};
