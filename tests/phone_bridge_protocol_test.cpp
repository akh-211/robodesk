#include "PhoneBridgeProtocol.h"
#include <cassert>
#include <atomic>
#include <array>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {
const uint8_t kGolden[] = {
  0x52,0x44,0x42,0x32,0x02,0x01,0x03,0x00,0x12,0x34,0x56,0x78,0x01,0x00,0x00,0x00,
  0xfa,0x00,0x00,0x00,0x14,0x00,0x4e,0x41,0x56,0x0a,0x6c,0x65,0x66,0x74,0x0a,0x4d,
  0x61,0x69,0x6e,0x20,0x53,0x74,0x72,0x65,0x65,0x74,0x89,0xca,0x1b,0x00,0x23,0xda,
  0xf9,0x33,0x28,0x8b,0x46,0x25,0x6d,0xef,0xf7,0x24,0xed,0x85,0xfb,0xd7,0xc0,0xe9,
  0xd9,0xb5,0x5f,0xf2,0x2e,0x9b,0x41,0xdd,0xa7,0x6c
};
struct Fixture { const uint8_t* prefix; size_t prefixSize; const uint8_t* mac; };
bool verifyGolden(const uint8_t* prefix,size_t size,const uint8_t* mac,size_t macSize,void* context){
  const auto* f=static_cast<const Fixture*>(context);
  return f&&macSize==32&&size==f->prefixSize&&std::memcmp(prefix,f->prefix,size)==0&&std::memcmp(mac,f->mac,macSize)==0;
}
bool maxAge(uint8_t kind,uint32_t* value,void*){if(kind!=3)return false;*value=500;return true;}
}

int main(){
  std::vector<uint8_t> framedMessage(PhoneBridgeProtocol::MaxEnvelope);
  for(size_t i=0;i<framedMessage.size();++i)framedMessage[i]=uint8_t(i*37u);
  PhoneBridgeProtocol::FragmentReassembler reassembler;
  uint8_t reassembled[PhoneBridgeProtocol::MaxMessage]{};
  size_t reassembledSize=0;uint16_t fragmentId=19;
  for(const size_t mtu:std::array<size_t,3>{23,247,512}){
    reassembler.reset();reassembledSize=0;size_t offset=0;uint8_t id=uint8_t(fragmentId++);
    while(offset<framedMessage.size()){
      uint8_t frame[512]{};
      const size_t frameSize=PhoneBridgeProtocol::encodeFragment(framedMessage.data(),framedMessage.size(),id,offset,mtu,frame,sizeof(frame));
      assert(frameSize>PhoneBridgeProtocol::FragmentHeaderSize);
      const size_t chunk=frameSize-PhoneBridgeProtocol::FragmentHeaderSize;
      const bool complete=reassembler.accept(frame,frameSize,uint32_t(offset),reassembled,sizeof(reassembled),&reassembledSize);
      offset+=chunk;
      assert(complete==(offset==framedMessage.size()));
    }
    assert(reassembledSize==framedMessage.size()&&std::memcmp(reassembled,framedMessage.data(),reassembledSize)==0);
  }
  uint8_t firstFragment[20]{},badFragment[20]{};
  size_t firstSize=PhoneBridgeProtocol::encodeFragment(framedMessage.data(),framedMessage.size(),20,0,23,firstFragment,sizeof(firstFragment));
  assert(!reassembler.accept(firstFragment,firstSize,10,reassembled,sizeof(reassembled),&reassembledSize));
  size_t badSize=PhoneBridgeProtocol::encodeFragment(framedMessage.data(),framedMessage.size(),20,24,23,badFragment,sizeof(badFragment));
  assert(!reassembler.accept(badFragment,badSize,11,reassembled,sizeof(reassembled),&reassembledSize)&&!reassembler.active());
  assert(!PhoneBridgeProtocol::encodeFragment(framedMessage.data(),PhoneBridgeProtocol::MaxMessage+1,21,0,23,firstFragment,sizeof(firstFragment)));
  std::cout<<"PASS: fixed-memory GATT framing round-trips MTU 23/247/512 and rejects out-of-order fragments\n";

  PhoneBridgeProtocol::CandidateLeaseState candidates;
  PhoneBridgeProtocol::CandidateLease staleCandidate{},approvedCandidate{};
  candidates.connected(7,"AA:BB:CC:DD:EE:01");
  assert(!candidates.capture(staleCandidate));
  assert(!candidates.authenticated(7,"AA:BB:CC:DD:EE:02"));
  assert(candidates.authenticated(7,"AA:BB:CC:DD:EE:01"));
  assert(candidates.capture(staleCandidate)&&candidates.matches(staleCandidate));
  candidates.disconnected(7);
  candidates.connected(8,"AA:BB:CC:DD:EE:02");
  assert(candidates.authenticated(8,"AA:BB:CC:DD:EE:02"));
  assert(candidates.capture(approvedCandidate)&&!candidates.matches(staleCandidate));
  assert(candidates.matches(approvedCandidate)&&approvedCandidate.generation!=staleCandidate.generation);
  std::cout<<"PASS: candidate lease invalidates stale and replaced BLE connections\n";

  constexpr size_t prefixSize=22+20;
  Fixture fixture{kGolden,prefixSize,kGolden+prefixSize};
  PhoneBridgeProtocol::EnvelopeView message;
  PhoneBridgeProtocol::SessionReceiver receiver(PhoneBridgeProtocol::PhoneToRobot,
      0x78563412u,maxAge,nullptr,verifyGolden,&fixture);
  assert(receiver.accept(kGolden,sizeof(kGolden),&message));
  assert(message.kind==3&&message.counter==1&&message.ageMs==250&&message.payloadSize==20);
  assert(std::memcmp(message.payload,"NAV\nleft\nMain Street",20)==0);
  assert(receiver.lastCounter()==1&&!receiver.accept(kGolden,sizeof(kGolden),&message));
  PhoneBridgeProtocol::SessionReceiver wrongDirection(PhoneBridgeProtocol::RobotToPhone,
      0x78563412u,maxAge,nullptr,verifyGolden,&fixture);
  assert(!wrongDirection.accept(kGolden,sizeof(kGolden),&message));
  PhoneBridgeProtocol::SessionReceiver wrongSession(PhoneBridgeProtocol::PhoneToRobot,
      0x78563413u,maxAge,nullptr,verifyGolden,&fixture);
  assert(!wrongSession.accept(kGolden,sizeof(kGolden),&message));
  PhoneBridgeProtocol::SessionReceiver shortInput(PhoneBridgeProtocol::PhoneToRobot,
      0x78563412u,maxAge,nullptr,verifyGolden,&fixture);
  assert(!shortInput.accept(kGolden,sizeof(kGolden)-1,&message));
  std::vector<uint8_t> changed(kGolden,kGolden+sizeof(kGolden));changed[22]^=1;
  PhoneBridgeProtocol::SessionReceiver tampered(PhoneBridgeProtocol::PhoneToRobot,
      0x78563412u,maxAge,nullptr,verifyGolden,&fixture);
  assert(!tampered.accept(changed.data(),changed.size(),&message));
  PhoneBridgeProtocol::SessionReceiver concurrent(PhoneBridgeProtocol::PhoneToRobot,
      0x78563412u,maxAge,nullptr,verifyGolden,&fixture);
  std::atomic<unsigned> accepted{0};std::vector<std::thread> workers;
  for(unsigned i=0;i<8;++i)workers.emplace_back([&]{PhoneBridgeProtocol::EnvelopeView value;if(concurrent.accept(kGolden,sizeof(kGolden),&value))++accepted;});
  for(auto&worker:workers)worker.join();
  assert(accepted==1&&concurrent.lastCounter()==1);
  std::cout<<"PASS: envelope frame parser matches fixed Android v2 wire fixture\n";
}
