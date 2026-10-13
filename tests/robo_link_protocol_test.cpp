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
  static_assert(GeminiKey==20&&Settings==21&&SettingsReply==22,"BLE settings UART frame types are part of the C3/S3 contract");
  static_assert(PairingRequest==23,"S3 physical pairing request frame type is part of the C3/S3 contract");
  {Frame request;request.type=PairingRequest;request.flags=Reliable;request.length=0;auto encoded=wire(request);Parser p;assert(deliver(p,encoded,out));assert(out.type==PairingRequest&&out.length==0);}
  {Frame settings;settings.type=Settings;settings.flags=Reliable;settings.length=512;for(size_t i=0;i<settings.length;++i)settings.payload[i]=uint8_t(i*13u);auto encoded=wire(settings);Parser sp;assert(deliver(sp,encoded,out));assert(out.type==Settings&&out.length==512&&!memcmp(out.payload,settings.payload,512));}
  static_assert(PairingState==24,"C3->S3 pairing window state frame type is part of the contract");
  {uint8_t b[3];uint8_t st=0;uint16_t ttl=0;encodePairingState(b,PairOpen,60000);assert(decodePairingState(b,3,st,ttl)&&st==PairOpen&&ttl==60000);encodePairingState(b,PairApproved,0);assert(decodePairingState(b,3,st,ttl)&&st==PairApproved&&ttl==0);
   encodePairingState(b,PairOpen,0);assert(!decodePairingState(b,3,st,ttl));encodePairingState(b,PairOpen,60001);assert(!decodePairingState(b,3,st,ttl));b[0]=9;assert(!decodePairingState(b,3,st,ttl));b[0]=PairExpired;assert(!decodePairingState(b,2,st,ttl));
   Frame pf;pf.type=PairingState;pf.flags=Reliable;pf.length=3;encodePairingState(pf.payload,PairOpen,1234);auto enc=wire(pf);Parser pp;assert(deliver(pp,enc,out)&&out.type==PairingState&&out.length==3);}
  std::puts("PASS: UART framing, boundary lengths, CRC rejection, noise recovery, sequence wrap");
  std::puts("PASS: BLE settings UART frame types are stable and a full 512-byte settings frame round-trips");
}
