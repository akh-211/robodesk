#pragma once
#include <stdint.h>
#include <string.h>

// ANCS responses are fragmented streams. Reassemble only the requested tuple
// sequence; reject a different command/UID/app instead of mixing notifications.
class PhoneAncsProtocol {
 public:
  enum class Kind : uint8_t { Identifier, Content, AppName };
  char appId[64]{},title[80]{},body[181]{},appName[40]{};
  void begin(Kind kind,uint32_t uid,const char*id=""){
    kind_=kind;uid_=uid;used_=0;done_=failed_=false;expectedApp_[0]=0;
    if(kind==Kind::Identifier){appId[0]=title[0]=body[0]=appName[0]=0;}
    if(kind==Kind::AppName){if(!id||strlen(id)>=sizeof(expectedApp_)){failed_=true;return;}strcpy(expectedApp_,id);}
  }
  bool feed(const uint8_t*p,size_t n){
    if(done_||failed_||!p||n>sizeof(buffer_)-used_){failed_=true;return false;}
    memcpy(buffer_+used_,p,n);used_+=n;parse();return !failed_;
  }
  bool complete()const{return done_;}bool failed()const{return failed_;}
 private:
  Kind kind_=Kind::Identifier;uint32_t uid_=0;uint8_t buffer_[384]{};size_t used_=0;bool done_=false,failed_=false;char expectedApp_[64]{};
  void parse(){
    size_t at=0;if(used_<1)return;
    if(kind_==Kind::AppName){
      if(buffer_[0]!=1){failed_=true;return;}size_t len=strlen(expectedApp_);if(used_<len+2)return;
      if(memcmp(buffer_+1,expectedApp_,len)||buffer_[len+1]!=0){failed_=true;return;}at=len+2;
    }else{
      if(buffer_[0]!=0){failed_=true;return;}if(used_<5)return;
      uint32_t uid=uint32_t(buffer_[1])|(uint32_t(buffer_[2])<<8)|(uint32_t(buffer_[3])<<16)|(uint32_t(buffer_[4])<<24);
      if(uid!=uid_){failed_=true;return;}at=5;
    }
    const uint8_t ids[2]={uint8_t(kind_==Kind::Content?1:0),3};unsigned count=kind_==Kind::Content?2:1;
    for(unsigned i=0;i<count;++i){
      if(used_<at+3)return;
      if(buffer_[at]!=ids[i]){failed_=true;return;}
      size_t len=size_t(buffer_[at+1])|(size_t(buffer_[at+2])<<8);at+=3;
      char*out=kind_==Kind::Identifier?appId:kind_==Kind::AppName?appName:i?body:title;
      size_t cap=kind_==Kind::Identifier?sizeof(appId):kind_==Kind::AppName?sizeof(appName):i?sizeof(body):sizeof(title);
      if(len>=cap){failed_=true;return;}if(used_<at+len)return;
      for(size_t j=0;j<len;++j){out[j]=(buffer_[at+j]<32||buffer_[at+j]==127)?' ':char(buffer_[at+j]);}
      out[len]=0;at+=len;
    }
    if(used_!=at){failed_=true;return;}done_=true;
  }
};
