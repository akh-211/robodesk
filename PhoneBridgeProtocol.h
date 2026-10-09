#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <atomic>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <mbedtls/md.h>
#endif

namespace PhoneBridgeProtocol {
constexpr size_t HeaderSize = 22;
constexpr size_t MacSize = 32;
constexpr size_t MaxPayload = 500;
constexpr size_t MaxEnvelope = HeaderSize + MaxPayload + MacSize;
constexpr size_t FragmentHeaderSize = 8;
constexpr size_t MaxMessage = 600;
constexpr uint32_t FragmentTimeoutMs = 5000;
constexpr uint8_t FragmentVersion = 1;
constexpr uint8_t Version = 2;
constexpr uint8_t PhoneToRobot = 1;
constexpr uint8_t RobotToPhone = 2;

struct EnvelopeView {
  uint8_t direction = 0;
  uint8_t kind = 0;
  uint32_t sessionId = 0;
  uint32_t counter = 0;
  uint32_t ageMs = 0;
  const uint8_t* payload = nullptr;
  uint16_t payloadSize = 0;
};

// Non-atomic candidate identity state. Firmware callers guard this object with
// their BLE peer lock; keeping it platform-neutral lets host tests exercise
// disconnect/replacement races against the exact lease checks used in GATT.
struct CandidateLease {
  uint32_t generation = 0;
  uint16_t connection = 0;
  char address[18]{};
};

// Fixed-memory, one-message-at-a-time GATT fragmentation. Each BLE value has
// an 8-byte header: version, flags(first/last), message ID, total length, offset.
// Callers provide one instance per direction/connection and reset it on disconnect.
class FragmentReassembler {
 public:
  bool accept(const uint8_t* frame,size_t frameSize,uint32_t now,
              uint8_t* output,size_t outputCapacity,size_t* outputSize) {
    if(outputSize)*outputSize=0;
    if(!frame||frameSize<=FragmentHeaderSize||!output||!outputSize)return fail_();
    if(active_&&uint32_t(now-lastAt_)>FragmentTimeoutMs)reset();
    const uint8_t flags=frame[1];
    const uint16_t id=uint16_t(frame[2])|(uint16_t(frame[3])<<8);
    const uint16_t total=uint16_t(frame[4])|(uint16_t(frame[5])<<8);
    const uint16_t offset=uint16_t(frame[6])|(uint16_t(frame[7])<<8);
    const size_t chunk=frameSize-FragmentHeaderSize;
    const bool first=(flags&1u)!=0,last=(flags&2u)!=0;
    if(frame[0]!=FragmentVersion||(flags&~3u)||!id||!total||total>MaxMessage||
       total>outputCapacity||offset>total||chunk>size_t(total-offset))return fail_();
    if(first){if(active_||offset!=0)return fail_();active_=true;messageId_=id;total_=total;next_=0;}
    if(!active_||id!=messageId_||total!=total_||offset!=next_)return fail_();
    memcpy(buffer_+offset,frame+FragmentHeaderSize,chunk);next_=uint16_t(next_+chunk);lastAt_=now;
    if(last){if(next_!=total_)return fail_();memcpy(output,buffer_,total_);*outputSize=total_;reset();return true;}
    if(next_==total_)return fail_();
    return false;
  }
  void reset(){active_=false;messageId_=total_=next_=0;lastAt_=0;memset(buffer_,0,sizeof(buffer_));}
  bool active()const{return active_;}
  uint16_t nextOffset()const{return next_;}
 private:
  bool active_=false;uint16_t messageId_=0,total_=0,next_=0;uint32_t lastAt_=0;
  uint8_t buffer_[MaxMessage]{};
  bool fail_(){reset();return false;}
};

inline size_t encodeFragment(const uint8_t* message,size_t messageSize,uint16_t id,
                             size_t offset,size_t mtu,uint8_t* out,size_t capacity) {
  if(!message||!out||!id||!messageSize||messageSize>MaxMessage||mtu<=3+FragmentHeaderSize||
     offset>=messageSize||capacity<FragmentHeaderSize)return 0;
  const size_t chunkCap=mtu-3-FragmentHeaderSize;
  const size_t chunk=messageSize-offset<chunkCap?messageSize-offset:chunkCap;
  if(capacity<FragmentHeaderSize+chunk)return 0;
  out[0]=FragmentVersion;out[1]=uint8_t((offset==0?1u:0u)|(offset+chunk==messageSize?2u:0u));
  out[2]=uint8_t(id);out[3]=uint8_t(id>>8);out[4]=uint8_t(messageSize);out[5]=uint8_t(messageSize>>8);
  out[6]=uint8_t(offset);out[7]=uint8_t(offset>>8);memcpy(out+FragmentHeaderSize,message+offset,chunk);
  return FragmentHeaderSize+chunk;
}

class CandidateLeaseState {
 public:
  void connected(uint16_t connection, const char* address) {
    advance_(); live_=false; authenticated_=false; connection_=connection;
    copyAddress_(address); live_=address_[0]!=0;
  }
  bool authenticated(uint16_t connection, const char* address) {
    if(!live_||connection_!=connection||!address||strcmp(address_,address)!=0)return false;
    authenticated_=true;return true;
  }
  void disconnected(uint16_t connection) {
    if(live_&&connection_==connection)invalidate();
  }
  void invalidate() {
    advance_();live_=authenticated_=false;connection_=0;address_[0]=0;
  }
  bool capture(CandidateLease&out)const {
    if(!live_||!authenticated_||!address_[0])return false;
    out.generation=generation_;out.connection=connection_;
    memcpy(out.address,address_,sizeof(out.address));return true;
  }
  bool matches(const CandidateLease&lease)const {
    return live_&&authenticated_&&lease.generation&&lease.generation==generation_&&
      lease.connection==connection_&&lease.address[0]&&strcmp(lease.address,address_)==0;
  }
 private:
  uint32_t generation_=0;uint16_t connection_=0;bool live_=false,authenticated_=false;
  char address_[18]{};
  void advance_(){if(++generation_==0)++generation_;}
  void copyAddress_(const char*address){address_[0]=0;if(address){size_t n=strlen(address);if(n<sizeof(address_))memcpy(address_,address,n+1);}}
};

using MacVerifier = bool (*)(const uint8_t* prefix, size_t prefixSize,
                             const uint8_t* receivedMac, size_t macSize,
                             void* context);
using AgeLimit = bool (*)(uint8_t kind, uint32_t* maxAgeMs, void* context);

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
struct HmacKey { const uint8_t* bytes; size_t size; };

inline bool hmacSha256Domain(const uint8_t* key,size_t keySize,const uint8_t* domain,
                             size_t domainSize,const uint8_t* prefix,size_t prefixSize,
                             uint8_t out[MacSize]);

inline bool hmacSha256(const uint8_t* key,size_t keySize,const uint8_t* prefix,
                       size_t prefixSize,uint8_t out[MacSize]) {
  static const uint8_t domain[]="RoboDesk-Bridge-Envelope-v2";
  return hmacSha256Domain(key,keySize,domain,sizeof(domain),prefix,prefixSize,out);
}

inline bool hmacSha256Domain(const uint8_t* key,size_t keySize,const uint8_t* domain,
                             size_t domainSize,const uint8_t* prefix,size_t prefixSize,
                             uint8_t out[MacSize]) {
  if(!key||keySize!=32||!domain||!domainSize||(!prefix&&prefixSize)||!out)return false;
  const mbedtls_md_info_t* info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if(!info)return false;
  mbedtls_md_context_t context;mbedtls_md_init(&context);
  int rc=mbedtls_md_setup(&context,info,1);
  if(rc==0)rc=mbedtls_md_hmac_starts(&context,key,keySize);
  if(rc==0)rc=mbedtls_md_hmac_update(&context,domain,domainSize);
  if(rc==0&&prefixSize)rc=mbedtls_md_hmac_update(&context,prefix,prefixSize);
  if(rc==0)rc=mbedtls_md_hmac_finish(&context,out);
  mbedtls_md_free(&context);
  if(rc!=0)memset(out,0,MacSize);
  return rc==0;
}

inline bool verifyHmacSha256(const uint8_t* prefix,size_t prefixSize,
                             const uint8_t* receivedMac,size_t macSize,void* context) {
  const auto* key=static_cast<const HmacKey*>(context);
  if(!key||!receivedMac||macSize!=MacSize)return false;
  uint8_t expected[MacSize];
  if(!hmacSha256(key->bytes,key->size,prefix,prefixSize,expected))return false;
  uint8_t diff=0;for(size_t i=0;i<MacSize;++i)diff|=uint8_t(expected[i]^receivedMac[i]);
  memset(expected,0,sizeof(expected));
  return diff==0;
}

inline bool hmacKnownAnswerSelfTest() {
  static const uint8_t prefix[]={
    0x52,0x44,0x42,0x32,0x02,0x01,0x03,0x00,0x12,0x34,0x56,0x78,0x01,0x00,0x00,0x00,
    0xfa,0x00,0x00,0x00,0x14,0x00,0x4e,0x41,0x56,0x0a,0x6c,0x65,0x66,0x74,0x0a,0x4d,
    0x61,0x69,0x6e,0x20,0x53,0x74,0x72,0x65,0x65,0x74
  };
  static const uint8_t expected[MacSize]={
    0x89,0xca,0x1b,0x00,0x23,0xda,0xf9,0x33,0x28,0x8b,0x46,0x25,0x6d,0xef,0xf7,0x24,
    0xed,0x85,0xfb,0xd7,0xc0,0xe9,0xd9,0xb5,0x5f,0xf2,0x2e,0x9b,0x41,0xdd,0xa7,0x6c
  };
  uint8_t key[32],actual[MacSize];for(unsigned i=0;i<sizeof(key);++i)key[i]=uint8_t(i);
  const bool ok=hmacSha256(key,sizeof(key),prefix,sizeof(prefix),actual);
  uint8_t diff=0;for(size_t i=0;i<MacSize;++i)diff|=uint8_t(actual[i]^expected[i]);
  memset(key,0,sizeof(key));memset(actual,0,sizeof(actual));
  return ok&&diff==0;
}
#endif

inline uint16_t read16le(const uint8_t* p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t read32le(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void write16le(uint8_t*p,uint16_t value){p[0]=uint8_t(value);p[1]=uint8_t(value>>8);}
inline void write32le(uint8_t*p,uint32_t value){p[0]=uint8_t(value);p[1]=uint8_t(value>>8);p[2]=uint8_t(value>>16);p[3]=uint8_t(value>>24);}

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
inline bool encodeEnvelope(const uint8_t*key,size_t keySize,uint8_t direction,uint8_t kind,
                           uint32_t session,uint32_t counter,uint32_t ageMs,
                           const uint8_t*payload,size_t payloadSize,uint8_t*out,
                           size_t capacity,size_t*outSize){
  if(outSize)*outSize=0;
  if(!key||keySize!=32||!out||!outSize||!session||!counter||!kind||
     (direction!=PhoneToRobot&&direction!=RobotToPhone)||payloadSize>MaxPayload||
     (payloadSize&&!payload)||capacity<HeaderSize+payloadSize+MacSize)return false;
  memcpy(out,"RDB2",4);out[4]=Version;out[5]=direction;out[6]=kind;out[7]=0;
  write32le(out+8,session);write32le(out+12,counter);write32le(out+16,ageMs);
  write16le(out+20,uint16_t(payloadSize));if(payloadSize)memcpy(out+HeaderSize,payload,payloadSize);
  const size_t prefixSize=HeaderSize+payloadSize;
  if(!hmacSha256(key,keySize,out,prefixSize,out+prefixSize)){memset(out,0,prefixSize+MacSize);return false;}
  *outSize=prefixSize+MacSize;return true;
}
#endif

// The verifier must calculate HMAC-SHA256 over
// "RoboDesk-Bridge-Envelope-v2\\0" followed by prefix and compare it in
// constant time with receivedMac. Output is exposed only after verification.
inline bool verifyEnvelope(const uint8_t* wire, size_t size, uint8_t expectedDirection,
                           uint32_t expectedSession, AgeLimit ageLimit, void* ageContext,
                           MacVerifier verifyMac, void* macContext, EnvelopeView* out) {
  static const uint8_t magic[4] = {'R', 'D', 'B', '2'};
  if (!wire || !out || !ageLimit || !verifyMac || size < HeaderSize + MacSize ||
      size > MaxEnvelope || memcmp(wire, magic, sizeof(magic)) != 0 ||
      wire[4] != Version || wire[5] != expectedDirection || wire[6] == 0 ||
      wire[7] != 0) return false;

  const uint32_t session = read32le(wire + 8);
  const uint32_t counter = read32le(wire + 12);
  const uint32_t age = read32le(wire + 16);
  const uint16_t payloadSize = read16le(wire + 20);
  if (!session || session != expectedSession || !counter ||
      payloadSize > MaxPayload || size != HeaderSize + payloadSize + MacSize) return false;

  uint32_t maxAge = 0;
  if (!ageLimit(wire[6], &maxAge, ageContext) || age > maxAge) return false;
  const size_t prefixSize = HeaderSize + payloadSize;
  if (!verifyMac(wire, prefixSize, wire + prefixSize, MacSize, macContext)) return false;

  out->direction = wire[5];
  out->kind = wire[6];
  out->sessionId = session;
  out->counter = counter;
  out->ageMs = age;
  out->payload = wire + HeaderSize;
  out->payloadSize = payloadSize;
  return true;
}

class SessionReceiver {
 public:
  SessionReceiver(uint8_t direction, uint32_t session, AgeLimit ageLimit,
                  void* ageContext, MacVerifier verifyMac, void* macContext)
      : direction_(direction), session_(session), ageLimit_(ageLimit),
        ageContext_(ageContext), verifyMac_(verifyMac), macContext_(macContext) {}

  bool accept(const uint8_t* wire, size_t size, EnvelopeView* out) {
    if (!session_ || !out || (direction_ != PhoneToRobot && direction_ != RobotToPhone)) return false;
    EnvelopeView candidate;
    if (!verifyEnvelope(wire,size,direction_,session_,ageLimit_,ageContext_,
                        verifyMac_,macContext_,&candidate)) return false;
    uint32_t prior=lastCounter_.load(std::memory_order_relaxed);
    while(candidate.counter>prior) {
      if(lastCounter_.compare_exchange_weak(prior,candidate.counter,
            std::memory_order_acq_rel,std::memory_order_relaxed)) {
        *out=candidate; // Commit before exposing the authenticated payload.
        return true;
      }
    }
    return false;
  }

  uint32_t lastCounter() const { return lastCounter_.load(std::memory_order_acquire); }

 private:
  uint8_t direction_;
  uint32_t session_;
  AgeLimit ageLimit_;
  void* ageContext_;
  MacVerifier verifyMac_;
  void* macContext_;
  std::atomic<uint32_t> lastCounter_{0};
};
}  // namespace PhoneBridgeProtocol
