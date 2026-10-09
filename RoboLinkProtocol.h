#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// COBS-delimited, little endian wire format. No native structs cross the link.
namespace robolink {
constexpr uint8_t Version=1;
constexpr size_t PayloadMax=1024, HeaderSize=12, RawMax=HeaderSize+PayloadMax+4, WireMax=RawMax+RawMax/254+2;
enum Type:uint8_t { Hello=1, HelloAck, Heartbeat, Ack, Credit, TcpOpen, TcpState, TcpData, TcpClose, Rpc, Reply, Clock, Notification, OtaBegin, OtaData, OtaFinish, OtaAbort, PairingPasskey, PairingPasskeyDisplayed, GeminiKey };
enum Flag:uint8_t { Reliable=1, First=2, Last=4 };
inline uint16_t get16(const uint8_t*p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline uint32_t get32(const uint8_t*p){return uint32_t(get16(p))|(uint32_t(get16(p+2))<<16);}
inline void put16(uint8_t*p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void put32(uint8_t*p,uint32_t v){put16(p,uint16_t(v));put16(p+2,uint16_t(v>>16));}
inline uint32_t crc32(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
struct Frame {uint8_t type=0,flags=0;uint16_t sequence=0,length=0;uint32_t session=0;uint8_t payload[PayloadMax]{};};
inline size_t encode(const Frame&f,uint8_t*out,size_t cap){
  if(f.length>PayloadMax||cap<WireMax)return 0;
  uint8_t raw[RawMax];raw[0]=Version;raw[1]=f.type;raw[2]=f.flags;raw[3]=0;put16(raw+4,f.sequence);put16(raw+6,f.length);put32(raw+8,f.session);
  memcpy(raw+HeaderSize,f.payload,f.length);const size_t n=HeaderSize+f.length;put32(raw+n,crc32(raw,n));
  size_t pos=1,codePos=0;uint8_t code=1;
  for(size_t i=0;i<n+4;++i){if(!raw[i]){out[codePos]=code;code=1;codePos=pos++;}else{out[pos++]=raw[i];if(++code==255){out[codePos]=code;code=1;codePos=pos++;}}}
  out[codePos]=code;out[pos++]=0;return pos;
}
class Parser {
  uint8_t encoded_[WireMax]{};size_t used_=0;bool overflow_=false;
public:
  uint32_t corrupt=0,oversize=0;void reset(){used_=0;overflow_=false;}
  bool feed(uint8_t b,Frame&f){
    if(b){if(used_<sizeof(encoded_)&&!overflow_)encoded_[used_++]=b;else overflow_=true;return false;}
    if(!used_&&!overflow_)return false;
    if(overflow_){++oversize;reset();return false;}
    uint8_t raw[RawMax];size_t n=0,i=0;bool valid=true;
    while(i<used_&&valid){const uint8_t code=encoded_[i++];if(!code||i+code-1>used_){valid=false;break;}
      for(unsigned k=1;k<code;++k){if(n==sizeof(raw)){valid=false;break;}raw[n++]=encoded_[i++];}
      if(code!=255&&i<used_){if(n==sizeof(raw)){valid=false;break;}raw[n++]=0;}}
    reset();
    if(!valid||n<HeaderSize+4||raw[0]!=Version||raw[3]||get16(raw+6)>PayloadMax||n!=HeaderSize+get16(raw+6)+4||get32(raw+n-4)!=crc32(raw,n-4)){++corrupt;return false;}
    f.type=raw[1];f.flags=raw[2];f.sequence=get16(raw+4);f.length=get16(raw+6);f.session=get32(raw+8);memcpy(f.payload,raw+HeaderSize,f.length);return true;
  }
};
inline uint16_t nextSequence(uint16_t s){return s==65535?1:uint16_t(s+1);}
// A C3 reboot can reuse the first RPC ID. Cache keys must include its session.
class RequestCache {
  uint32_t peer_=0,id_=0;
public:
  bool shouldExecute(uint32_t peer,uint32_t id){if(peer==peer_&&id==id_)return false;peer_=peer;id_=id;return true;}
};
}
