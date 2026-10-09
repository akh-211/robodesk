#include "RoboLinkProtocol.h"
#include <cassert>
#include <vector>
#include <random>
#include <cstdio>
using namespace robolink;
static bool deliver(Parser&p,const std::vector<uint8_t>&wire,Frame&out){bool found=false;for(auto b:wire)found=p.feed(b,out)||found;return found;}
static std::vector<uint8_t> wire(const Frame&f){std::vector<uint8_t>b(WireMax);b.resize(encode(f,b.data(),b.size()));return b;}
int main(){
  Parser parser;Frame out;
  for(size_t length=0;length<=PayloadMax;++length){Frame f;f.type=TcpData;f.flags=Reliable;f.session=0x12340000;f.sequence=65535;f.length=uint16_t(length);for(size_t i=0;i<length;++i)f.payload[i]=uint8_t(i);auto encoded=wire(f);assert(!encoded.empty()&&encoded.back()==0);assert(deliver(parser,encoded,out));assert(out.type==f.type&&out.flags==f.flags&&out.session==f.session&&out.sequence==f.sequence&&out.length==length&&!memcmp(out.payload,f.payload,length));}
  for(uint8_t type:{PairingPasskey,PairingPasskeyDisplayed}){Frame pairing;pairing.type=type;pairing.flags=Reliable;pairing.length=type==PairingPasskey?10:4;put32(pairing.payload,0x10203040);put32(pairing.payload+4,123456);if(pairing.length==10)put16(pairing.payload+8,60000);auto encoded=wire(pairing);Parser p;assert(deliver(p,encoded,out));assert(out.type==type&&out.length==pairing.length&&!memcmp(out.payload,pairing.payload,pairing.length));}
  Frame f;f.type=Rpc;f.session=7;f.sequence=1;f.length=1024;memset(f.payload,0xff,sizeof(f.payload));auto valid=wire(f);
  // Corruption must never execute a command; the next delimited frame recovers.
  for(size_t i=0;i+1<valid.size();++i){auto damaged=valid;damaged[i]^=0x40;Parser p;assert(!deliver(p,damaged,out));assert(deliver(p,valid,out));}
  for(size_t i=1;i+1<valid.size();++i){auto cut=valid;cut.erase(cut.begin()+i);Parser p;assert(!deliver(p,cut,out));assert(deliver(p,valid,out));}
  std::vector<uint8_t> huge(WireMax*3,0x11);huge.push_back(0);assert(!deliver(parser,huge,out));assert(parser.oversize>0);assert(deliver(parser,valid,out));
  // A truncated packet cannot contaminate a later complete packet.
  Parser p;for(size_t i=0;i<valid.size()/2;++i)p.feed(valid[i],out);p.feed(0,out);assert(deliver(p,valid,out));
  std::mt19937 rng(711);for(unsigned run=0;run<1000;++run){std::vector<uint8_t>noise(rng()%3000);for(auto&b:noise)b=uint8_t(rng());noise.push_back(0);deliver(parser,noise,out);assert(deliver(parser,valid,out));}
  assert(nextSequence(65535)==1&&nextSequence(1)==2);assert(crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926u);
  RequestCache cache;assert(cache.shouldExecute(7,2));assert(!cache.shouldExecute(7,2));assert(cache.shouldExecute(8,2));assert(!cache.shouldExecute(8,2));assert(cache.shouldExecute(8,3));
  f.length=1025;uint8_t b[WireMax];assert(encode(f,b,sizeof(b))==0);f.length=2;assert(encode(f,b,5)==0);
  std::puts("PASS: UART framing, boundary lengths, CRC rejection, noise recovery, sequence wrap");
}
