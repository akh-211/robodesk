#pragma once
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include "RoboLinkTlsClient.h"

// S3 side of external integrations. The C3 owns configuration and parsing and
// asks for one HTTPS GET at a time over the link; TLS ends here, never on the C3.
// Credentials arrive per request, live in RAM only while it runs, and are wiped after.
namespace roboextfetch {
enum State:uint8_t{Idle,Running,Done};
constexpr size_t BodyMax=8192;
inline std::atomic<uint8_t> state{Idle};
inline uint16_t resultCode=0;
inline char* body=nullptr;inline size_t bodySize=0;
inline String url,bearer;
struct Sink:Stream{
  int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}
  size_t size=0;bool overflow=false;
  size_t write(uint8_t c)override{return write(&c,1);}
  size_t write(const uint8_t*p,size_t n)override{if(!body||n>BodyMax-size){overflow=true;return 0;}memcpy(body+size,p,n);size+=n;return n;}
};
inline void task(void*){
  uint16_t code=502;bodySize=0;
  {
    RoboLinkTlsClient tls;tls.useCertBundle();
    HTTPClient http;http.setConnectTimeout(5000);http.setTimeout(5000);http.setReuse(false);
    if(http.begin(tls,url)){
      http.addHeader("Accept","application/json, text/calendar;q=0.9, */*;q=0.1");
      if(bearer.length()){String auth="Bearer ";auth+=bearer;http.addHeader("Authorization",auth);}
      const int status=http.GET();
      if(status==200){Sink sink;http.writeToStream(&sink);if(sink.overflow)code=413;else{code=200;bodySize=sink.size;body[bodySize]=0;}}
      else if(status>0){code=422;bodySize=size_t(snprintf(body,16,"%d",status));}
    }
    http.end();tls.stop();
  }
  url="";bearer="";
  resultCode=code;RoboDual.tunnelBusy=false;state.store(Done,std::memory_order_release);vTaskDelete(nullptr);
}
// Returns false when the tunnel is not free (Gemini/memory worker owns it) or resources are missing.
inline bool start(const String&u,const String&b){
  uint8_t expected=Idle;if(RoboDual.tunnelBusy.load()||!state.compare_exchange_strong(expected,Running))return false;
  if(!body)body=static_cast<char*>(heap_caps_malloc(BodyMax+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!body){state=Idle;return false;}
  url=u;bearer=b;RoboDual.tunnelBusy=true;
  if(xTaskCreatePinnedToCore(task,"rdExtFetch",12288,nullptr,1,nullptr,0)!=pdPASS){url="";bearer="";RoboDual.tunnelBusy=false;state=Idle;return false;}
  return true;
}
inline bool running(){return state.load()==Running;}
}
