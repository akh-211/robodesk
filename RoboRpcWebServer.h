#pragma once
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include "RoboDualRuntime.h"

// WebServer-compatible local dispatch on S3. Only the authenticated C3 can call
// it, through the internal UART; this class never opens a network interface.
enum HTTPMethod {HTTP_GET=1,HTTP_POST=2};
enum {BASIC_AUTH=0,CONTENT_LENGTH_UNKNOWN=-1,UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload {int status=UPLOAD_FILE_ABORTED;String filename,name;uint8_t*buf=nullptr;size_t currentSize=0;};
class WebServer {
  struct Route{String path;HTTPMethod method;std::function<void()> handler;};
  std::vector<Route> routes_;std::function<void()> missing_;JsonDocument request_;HTTPUpload upload_;
  std::function<void(const uint8_t*,size_t)> binary_;
  uint16_t code_=500;String type_="text/plain",body_;robolink::RequestCache cache_;bool responded_=false;
public:
  explicit WebServer(int){}
  template<class F>void on(const char*p,HTTPMethod m,F f){routes_.push_back({String(p),m,f});}
  template<class F,class G>void on(const char*p,HTTPMethod m,F f,G){on(p,m,f);}
  template<class F>void onNotFound(F f){missing_=f;}
  void collectHeaders(const char**,size_t){}void begin(){}
  bool authenticate(const char*,const char*){return true;}void requestAuthentication(int,const char*,const char*){}
  String header(const char*name)const{return !strcmp(name,"Host")?String("robot-uart"):(!strcmp(name,"Origin")?String("http://robot-uart"):String());}
  String uri()const{return String(request_["uri"]|"");}HTTPMethod method()const{return HTTPMethod(request_["method"]|0);}
  bool hasArg(const char*k)const{return !request_["args"][k].isNull();}
  String arg(const char*k)const{return String(request_["args"][k]|"");}
  int args()const{return int(request_["args"].size());}
  String argName(int i)const{int n=0;for(JsonPairConst p:request_["args"].as<JsonObjectConst>())if(n++==i)return String(p.key().c_str());return String();}
  String arg(int i)const{return arg(argName(i).c_str());}
  void sendHeader(const char*,const char*,bool=false){}void setContentLength(size_t){}
  void send(int code,const char*type,const String&body=String()){code_=uint16_t(code);type_=type;body_=body;responded_=true;}
  void send_P(int code,const char*type,const char*body){send(code,type,String(body));}
  void sendContent(const char*p,size_t n){if(body_.length()+n<RoboDualRuntime::MessageMax)body_.concat(p,n);else send(507,"text/plain","Robot response exceeds link capacity");}
  template<class F>void setBinaryHandler(F f){binary_=f;}
  HTTPUpload&upload(){return upload_;}
  void handleClient(){
    if(!RoboDual.messageReady(robolink::Rpc))return;const uint32_t id=RoboDual.messageId();
    const uint32_t peer=RoboDual.link.health().peerSession;
    if(cache_.shouldExecute(peer,id)){code_=500;type_="text/plain";body_="No robot handler";responded_=false;
      if(RoboDual.messageSize()&&RoboDual.message()[0]==1&&binary_)binary_(RoboDual.message(),RoboDual.messageSize());
      else if(deserializeJson(request_,RoboDual.message(),RoboDual.messageSize()))send(400,"text/plain","Invalid robot request");
      else{bool found=false;for(auto&r:routes_)if(r.path==uri()&&r.method==method()){found=true;r.handler();break;}if(!found&&missing_)missing_();}
      if(!responded_)send(503,"text/plain","Robot service unavailable");
    }
    RoboDual.releaseMessage();const size_t typeLen=type_.length();size_t n=3+typeLen+body_.length();
    if(typeLen>127||n>RoboDualRuntime::MessageMax){code_=507;type_="text/plain";body_="Robot response exceeds link capacity";n=3+type_.length()+body_.length();}
    // PSRAM on the robot; no second large response allocation on C3.
    uint8_t*out=static_cast<uint8_t*>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!out)return;robolink::put16(out,code_);out[2]=uint8_t(type_.length());memcpy(out+3,type_.c_str(),type_.length());memcpy(out+3+type_.length(),body_.c_str(),body_.length());RoboDual.sendMessage(robolink::Reply,id,out,n);free(out);request_.clear();
  }
};
