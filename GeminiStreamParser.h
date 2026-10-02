#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

class GeminiStreamSink {
public:
  virtual ~GeminiStreamSink() {}
  virtual void onGeminiSetupComplete() = 0;
  virtual void onGeminiAudio(const uint8_t* data, size_t len) = 0;
  virtual void onGeminiInputTranscript(const char* text) = 0;
  virtual void onGeminiOutputTranscript(const char* text) = 0;
  virtual void onGeminiTurnComplete() = 0;
  virtual void onGeminiWaitingForInput() = 0;
  virtual void onGeminiGenerationComplete() = 0;
  virtual void onGeminiInterrupted() = 0;
  virtual void onGeminiGoAway() = 0;
  virtual void onGeminiSessionHandle(const char* handle) = 0;
  virtual void onGeminiToolCall(const char* id,const char* name,const char* argsJson) = 0;
  virtual void onGeminiToolCancelled(const char*) {}
  virtual void onGeminiUsage(uint32_t) {}
  virtual void onGeminiTranscriptTruncated(bool) {}
  virtual void onGeminiGoAwayTime(const char*) {onGeminiGoAway();}
  virtual void onGeminiProtocolError(const char* text) = 0;
};

class GeminiStreamParser {
  GeminiStreamSink* sink_ = 0;
  static const size_t META_CAP = 8192;
  static const size_t TEXT_CAP = 1024;
  char meta_[META_CAP];
  size_t metaLen_ = 0;
  bool metadataOverflow_=false;
  bool inlineDataSeen_ = false;
  bool inAudioData_ = false;
  bool dataKeySeen_ = false;
  uint8_t dataValueState_ = 0; // 0=idle, 1=await colon, 2=await opening quote
  bool audioDataStarted_ = false;
  uint32_t decodedAudioBytes_ = 0;
  uint8_t b64_[4];
  uint8_t b64Count_ = 0;
  uint8_t audio_[384];
  size_t audioLen_ = 0;

  // HOTFIX16: keep large parser/tool scratch out of the Arduino loop task stack.
  // Tool callbacks can be triggered from endMessage(), so stack-local copies here
  // used to nest with RoboBrain/tool-response buffers and could exhaust the task.
  char toolObj_[1400];
  char toolId_[96];
  char toolName_[96];
  char toolArgs_[900];
  char textScratch_[TEXT_CAP];
  char handleScratch_[1024];

  static bool isWs(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }

  bool endsWith(const char* s) const {
    size_t n = strlen(s);
    return n <= metaLen_ && memcmp(meta_ + metaLen_ - n, s, n) == 0;
  }

  void appendMeta(char c) {
    if (metaLen_ + 1 < META_CAP) {
      meta_[metaLen_++] = c;
      meta_[metaLen_] = 0;
    } else metadataOverflow_=true;
  }

  static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
  }

  void flushAudio() {
    if (audioLen_ && sink_) sink_->onGeminiAudio(audio_, audioLen_);
    audioLen_ = 0;
  }

  void emitAudioByte(uint8_t b) {
    ++decodedAudioBytes_;
    audio_[audioLen_++] = b;
    if (audioLen_ == sizeof(audio_)) flushAudio();
  }

  void decodeQuartet() {
    int a=b64val(char(b64_[0])), b=b64val(char(b64_[1]));
    int c=b64val(char(b64_[2])), d=b64val(char(b64_[3]));
    if (a < 0 || b < 0) return;
    emitAudioByte(uint8_t((a<<2) | (b>>4)));
    if (c == -2) return;
    if (c < 0) return;
    emitAudioByte(uint8_t(((b&15)<<4) | (c>>2)));
    if (d == -2) return;
    if (d < 0) return;
    emitAudioByte(uint8_t(((c&3)<<6) | d));
  }

  void feedB64(char c) {
    if (isWs(c)) return;
    int v=b64val(c);
    if (v == -1) return;
    b64_[b64Count_++] = uint8_t(c);
    if (b64Count_ == 4) { decodeQuartet(); b64Count_ = 0; }
  }

  static bool containsBool(const char* src, const char* key, bool value) {
    const char* p = strstr(src, key);
    if (!p) return false;
    p += strlen(key);
    while (*p && *p != ':') ++p;
    if (*p != ':') return false;
    ++p; while (*p && isWs(*p)) ++p;
    return value ? strncmp(p,"true",4)==0 : strncmp(p,"false",5)==0;
  }

  static void appendUtf8(char* out, size_t cap, size_t& n, uint32_t cp) {
    if (cp <= 0x7f) { if(n+1<cap) out[n++]=char(cp); return; }
    if (cp <= 0x7ff) {
      if(n+2<cap){ out[n++]=char(0xc0|(cp>>6)); out[n++]=char(0x80|(cp&0x3f)); }
      return;
    }
    if (cp <= 0xffff) {
      if(n+3<cap){ out[n++]=char(0xe0|(cp>>12)); out[n++]=char(0x80|((cp>>6)&0x3f)); out[n++]=char(0x80|(cp&0x3f)); }
    }
  }

  static int hexVal(char c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
  }

  static bool extractStringValue(const char* src, const char* key, char* out, size_t cap) {
    if (!cap) return false;
    out[0]=0;
    const char* p=strstr(src,key); if(!p) return false;
    p=strchr(p,':'); if(!p) return false; ++p; while(*p && isWs(*p)) ++p;
    if(*p!='\"') return false;
    ++p;
    size_t n=0;
    while(*p && *p!='\"'){
      unsigned char c=static_cast<unsigned char>(*p++);
      if(c=='\\'){
        char e=*p++; if(!e) break;
        if(e=='n') c='\n'; else if(e=='r') c='\r'; else if(e=='t') c='\t';
        else if(e=='b') c='\b'; else if(e=='f') c='\f'; else if(e=='u'){
          int h0=hexVal(p[0]),h1=hexVal(p[1]),h2=hexVal(p[2]),h3=hexVal(p[3]);
          if(h0>=0&&h1>=0&&h2>=0&&h3>=0){ uint32_t cp=uint32_t((h0<<12)|(h1<<8)|(h2<<4)|h3); appendUtf8(out,cap,n,cp); p+=4; continue; }
          c='?';
        } else c=static_cast<unsigned char>(e);
      }
      if(n+1<cap) out[n++]=char(c);
    }
    out[n]=0; return n>0;
  }


  static const char* matchingBrace(const char* start){
    if(!start||*start!='{')return 0;
    int depth=0;bool str=false,esc=false;
    for(const char*p=start;*p;p++){char c=*p;if(str){if(esc){esc=false;continue;}if(c=='\\'){esc=true;continue;}if(c=='"')str=false;continue;}if(c=='"'){str=true;continue;}if(c=='{')++depth;else if(c=='}'&&--depth==0)return p;}return 0;
  }

  static bool extractObjectValue(const char* src,const char* key,char* out,size_t cap){
    if(!src||!key||!out||cap<3)return false;
    out[0]=0;const char*p=strstr(src,key);if(!p)return false;p=strchr(p,':');if(!p)return false;++p;while(*p&&isWs(*p))++p;if(*p!='{')return false;const char*e=matchingBrace(p);if(!e)return false;size_t n=size_t(e-p+1);if(n>=cap)n=cap-1;memcpy(out,p,n);out[n]=0;return true;
  }

  void emitToolCalls(){
    const char* tc=strstr(meta_,"\"toolCall\"");if(!tc)return;const char*fc=strstr(tc,"\"functionCalls\"");if(!fc)return;const char*arr=strchr(fc,'[');if(!arr)return;const char*p=arr+1;unsigned emitted=0;
    while(*p&&emitted<4){
      while(*p&&(isWs(*p)||*p==','))++p;
      if(*p==']'||!*p)break;
      if(*p!='{'){++p;continue;}
      const char*e=matchingBrace(p);
      if(!e)break;
      size_t len=size_t(e-p+1);
      if(len>=sizeof(toolObj_))len=sizeof(toolObj_)-1;
      memcpy(toolObj_,p,len);toolObj_[len]=0;toolId_[0]=toolName_[0]=0;strcpy(toolArgs_,"{}");
      extractStringValue(toolObj_,"\"id\"",toolId_,sizeof(toolId_));
      extractStringValue(toolObj_,"\"name\"",toolName_,sizeof(toolName_));
      extractObjectValue(toolObj_,"\"args\"",toolArgs_,sizeof(toolArgs_));
      if(toolId_[0]&&toolName_[0]&&sink_)sink_->onGeminiToolCall(toolId_,toolName_,toolArgs_);
      ++emitted;p=e+1;
    }
  }

  static bool extractText(const char* src, const char* section, char* out, size_t cap,bool*truncated=nullptr) {
    if (!cap) return false;
    out[0]=0;
    if(truncated)*truncated=false;
    const char* p = strstr(src, section); if(!p) return false;
    const char* start=strchr(p,':');if(!start)return false;++start;while(isWs(*start))++start;
    const char*end=matchingBrace(start);if(!end)return false;
    p = strstr(start, "\"text\""); if(!p||p>=end) return false;
    p = strchr(p, ':'); if(!p) return false; ++p; while(*p && isWs(*p)) ++p;
    if(*p!='\"') return false;
    ++p;
    size_t n=0;
    while(*p && *p!='\"'){
      unsigned char c=static_cast<unsigned char>(*p++);
      if(c=='\\'){
        char e=*p++; if(!e) break;
        if(e=='n') c='\n'; else if(e=='r') c='\r'; else if(e=='t') c='\t';
        else if(e=='b') c='\b'; else if(e=='f') c='\f'; else if(e=='u'){
          int h0=hexVal(p[0]),h1=hexVal(p[1]),h2=hexVal(p[2]),h3=hexVal(p[3]);
          if(h0>=0&&h1>=0&&h2>=0&&h3>=0){ uint32_t cp=uint32_t((h0<<12)|(h1<<8)|(h2<<4)|h3); size_t needed=cp<=0x7f?1:cp<=0x7ff?2:3;if(truncated&&(n+needed>=cap||cp==0))*truncated=true;if(cp==0)cp='?';appendUtf8(out,cap,n,cp); p+=4; continue; }
          c='?';
        } else c=static_cast<unsigned char>(e);
      }
      if(n+1<cap) out[n++]=char(c);else if(truncated)*truncated=true;
    }
    out[n]=0; return n>0;
  }

public:
  explicit GeminiStreamParser(GeminiStreamSink* sink=0): sink_(sink) { beginMessage(); }
  void setSink(GeminiStreamSink* sink){ sink_=sink; }

  void beginMessage(){
    metadataOverflow_=false;metaLen_=0; meta_[0]=0; inlineDataSeen_=false; inAudioData_=false; dataKeySeen_=false; dataValueState_=0; audioDataStarted_=false; decodedAudioBytes_=0; b64Count_=0; audioLen_=0;
  }

  void feed(const uint8_t* data,size_t n){
    for(size_t i=0;i<n;i++){
      char c=char(data[i]);
      if(inAudioData_){
        if(c=='\"'){
          if(b64Count_) b64Count_=0;
          flushAudio(); inAudioData_=false; appendMeta(c);
        } else feedB64(c);
        continue;
      }
      appendMeta(c);
      if(!inlineDataSeen_ && endsWith("\"inlineData\"")) inlineDataSeen_=true;

      // The original v0.15 parser only recognized the exact compact token
      //   "data":"..."
      // Some protobuf JSON serializers are free to emit whitespace as
      //   "data" : "..."
      // so detect the key and then tolerate JSON whitespace around ':'.
      if(inlineDataSeen_){
        if(!dataKeySeen_ && endsWith("\"data\"")){
          dataKeySeen_=true; dataValueState_=1;
          continue;
        }
        if(dataKeySeen_){
          if(dataValueState_==1){
            if(isWs(c)) continue;
            if(c==':'){ dataValueState_=2; continue; }
            dataKeySeen_=false; dataValueState_=0;
          } else if(dataValueState_==2){
            if(isWs(c)) continue;
            if(c=='\"'){
              inAudioData_=true; audioDataStarted_=true; b64Count_=0;
              dataKeySeen_=false; dataValueState_=0;
              continue;
            }
            dataKeySeen_=false; dataValueState_=0;
          }
        }
      }
    }
  }

  bool sawModelTurn() const { return strstr(meta_,"\"modelTurn\"") != 0; }
  bool sawInlineData() const { return inlineDataSeen_; }
  bool startedAudioData() const { return audioDataStarted_; }
  uint32_t decodedAudioBytes() const { return decodedAudioBytes_; }

  void endMessage(){
    flushAudio();
    if(!sink_) return;
    if(metadataOverflow_){sink_->onGeminiProtocolError("metadata_too_large");return;}
    if(strstr(meta_,"\"setupComplete\"")) sink_->onGeminiSetupComplete();
    bool truncated=false;
    if(extractText(meta_,"\"inputTranscription\"",textScratch_,sizeof(textScratch_),&truncated)) {sink_->onGeminiInputTranscript(textScratch_);if(truncated)sink_->onGeminiTranscriptTruncated(true);}
    if(extractText(meta_,"\"outputTranscription\"",textScratch_,sizeof(textScratch_),&truncated)) {sink_->onGeminiOutputTranscript(textScratch_);if(truncated)sink_->onGeminiTranscriptTruncated(false);}
    if(containsBool(meta_,"\"interrupted\"",true)) sink_->onGeminiInterrupted();
    if(containsBool(meta_,"\"generationComplete\"",true)) sink_->onGeminiGenerationComplete();
    if(containsBool(meta_,"\"waitingForInput\"",true)) sink_->onGeminiWaitingForInput();
    if(containsBool(meta_,"\"turnComplete\"",true)) sink_->onGeminiTurnComplete();
    if(strstr(meta_,"\"goAway\"")){extractStringValue(meta_,"\"timeLeft\"",textScratch_,sizeof(textScratch_));sink_->onGeminiGoAwayTime(textScratch_);}
    const char*cancellation=strstr(meta_,"\"toolCallCancellation\"");
    if(cancellation){const char*ids=strstr(cancellation,"\"ids\"");const char*p=ids?strchr(ids,'['):nullptr;if(p){++p;while(*p&&*p!=']'){while(*p&&(isWs(*p)||*p==','))++p;if(*p!='"')break;const char*end=strchr(++p,'"');if(!end)break;size_t n=size_t(end-p);if(n&&n<sizeof(toolId_)){memcpy(toolId_,p,n);toolId_[n]=0;sink_->onGeminiToolCancelled(toolId_);}p=end+1;}}}
    if(strstr(meta_,"\"usageMetadata\"")){const char*p=strstr(meta_,"\"totalTokenCount\"");if(p&&(p=strchr(p,':')))sink_->onGeminiUsage(uint32_t(strtoul(p+1,nullptr,10)));}
    emitToolCalls();
    if(strstr(meta_,"\"sessionResumptionUpdate\"")&&containsBool(meta_,"\"resumable\"",false))sink_->onGeminiSessionHandle("");
    else if(strstr(meta_,"\"sessionResumptionUpdate\"") && extractStringValue(meta_,"\"newHandle\"",handleScratch_,sizeof(handleScratch_))) sink_->onGeminiSessionHandle(handleScratch_);
    if(strstr(meta_,"\"error\"")){
      if(extractStringValue(meta_,"\"message\"",textScratch_,sizeof(textScratch_))) sink_->onGeminiProtocolError(textScratch_);
      else if(extractText(meta_,"\"error\"",textScratch_,sizeof(textScratch_))) sink_->onGeminiProtocolError(textScratch_);
      else sink_->onGeminiProtocolError("Gemini Live returned an error object");
    }
  }
};
