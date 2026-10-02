#include "RoboLog.h"
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha1.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include <atomic>
#include <memory>
#include <new>
#include <cstdlib>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Arduino-ESP32 3.3.x compatibility: after the HTTP 101 upgrade we avoid
// NetworkClientSecure::available()/read(), whose zero-length mbedTLS probe can
// fail to pull newly arrived TLS application records.  The socket is switched
// to nonblocking mode and application data is read directly through mbedTLS.
class RoboDeskTlsClient : public WiFiClientSecure {
public:
  bool enableDirectNonBlocking(){
    if(!sslclient || sslclient->socket < 0) return false;
    int flags=fcntl(sslclient->socket,F_GETFL,0);
    if(flags<0)return false;
    return fcntl(sslclient->socket,F_SETFL,flags|O_NONBLOCK)==0;
  }
  int directTlsRead(uint8_t* out,size_t cap){
    if(!out||!cap||!sslclient||sslclient->socket<0)return -1;
    int ret=mbedtls_ssl_read(&sslclient->ssl_ctx,out,cap);
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    if(ret==MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)return 0;
#endif
    if(ret==MBEDTLS_ERR_SSL_WANT_READ||ret==MBEDTLS_ERR_SSL_WANT_WRITE)return 0;
    if(ret==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY||ret==0)return -1;
    return ret;
  }
  // Bound all TLS records of a WebSocket frame to one short deadline.
  bool directTlsWrite(const uint8_t* data,size_t len,uint32_t startedAt,uint32_t budgetMs=12u){
    if(!sslclient||sslclient->socket<0)return false;
    size_t sent=0;
    while(sent<len){
      if(uint32_t(millis()-startedAt)>=budgetMs)return false;
      const int n=mbedtls_ssl_write(&sslclient->ssl_ctx,data+sent,len-sent);
      if(n>0){sent+=size_t(n);continue;}
      if(n!=MBEDTLS_ERR_SSL_WANT_READ&&n!=MBEDTLS_ERR_SSL_WANT_WRITE)return false;
      delay(1); // WANT retry must use the same buffer and length.
    }
    return true;
  }
  int tlsBuffered() const {
    if(!sslclient)return 0;
    return int(mbedtls_ssl_get_bytes_avail(&sslclient->ssl_ctx));
  }
  int socketFd() const { return sslclient ? sslclient->socket : -1; }
  int rawPeek(uint8_t* out,size_t cap,int* errOut=0) const {
    if(errOut)*errOut=0;
    if(!out||!cap||!sslclient||sslclient->socket<0){if(errOut)*errOut=EBADF;return -2;}
    int r=::recv(sslclient->socket,out,cap,MSG_PEEK|MSG_DONTWAIT);
    if(r<0){int e=errno;if(errOut)*errOut=e;if(e==EAGAIN||e==EWOULDBLOCK)return -1;return -2;}
    return r; // >0 encrypted bytes ready, 0 peer FIN
  }
};
#include "GeminiStreamParser.h"

class GeminiLiveDirectClient {
public:
  struct Config {
    const char* apiKey;
    const char* model;
    const char* voice;
    const char* languageCode;
    const char* systemPrompt;
    bool insecureTls;
    const char* rootCaPem;
    const char* resumeHandle;
    const char* toolsJson;
  };

private:
  static const char* HOST;
  static const uint16_t PORT = 443;
  RoboDeskTlsClient tls_;
  uint8_t txSmallFrame_[2048]; // One TLS record for each 40 ms microphone packet.
  struct Scratch {
    GeminiStreamParser parser;
    char promptEsc[4800],handleEsc[1200],resumeJson[1240],setupBuf[9200];
    char toolIdEsc[160],toolNameEsc[160],toolMsg[1800];
    char audioB64[1800],audioMsg[1980];
    explicit Scratch(GeminiStreamSink* sink):parser(sink){}
  };
  struct ScratchDeleter {
    void operator()(Scratch* p) const {if(p){p->~Scratch();std::free(p);}}
  };
  std::unique_ptr<Scratch,ScratchDeleter> scratch_;
  GeminiStreamSink* sink_=nullptr;
  bool ensureScratch(){
    if(scratch_)return true;
    void* memory=nullptr;
#if defined(ARDUINO_ARCH_ESP32)
    memory=heap_caps_malloc(sizeof(Scratch),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory)memory=heap_caps_malloc(sizeof(Scratch),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
#else
    memory=std::malloc(sizeof(Scratch));
#endif
    if(!memory){RoboLog.println("LEV,GEMINI,SCRATCH_ALLOC_FAIL");return false;}
    scratch_.reset(new(memory) Scratch(sink_));
    RoboLog.printf("LEV,GEMINI,SCRATCH,bytes=%u\n",unsigned(sizeof(Scratch)));
    return true;
  }
  Config cfg_;
  std::atomic<bool> connecting_{false},cancelConnect_{false};
  String connectStrings_[8]; // Immutable config snapshot while worker owns TLS.
  uint32_t connectBackoffMs_=2500;
  uint32_t heartbeatSentAt_=0;
  bool heartbeatPending_=false;
  bool connected_ = false;
  bool ready_ = false;
  uint32_t reconnectAt_ = 0;
  uint32_t lastRxAt_ = 0;
  uint32_t lastTxAt_ = 0;
  uint32_t connects_ = 0;
  uint32_t disconnects_ = 0;
  uint32_t protocolErrors_ = 0;
  uint32_t setupSentAt_ = 0;
  uint32_t rxBytesTotal_ = 0;
  uint32_t rxFrames_ = 0;
  uint32_t rxMessages_ = 0;
  uint32_t directTlsReads_ = 0;
  uint32_t directTlsBytes_ = 0;
  uint32_t directTlsNoData_ = 0;
  uint32_t serviceCalls_ = 0;
  uint32_t serviceBudgetZero_ = 0;
  uint32_t preReadyForcedPolls_ = 0;
  uint32_t rawPeekData_ = 0;
  uint32_t rawPeekNoData_ = 0;
  uint32_t rawPeekFin_ = 0;
  uint32_t rawPeekErr_ = 0;
  int lastRawPeek_ = -99;
  int lastRawErr_ = 0;
  int lastTlsReadRet_ = 0;
  uint8_t lastRxOpcode_ = 0;
  size_t setupBytes_ = 0;
  uint32_t probePingSentAt_ = 0;
  bool probePongSeen_ = false;
  uint32_t currentMessageBytes_ = 0;
  uint8_t currentMessageOpcode_ = 0;
  char preReadyPreview_[384];
  size_t preReadyPreviewLen_ = 0;
  // Parser and text/audio scratch share a persistent PSRAM block, allocated after boot.
  uint32_t audioEnvelopeMessages_ = 0;
  uint32_t audioEnvelopeBytes_ = 0;
  uint32_t audioEnvelopeLogAt_ = 0;

  enum RxState : uint8_t { RX_B0, RX_B1, RX_LEN16, RX_LEN64, RX_MASK, RX_PAYLOAD };
  RxState rxState_ = RX_B0;
  uint8_t rxOpcode_ = 0;
  bool rxFin_ = false;
  bool rxMasked_ = false;
  uint64_t rxLen_ = 0;
  uint64_t rxRead_ = 0;
  uint8_t rxExtNeed_ = 0;
  uint8_t rxExtGot_ = 0;
  uint8_t rxMask_[4] = {0,0,0,0};
  uint8_t rxMaskGot_ = 0;
  uint8_t controlBuf_[128];
  size_t controlLen_ = 0;
  bool textMessageActive_ = false;

  static const char* b64chars(){ return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; }

  static size_t base64Encode(const uint8_t* in,size_t n,char* out,size_t cap){
    size_t need=((n+2)/3)*4; if(cap<=need) return 0;
    size_t oi=0;
    for(size_t i=0;i<n;i+=3){
      uint32_t v=uint32_t(in[i])<<16;
      bool h1=i+1<n,h2=i+2<n; if(h1)v|=uint32_t(in[i+1])<<8; if(h2)v|=in[i+2];
      out[oi++]=b64chars()[(v>>18)&63]; out[oi++]=b64chars()[(v>>12)&63];
      out[oi++]=h1?b64chars()[(v>>6)&63]:'='; out[oi++]=h2?b64chars()[v&63]:'=';
    }
    out[oi]=0; return oi;
  }

  static bool headerContains(const char* headers,const char* needle){ return strstr(headers,needle)!=0; }

  static bool websocketAcceptMatches(const char* headers,const char* key){
    const char* p=strstr(headers,"Sec-WebSocket-Accept:"); if(!p) p=strstr(headers,"sec-websocket-accept:"); if(!p) return false;
    p=strchr(p,':'); if(!p)return false; ++p; while(*p==' '||*p=='\t')++p;
    char got[64]; size_t n=0; while(*p&&*p!='\r'&&*p!='\n'&&n+1<sizeof(got))got[n++]=*p++; got[n]=0;
    char src[128]; snprintf(src,sizeof(src),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
    unsigned char sha[20]; mbedtls_sha1(reinterpret_cast<const unsigned char*>(src),strlen(src),sha);
    char expected[40]; base64Encode(sha,sizeof(sha),expected,sizeof(expected));
    return strcmp(got,expected)==0;
  }

  bool sendFrame(uint8_t opcode,const uint8_t* payload,size_t len){
    if(!connected_)return false;
    const uint32_t writeStartedAt=millis();
    const uint32_t writeBudget=connecting()?1000u:12u; // Longer setup budget belongs only to worker.
    uint8_t h[14]; size_t hn=0; h[hn++]=uint8_t(0x80|(opcode&0x0f));
    if(len<=125){h[hn++]=uint8_t(0x80|len);} else if(len<=65535){h[hn++]=0x80|126;h[hn++]=uint8_t(len>>8);h[hn++]=uint8_t(len);} else {return false;}
    uint32_t r=esp_random(); uint8_t mask[4]={uint8_t(r),uint8_t(r>>8),uint8_t(r>>16),uint8_t(r>>24)};
    for(int i=0;i<4;i++)h[hn++]=mask[i];
    if(hn+len<=sizeof(txSmallFrame_)){
      memcpy(txSmallFrame_,h,hn);
      for(size_t i=0;i<len;i++)txSmallFrame_[hn+i]=payload[i]^mask[i&3];
      if(!tls_.directTlsWrite(txSmallFrame_,hn+len,writeStartedAt,writeBudget)){disconnectInternal(millis());return false;}
      lastTxAt_=millis();return true;
    }
    if(!tls_.directTlsWrite(h,hn,writeStartedAt,writeBudget)){disconnectInternal(millis());return false;}
    uint8_t tmp[256]; size_t off=0;
    while(off<len){ size_t k=(len-off)<sizeof(tmp)?(len-off):sizeof(tmp); for(size_t i=0;i<k;i++)tmp[i]=payload[off+i]^mask[(off+i)&3]; if(!tls_.directTlsWrite(tmp,k,writeStartedAt,writeBudget)){disconnectInternal(millis());return false;} off+=k; }
    lastTxAt_=millis(); return true;
  }

  // HOTFIX9 diagnostic path: one complete small WebSocket frame in one TLS write.
  // Used for the first application message so the Live API sees setup first.
  bool sendSmallFrameSingle(uint8_t opcode,const uint8_t* payload,size_t len){
    if(!connected_||len>125)return false;
    uint8_t frame[2+4+125]; size_t n=0;
    frame[n++]=uint8_t(0x80|(opcode&0x0f));
    frame[n++]=uint8_t(0x80|len);
    uint32_t r=esp_random(); uint8_t mask[4]={uint8_t(r),uint8_t(r>>8),uint8_t(r>>16),uint8_t(r>>24)};
    for(uint8_t i=0;i<4;i++)frame[n++]=mask[i];
    for(size_t i=0;i<len;i++)frame[n++]=payload[i]^mask[i&3];
    const size_t wrote=tls_.directTlsWrite(frame,n,millis())?n:0;
    RoboLog.printf("LEV,GEMINI,TX_FRAME,opcode=%u,payload=%u,wire=%u,wrote=%u\n",unsigned(opcode),unsigned(len),unsigned(n),unsigned(wrote));
    if(wrote!=n)return false;
    lastTxAt_=millis();return true;
  }

  bool sendTextFrame(const char* text){ return sendFrame(0x1,reinterpret_cast<const uint8_t*>(text),strlen(text)); }
  bool sendPong(const uint8_t* p,size_t n){ return sendFrame(0xA,p,n); }

  bool readHttpHeaders(char* out,size_t cap,uint32_t timeoutMs){
    size_t n=0; uint32_t start=millis();
    while(millis()-start<timeoutMs && n+1<cap){
      while(tls_.available()&&n+1<cap){ out[n++]=char(tls_.read()); out[n]=0; if(n>=4&&memcmp(out+n-4,"\r\n\r\n",4)==0)return true; }
      delay(1);
    }
    return false;
  }

  static size_t jsonEscape(const char* in,char* out,size_t cap){
    if(!out||!cap)return 0;
    size_t n=0;
    if(!in){out[0]=0;return 0;}
    for(const char* p=in;*p&&n+2<cap;++p){unsigned char c=static_cast<unsigned char>(*p);
      if(c=='\"'||c=='\\'){out[n++]='\\';out[n++]=char(c);}
      else if(c=='\n'){out[n++]='\\';out[n++]='n';}
      else if(c=='\r'){out[n++]='\\';out[n++]='r';}
      else if(c=='\t'){out[n++]='\\';out[n++]='t';}
      else if(c>=0x20)out[n++]=char(c);
    }
    out[n]=0; return n;
  }

  bool sendSetup(){
    setupBytes_=0;
    jsonEscape(cfg_.systemPrompt,scratch_->promptEsc,sizeof(scratch_->promptEsc));
    jsonEscape(cfg_.resumeHandle,scratch_->handleEsc,sizeof(scratch_->handleEsc));
    if(scratch_->handleEsc[0])snprintf(scratch_->resumeJson,sizeof(scratch_->resumeJson),"{\"handle\":\"%s\"}",scratch_->handleEsc);
    else strcpy(scratch_->resumeJson,"{}");
    const char* tools=(cfg_.toolsJson&&cfg_.toolsJson[0])?cfg_.toolsJson:"[]";
    int n=snprintf(scratch_->setupBuf,sizeof(scratch_->setupBuf),
      "{\"setup\":{\"model\":\"models/%s\",\"generationConfig\":{\"responseModalities\":[\"AUDIO\"],\"speechConfig\":{\"voiceConfig\":{\"prebuiltVoiceConfig\":{\"voiceName\":\"%s\"}}}},\"systemInstruction\":{\"parts\":[{\"text\":\"%s\"}]},\"tools\":%s,\"realtimeInputConfig\":{\"automaticActivityDetection\":{\"disabled\":false,\"startOfSpeechSensitivity\":\"START_SENSITIVITY_HIGH\",\"prefixPaddingMs\":160,\"endOfSpeechSensitivity\":\"END_SENSITIVITY_HIGH\",\"silenceDurationMs\":650}},\"inputAudioTranscription\":{},\"outputAudioTranscription\":{},\"contextWindowCompression\":{\"triggerTokens\":\"25000\",\"slidingWindow\":{\"targetTokens\":\"8000\"}},\"sessionResumption\":%s}}",
      cfg_.model,cfg_.voice,scratch_->promptEsc,tools,scratch_->resumeJson);
    if(n<=0||size_t(n)>=sizeof(scratch_->setupBuf))return false;
    setupBytes_=size_t(n);
    const bool ok=sendTextFrame(scratch_->setupBuf);
    if(ok){setupSentAt_=millis();RoboLog.printf("LEV,GEMINI,SETUP_SENT,profile=v0151_realtime_hybrid,lang=%s,setupBytes=%u\n",(cfg_.languageCode&&cfg_.languageCode[0])?cfg_.languageCode:"id-ID",unsigned(setupBytes_));}
    return ok;
  }

  void resetRx(){ rxState_=RX_B0;rxOpcode_=0;rxFin_=false;rxMasked_=false;rxLen_=rxRead_=0;rxExtNeed_=rxExtGot_=0;rxMaskGot_=0;controlLen_=0; }

  void resetMessagePreview(uint8_t opcode){
    currentMessageBytes_=0;currentMessageOpcode_=opcode;preReadyPreviewLen_=0;preReadyPreview_[0]=0;
  }

  void previewByte(uint8_t b){
    ++currentMessageBytes_;
    if(ready_||preReadyPreviewLen_+1>=sizeof(preReadyPreview_))return;
    char c=char(b);
    if(c=='\r'||c=='\n'||c=='\t')c=' ';
    if(uint8_t(c)<0x20||uint8_t(c)>0x7e)c='.';
    preReadyPreview_[preReadyPreviewLen_++]=c;
    preReadyPreview_[preReadyPreviewLen_]=0;
  }

  void disconnectInternal(uint32_t now,uint32_t backoffMs=2500u){
    tls_.stop();
    if(connected_)++disconnects_;
    connected_=ready_=false; heartbeatPending_=false; textMessageActive_=false; resetRx(); reconnectAt_=now+backoffMs;
  }

  void finishFrame(){
    if(rxOpcode_==0x8){
      unsigned code=0;char reason[96];reason[0]=0;
      if(controlLen_>=2){code=(unsigned(controlBuf_[0])<<8)|unsigned(controlBuf_[1]);size_t rn=0;for(size_t i=2;i<controlLen_&&rn+1<sizeof(reason);++i){char c=char(controlBuf_[i]);reason[rn++]=(uint8_t(c)>=0x20&&uint8_t(c)<=0x7e)?c:'.';}reason[rn]=0;}
      RoboLog.printf("LEV,GEMINI,CLOSE,code=%u,reason=%s\n",code,reason);
      const bool quotaClose=(strstr(reason,"quota")!=0)||(strstr(reason,"Quota")!=0);
      const uint32_t backoff=quotaClose?300000u:2500u;
      if(quotaClose)RoboLog.printf("LEV,GEMINI,QUOTA_BACKOFF,ms=%lu\n",(unsigned long)backoff);
      disconnectInternal(millis(),backoff); return;
    }
    if(rxOpcode_==0x9){ sendPong(controlBuf_,controlLen_); }
    if(rxOpcode_==0xA){
      if(controlLen_==2&&memcmp(controlBuf_,"rd",2)==0)heartbeatPending_=false;
      probePongSeen_=true;
      char pong[48];size_t pn=0;for(size_t i=0;i<controlLen_&&pn+1<sizeof(pong);++i){char c=char(controlBuf_[i]);pong[pn++]=(uint8_t(c)>=0x20&&uint8_t(c)<=0x7e)?c:'.';}pong[pn]=0;
      RoboLog.printf("LEV,GEMINI,PONG,len=%u,payload=%s\n",unsigned(controlLen_),pong);
    }
    if((rxOpcode_==0x1||rxOpcode_==0x2||rxOpcode_==0x0) && rxFin_ && textMessageActive_){
      scratch_->parser.endMessage();++rxMessages_;
      if(!ready_)RoboLog.printf("LEV,GEMINI,RX_PRE_READY,opcode=%u,msgBytes=%lu,preview=%s\n",unsigned(currentMessageOpcode_),(unsigned long)currentMessageBytes_,preReadyPreview_);
      else if(scratch_->parser.sawModelTurn()){
        const uint32_t decoded=scratch_->parser.decodedAudioBytes();
        if(decoded){
          ++audioEnvelopeMessages_; audioEnvelopeBytes_+=decoded;
          const uint32_t logNow=millis();
          if(uint32_t(logNow-audioEnvelopeLogAt_)>=2000u){
            audioEnvelopeLogAt_=logNow;
            RoboLog.printf("LEV,GEMINI,AUDIO_FLOW,msgs=%lu,decoded=%lu\n",(unsigned long)audioEnvelopeMessages_,(unsigned long)audioEnvelopeBytes_);
          }
        } else if(scratch_->parser.sawModelTurn()) RoboLog.printf("LEV,GEMINI,MODEL_TURN_NO_AUDIO,msgBytes=%lu,inline=%u,data=%u\n",(unsigned long)currentMessageBytes_,unsigned(scratch_->parser.sawInlineData()),unsigned(scratch_->parser.startedAudioData()));
      }
      textMessageActive_=false;
    }
    resetRx();
  }

  void feedPayloadByte(uint8_t b){
    if(rxMasked_)b^=rxMask_[rxRead_&3];
    ++rxBytesTotal_;
    if(rxOpcode_==0x1||rxOpcode_==0x2){
      if(rxRead_==0){scratch_->parser.beginMessage();textMessageActive_=true;resetMessagePreview(rxOpcode_);} scratch_->parser.feed(&b,1);previewByte(b);
    }
    else if(rxOpcode_==0x0 && textMessageActive_){scratch_->parser.feed(&b,1);previewByte(b);}
    else if((rxOpcode_==0x8||rxOpcode_==0x9||rxOpcode_==0xA) && controlLen_<sizeof(controlBuf_))controlBuf_[controlLen_++]=b;
    ++rxRead_; lastRxAt_=millis(); if(rxRead_>=rxLen_)finishFrame();
  }

  bool consumeRxByte(uint8_t b){
    switch(rxState_){
      case RX_B0: rxFin_=(b&0x80)!=0; rxOpcode_=b&0x0f; lastRxOpcode_=rxOpcode_;++rxFrames_; rxState_=RX_B1; return true;
      case RX_B1:
        rxMasked_=(b&0x80)!=0; rxLen_=b&0x7f; rxRead_=0; controlLen_=0;
        if(rxLen_==126){rxLen_=0;rxExtNeed_=2;rxExtGot_=0;rxState_=RX_LEN16;}
        else if(rxLen_==127){rxLen_=0;rxExtNeed_=8;rxExtGot_=0;rxState_=RX_LEN64;}
        else if(rxMasked_){rxMaskGot_=0;rxState_=RX_MASK;}
        else if(rxLen_==0)finishFrame(); else rxState_=RX_PAYLOAD;
        return true;
      case RX_LEN16: case RX_LEN64:
        rxLen_=(rxLen_<<8)|b; if(++rxExtGot_>=rxExtNeed_){ if(rxMasked_){rxMaskGot_=0;rxState_=RX_MASK;} else if(rxLen_==0)finishFrame(); else rxState_=RX_PAYLOAD; } return true;
      case RX_MASK:
        rxMask_[rxMaskGot_++]=b; if(rxMaskGot_==4){if(rxLen_==0)finishFrame(); else rxState_=RX_PAYLOAD;} return true;
      case RX_PAYLOAD: feedPayloadByte(b); return true;
    }
    return false;
  }

public:
  explicit GeminiLiveDirectClient(GeminiStreamSink* sink=0):sink_(sink){ memset(&cfg_,0,sizeof(cfg_)); }
  void setSink(GeminiStreamSink* sink){sink_=sink;if(scratch_)scratch_->parser.setSink(sink);}
  bool connecting()const{return connecting_.load(std::memory_order_acquire);}
  bool connected()const{return !connecting()&&connected_;}
  bool ready()const{return !connecting()&&ready_;}
  uint32_t connects()const{return connecting()?0:connects_;}
  uint32_t disconnects()const{return connecting()?0:disconnects_;}
  uint32_t protocolErrors()const{return connecting()?0:protocolErrors_;}
  uint32_t lastRxAt()const{return connecting()?0:lastRxAt_;}
  size_t setupBytes()const{return connecting()?0:setupBytes_;}
  uint32_t rxBytesTotal()const{return connecting()?0:rxBytesTotal_;}
  uint32_t rxFrames()const{return connecting()?0:rxFrames_;}
  uint32_t rxMessages()const{return connecting()?0:rxMessages_;}

  bool connectAsync(const Config& cfg){
    if(connecting()||connected())return false;
    if(!ensureScratch()){reconnectAt_=millis()+30000u;return false;}
    const char* src[]={cfg.apiKey,cfg.model,cfg.voice,cfg.languageCode,cfg.systemPrompt,cfg.rootCaPem,cfg.resumeHandle,cfg.toolsJson};
    for(size_t i=0;i<8;++i){
      const char* value=src[i]?src[i]:"";
      connectStrings_[i]=value;
      if(connectStrings_[i].length()!=strlen(value)){reconnectAt_=millis()+30000u;return false;}
    }
    cfg_={connectStrings_[0].c_str(),connectStrings_[1].c_str(),connectStrings_[2].c_str(),connectStrings_[3].c_str(),connectStrings_[4].c_str(),cfg.insecureTls,connectStrings_[5].c_str(),connectStrings_[6].c_str(),connectStrings_[7].c_str()};
    cancelConnect_.store(false);
    connecting_.store(true,std::memory_order_release);
    if(xTaskCreatePinnedToCore(connectTaskMain,"rdConnect",12288,this,1,nullptr,0)!=pdPASS){
      reconnectAt_=millis()+30000u;connecting_.store(false,std::memory_order_release);return false;
    }
    return true;
  }

  void markSetupComplete(){ ready_=true; }
  void markProtocolError(){ ++protocolErrors_; }

private:
  static void connectTaskMain(void* arg){
    auto* self=static_cast<GeminiLiveDirectClient*>(arg);
    const Config cfg=self->cfg_;
    const bool ok=self->connect(cfg,millis());
    if(self->cancelConnect_.load())self->disconnectInternal(millis());
    else if(!ok){
      self->disconnectInternal(millis(),self->connectBackoffMs_);
      self->connectBackoffMs_=self->connectBackoffMs_<15000u?self->connectBackoffMs_*2u:30000u;
    }
    RoboLog.printf("LEV,GEMINI,CONNECT_RESULT,ok=%u,cancelled=%u\n",unsigned(ok),unsigned(self->cancelConnect_.load()));
    // Publish TLS/parser state only when the worker has finished all access.
    self->connecting_.store(false,std::memory_order_release);
    vTaskDelete(nullptr);
  }

  bool connect(const Config& cfg,uint32_t now){
    if(!ensureScratch()){reconnectAt_=now+30000u;return false;}
    cfg_=cfg; ready_=false; connected_=false; setupSentAt_=0;setupBytes_=0;probePingSentAt_=0;probePongSeen_=false;rxBytesTotal_=0;rxFrames_=0;rxMessages_=0;audioEnvelopeMessages_=0;audioEnvelopeBytes_=0;audioEnvelopeLogAt_=0;directTlsReads_=0;directTlsBytes_=0;directTlsNoData_=0;serviceCalls_=0;serviceBudgetZero_=0;preReadyForcedPolls_=0;rawPeekData_=0;rawPeekNoData_=0;rawPeekFin_=0;rawPeekErr_=0;lastRawPeek_=-99;lastRawErr_=0;lastTlsReadRet_=0;lastRxOpcode_=0;textMessageActive_=false;resetMessagePreview(0);resetRx();
    if(!cfg.apiKey||!cfg.apiKey[0]||!cfg.model||!cfg.voice)return false;
    tls_.stop(); tls_.setTimeout(3); tls_.setHandshakeTimeout(4);
    if(cfg.insecureTls||!cfg.rootCaPem||!cfg.rootCaPem[0])tls_.setInsecure(); else tls_.setCACert(cfg.rootCaPem);
    if(!tls_.connect(HOST,PORT,2500)){reconnectAt_=millis()+2500;return false;}
    uint8_t randomKey[16]; for(size_t i=0;i<sizeof(randomKey);i+=4){uint32_t r=esp_random();memcpy(randomKey+i,&r,4);} char key[32];base64Encode(randomKey,sizeof(randomKey),key,sizeof(key));
    char req[1024];
    int rn=snprintf(req,sizeof(req),
      "GET /ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: RoboDesk-LivingEyes/0.12-living\r\n\r\n",
      cfg.apiKey,HOST,key);
    if(rn<=0||size_t(rn)>=sizeof(req)||tls_.write(reinterpret_cast<const uint8_t*>(req),rn)!=size_t(rn)){tls_.stop();reconnectAt_=now+2500;return false;}
    char headers[1536];headers[0]=0;
    if(!readHttpHeaders(headers,sizeof(headers),5000)||!headerContains(headers," 101 ")||!websocketAcceptMatches(headers,key)){tls_.stop();reconnectAt_=now+2500;return false;}
    const bool directNb=tls_.enableDirectNonBlocking();
    connected_=true;++connects_;lastRxAt_=lastTxAt_=millis();heartbeatPending_=false;
    RoboLog.printf("LEV,GEMINI,TLS_RX_MODE,direct=1,nonblock=%u,fd=%d\n",unsigned(directNb),tls_.socketFd());
    if(!directNb){RoboLog.println("LEV,GEMINI,TLS_NONBLOCK_FAIL");disconnectInternal(now);return false;}
    if(!sendSetup()){RoboLog.println("LEV,GEMINI,SETUP_SEND_FAIL");disconnectInternal(now);return false;}
    RoboLog.printf("LEV,GEMINI,WSS_CONNECTED,profile=v0151_realtime_hybrid,model=%s,voice=%s,setupBytes=%u\n",cfg_.model,cfg_.voice,unsigned(setupBytes_));
    return true;
  }

public:
  void disconnect(uint32_t now){if(connecting()){cancelConnect_.store(true);return;}disconnectInternal(now);}
  bool reconnectDue(uint32_t now){return !connecting()&&!connected()&&int32_t(now-reconnectAt_)>=0;}

  // Hybrid VAD: automatic server-side VAD stays enabled. Manual activityStart/activityEnd
  // are invalid in that mode. Touch/wake capture can finalize a finite stream;
  // AlwaysListening keeps silence flowing for automatic server-side detection.
  bool sendAudioStreamEnd(){return connected()&&sendTextFrame("{\"realtimeInput\":{\"audioStreamEnd\":true}}");}

  bool sendToolResponse(const char* id,const char* name,const char* responseJson){
    if(!id||!name||!responseJson||!connected())return false;
    jsonEscape(id,scratch_->toolIdEsc,sizeof(scratch_->toolIdEsc));jsonEscape(name,scratch_->toolNameEsc,sizeof(scratch_->toolNameEsc));
    int m=snprintf(scratch_->toolMsg,sizeof(scratch_->toolMsg),"{\"toolResponse\":{\"functionResponses\":[{\"id\":\"%s\",\"name\":\"%s\",\"response\":%s}]}}",scratch_->toolIdEsc,scratch_->toolNameEsc,responseJson);
    return m>0&&size_t(m)<sizeof(scratch_->toolMsg)&&sendTextFrame(scratch_->toolMsg);
  }

  bool sendClientTextTurn(const char* text,bool turnComplete=true){
    if(!text||!connected())return false;
    char escaped[900];jsonEscape(text,escaped,sizeof(escaped));char msg[1100];int m=snprintf(msg,sizeof(msg),"{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}],\"turnComplete\":%s}}",escaped,turnComplete?"true":"false");return m>0&&size_t(m)<sizeof(msg)&&sendTextFrame(msg);
  }

  bool sendRealtimeText(const char* text){
    if(!text||!connected())return false;
    char escaped[700];size_t n=0;
    for(const char* p=text;*p&&n+2<sizeof(escaped);++p){char c=*p;if(c=='\"'||c=='\\'){escaped[n++]='\\';escaped[n++]=c;}else if(c=='\n'){escaped[n++]='\\';escaped[n++]='n';}else if(uint8_t(c)>=0x20)escaped[n++]=c;}
    escaped[n]=0; char msg[820];int m=snprintf(msg,sizeof(msg),"{\"realtimeInput\":{\"text\":\"%s\"}}",escaped);return m>0&&size_t(m)<sizeof(msg)&&sendTextFrame(msg);
  }

  bool sendAudio(const uint8_t* pcm,size_t n){
    if(!ready()||!pcm||!n||n>1280)return false;
    size_t bn=base64Encode(pcm,n,scratch_->audioB64,sizeof(scratch_->audioB64)); if(!bn)return false;
    int m=snprintf(scratch_->audioMsg,sizeof(scratch_->audioMsg),"{\"realtimeInput\":{\"audio\":{\"data\":\"%s\",\"mimeType\":\"audio/pcm;rate=16000\"}}}",scratch_->audioB64);
    return m>0&&size_t(m)<sizeof(scratch_->audioMsg)&&sendTextFrame(scratch_->audioMsg);
  }

  void service(uint32_t now,size_t byteBudget){
    if(!connected())return;
    // HOTFIX12: `now` can be stale when connect() performs DNS/TLS/HTTP before
    // returning to the same loop iteration. setupSentAt_ is stamped with
    // millis() after that handshake, so subtracting the older caller timestamp
    // underflows uint32_t and causes an immediate false 10-second timeout.
    // Use a fresh monotonic timestamp for all post-connect timing decisions.
    const uint32_t serviceNow=millis();
    (void)now;
    ++serviceCalls_;
    if(byteBudget==0)++serviceBudgetZero_;

    // SetupComplete must be consumed before any audio can exist. Do not let
    // speaker backpressure suppress the WebSocket handshake receive path.
    size_t effectiveBudget=byteBudget;
    if(!ready_&&effectiveBudget<256){effectiveBudget=256;++preReadyForcedPolls_;}

    if(effectiveBudget){
      uint8_t buf[256];
      size_t done=0;
      while(done<effectiveBudget&&connected_){
        // Observe the raw TCP socket without consuming encrypted TLS records.
        // -1 = no data/EAGAIN, 0 = peer FIN, >0 = encrypted bytes waiting.
        uint8_t raw[8];int rawErr=0;int rp=tls_.rawPeek(raw,sizeof(raw),&rawErr);
        lastRawPeek_=rp;lastRawErr_=rawErr;
        if(rp>0)++rawPeekData_;
        else if(rp==0)++rawPeekFin_;
        else if(rp==-1)++rawPeekNoData_;
        else ++rawPeekErr_;

        int n=tls_.directTlsRead(buf,effectiveBudget-done>sizeof(buf)?sizeof(buf):effectiveBudget-done);
        lastTlsReadRet_=n;++directTlsReads_;
        if(n>0){
          directTlsBytes_+=uint32_t(n);
          for(int i=0;i<n&&connected_;++i)consumeRxByte(buf[i]);
          done+=size_t(n);
          continue;
        }
        if(n==0){++directTlsNoData_;break;}
        RoboLog.printf("LEV,GEMINI,TLS_READ_END,ret=%d,rawPeek=%d,rawErr=%d,frames=%lu,bytes=%lu\n",n,lastRawPeek_,lastRawErr_,(unsigned long)rxFrames_,(unsigned long)rxBytesTotal_);
        disconnectInternal(serviceNow);return;
      }
    }
    if(connected_&&!ready_&&setupSentAt_&&uint32_t(serviceNow-setupSentAt_)>=10000u){
      RoboLog.printf("LEV,GEMINI,SETUP_TIMEOUT,setupBytes=%u,rxFrames=%lu,rxMessages=%lu,rxBytes=%lu,lastOpcode=%u,svc=%lu,budget0=%lu,forced=%lu,tlsReads=%lu,tlsBytes=%lu,noData=%lu,lastTls=%d,tlsBuffered=%d,peekData=%lu,peekNoData=%lu,peekFin=%lu,peekErr=%lu,lastPeek=%d,lastErr=%d,fd=%d\n",unsigned(setupBytes_),(unsigned long)rxFrames_,(unsigned long)rxMessages_,(unsigned long)rxBytesTotal_,unsigned(lastRxOpcode_),(unsigned long)serviceCalls_,(unsigned long)serviceBudgetZero_,(unsigned long)preReadyForcedPolls_,(unsigned long)directTlsReads_,(unsigned long)directTlsBytes_,(unsigned long)directTlsNoData_,lastTlsReadRet_,tls_.tlsBuffered(),(unsigned long)rawPeekData_,(unsigned long)rawPeekNoData_,(unsigned long)rawPeekFin_,(unsigned long)rawPeekErr_,lastRawPeek_,lastRawErr_,tls_.socketFd());
      disconnectInternal(serviceNow);
    }
    if(!effectiveBudget&&heartbeatPending_)heartbeatSentAt_=millis(); // Pause during speaker backpressure.
    if(connected_&&ready_&&effectiveBudget){
      connectBackoffMs_=2500; // Reset after setup succeeded, not just TCP/TLS.
      if(heartbeatPending_&&uint32_t(millis()-heartbeatSentAt_)>=10000u){
        RoboLog.println("LEV,GEMINI,HEARTBEAT_TIMEOUT");disconnectInternal(millis(),5000u);
      }else if(!heartbeatPending_&&uint32_t(millis()-lastRxAt_)>=15000u){
        if(sendFrame(0x9,reinterpret_cast<const uint8_t*>("rd"),2)){heartbeatPending_=true;heartbeatSentAt_=millis();}
      }
    }
  }
};

const char* GeminiLiveDirectClient::HOST="generativelanguage.googleapis.com";
