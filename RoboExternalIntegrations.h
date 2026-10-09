#pragma once

// Optional, read-only integrations owned exclusively by the ESP32-C3 gateway.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include <cstdlib>
#include <math.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include "DashboardSecurity.h"
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "RuntimeSettings.h"

namespace roboexternal {

constexpr uint32_t ConfigMagic = 0x52455831u;
constexpr size_t BodyCapacity = 8192;
constexpr size_t MinFreeHeapBeforeRequest = 98304;
constexpr size_t MinLargestBlockBeforeRequest = 32768;
constexpr size_t MinFreeHeapRemote = 24576;
constexpr size_t MinLargestBlockRemote = 12288;
constexpr uint32_t WeatherPeriodMs = 15u * 60u * 1000u;
constexpr uint32_t HomeAssistantPeriodMs = 60u * 1000u;
constexpr uint32_t CalendarPeriodMs = 30u * 60u * 1000u;

struct Config {
  uint32_t magic;
  char homeAssistantBase[128];
  char homeAssistantToken[192];
  char entities[3][48];
  char calendarUrl[256];
  float latitude;
  float longitude;
  uint8_t weatherEnabled;
  uint32_t crc;
};

struct EntityState { char value[25]; uint32_t updatedAt=0; bool valid=false; };
struct Snapshot {
  char weather[96]{};
  char calendar[220]{};
  EntityState entities[3]{};
  float temperature=0;
  int weatherCode=-1;
  uint32_t weatherAt=0,calendarAt=0;
  uint8_t weatherError=0,homeAssistantError=0,calendarError=0;
  bool weatherValid=false,calendarValid=false;
};

inline Config config{};
inline Snapshot snapshot{};
inline portMUX_TYPE stateLock=portMUX_INITIALIZER_UNLOCKED;
inline std::atomic<bool> requestBusy{false},refreshRequested{false};
inline uint32_t nextWeatherAt=0,nextHomeAssistantAt=0,nextCalendarAt=0;
inline uint8_t nextEntity=0,nextProvider=0;
inline uint8_t selectedProvider=0;
// Only the single external worker owns this buffer while requestBusy is true.
inline char* responseBody=nullptr;

inline uint32_t crc32(const uint8_t* data,size_t size) {
  uint32_t crc=0xffffffffu;
  for(size_t i=0;i<size;++i){crc^=data[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&uint32_t(0-int(crc&1)));}
  return ~crc;
}
inline uint32_t configCrc(const Config& c){return crc32(reinterpret_cast<const uint8_t*>(&c),offsetof(Config,crc));}
inline bool validText(const char* s,size_t cap){return s&&memchr(s,0,cap)!=nullptr;}
inline bool httpsUrl(const char* url,size_t cap,bool allowQuery) {
  if(!validText(url,cap)||strncmp(url,"https://",8))return false;
  const char* host=url+8;const char* end=strpbrk(host,"/?");if(!end)end=host+strlen(host);if(end==host||*host=='.'||memchr(host,'@',size_t(end-host)))return false;
  for(const unsigned char* p=reinterpret_cast<const unsigned char*>(url);*p;++p){
    if(*p<=0x20||*p>=0x7f||*p=='\\'||*p=='#'||(!allowQuery&&*p=='?'))return false;
  }
  return true;
}
inline bool validEntity(const char* s,size_t cap){
  if(!validText(s,cap)||!*s)return false;const char*dot=strchr(s,'.');if(!dot||dot==s||!dot[1]||strchr(dot+1,'.'))return false;
  for(const char*p=s;*p;++p)if(*p!='.'&&!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))return false;
  return true;
}
inline bool load() {
  Preferences p;if(!p.begin("robodesk_ext",true))return false;
  Config loaded{};const bool ok=p.getBytesLength("config")==sizeof(loaded)&&p.getBytes("config",&loaded,sizeof(loaded))==sizeof(loaded);p.end();
  if(!ok||loaded.magic!=ConfigMagic||loaded.crc!=configCrc(loaded)||!validText(loaded.homeAssistantBase,sizeof(loaded.homeAssistantBase))||!validText(loaded.homeAssistantToken,sizeof(loaded.homeAssistantToken))||!validText(loaded.calendarUrl,sizeof(loaded.calendarUrl))||loaded.weatherEnabled>1||!isfinite(loaded.latitude)||!isfinite(loaded.longitude)||loaded.latitude< -90||loaded.latitude>90||loaded.longitude< -180||loaded.longitude>180||(loaded.homeAssistantBase[0]&&!httpsUrl(loaded.homeAssistantBase,sizeof(loaded.homeAssistantBase),false))||(loaded.calendarUrl[0]&&!httpsUrl(loaded.calendarUrl,sizeof(loaded.calendarUrl),true)))return false;
  for(const auto&entity:loaded.entities)if(entity[0]&&!validEntity(entity,sizeof(entity)))return false;
  config=loaded;return true;
}
inline bool save(Config next) {
  next.magic=ConfigMagic;next.crc=configCrc(next);Preferences p;if(!p.begin("robodesk_ext",false))return false;
  const bool ok=p.putBytes("config",&next,sizeof(next))==sizeof(next);p.end();if(ok)config=next;return ok;
}
inline bool configuredWeather(){return config.weatherEnabled&&isfinite(config.latitude)&&isfinite(config.longitude)&&config.latitude>=-90&&config.latitude<=90&&config.longitude>=-180&&config.longitude<=180;}
inline bool configuredHomeAssistant(){return config.homeAssistantBase[0]&&config.homeAssistantToken[0]&&httpsUrl(config.homeAssistantBase,sizeof(config.homeAssistantBase),false);}
inline bool configuredCalendar(){return config.calendarUrl[0]&&httpsUrl(config.calendarUrl,sizeof(config.calendarUrl),true);}

struct HttpSink { char* body;size_t cap;size_t size;bool overflow; };
inline esp_err_t httpEvent(esp_http_client_event_t* event) {
  if(event&&event->event_id==HTTP_EVENT_ON_DATA&&event->data&&event->data_len>0){auto*s=static_cast<HttpSink*>(event->user_data);const size_t n=size_t(event->data_len);if(!s||n>s->cap-s->size){if(s)s->overflow=true;return ESP_FAIL;}memcpy(s->body+s->size,event->data,n);s->size+=n;s->body[s->size]=0;}
  return ESP_OK;
}
// Set by the dual-board gateway so TLS ends on the S3; returns the same error codes as get(). Null on single-board builds.
inline uint8_t (*remoteFetch)(const String& url,const char* bearer,char* out,size_t cap,size_t& size)=nullptr;
inline uint8_t get(const String& url,const char* bearer,HttpSink& sink) {
  sink={responseBody,BodyCapacity,0,false};if(!responseBody)return 1;responseBody[0]=0;
  if(remoteFetch){size_t n=0;const uint8_t error=remoteFetch(url,bearer,responseBody,BodyCapacity,n);sink.size=n;sink.overflow=error==2;return error;}
  esp_http_client_config_t cfg{};cfg.url=url.c_str();cfg.event_handler=httpEvent;cfg.user_data=&sink;cfg.timeout_ms=7000;cfg.buffer_size=512;cfg.buffer_size_tx=512;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_redirect=true;cfg.keep_alive_enable=false;
  esp_http_client_handle_t client=esp_http_client_init(&cfg);if(!client)return 1;
  esp_http_client_set_method(client,HTTP_METHOD_GET);
  esp_http_client_set_header(client,"Accept","application/json, text/calendar;q=0.9, */*;q=0.1");
  if(bearer&&*bearer){String auth=String("Bearer ")+bearer;esp_http_client_set_header(client,"Authorization",auth.c_str());}
  const esp_err_t result=esp_http_client_perform(client);const int code=esp_http_client_get_status_code(client);esp_http_client_cleanup(client);
  if(sink.overflow)return 2;if(result!=ESP_OK)return 3;if(code!=200)return code>=400&&code<500?uint8_t(code==401||code==403?4:5):6;return 0;
}
inline void scrub(char* out,size_t cap,const char* input,size_t maxChars) {
  if(!cap)return;size_t n=0;if(input)for(const unsigned char*p=reinterpret_cast<const unsigned char*>(input);*p&&n+1<cap&&n<maxChars;++p){unsigned char c=*p;if(c<0x20||c>=0x7f)c=' ';if(c=='|'||c=='['||c==']'||c=='{'||c=='}')c=' ';out[n++]=char(c);}while(n&&out[n-1]==' ')--n;out[n]=0;
}
inline const char* weatherName(int code){switch(code){case 0:return"clear";case 1:case 2:return"partly cloudy";case 3:return"cloudy";case 45:case 48:return"fog";case 51:case 53:case 55:return"drizzle";case 61:case 63:case 65:return"rain";case 71:case 73:case 75:return"snow";case 80:case 81:case 82:return"rain showers";case 95:case 96:case 99:return"thunderstorm";default:return"conditions unavailable";}}
inline uint8_t fetchWeather(const Config& c) {
  char lat[20],lon[20];snprintf(lat,sizeof(lat),"%.4f",double(c.latitude));snprintf(lon,sizeof(lon),"%.4f",double(c.longitude));
  String url="https://api.open-meteo.com/v1/forecast?latitude=";url+=lat;url+="&longitude=";url+=lon;url+="&current=temperature_2m,apparent_temperature,precipitation,weather_code&timezone=auto";
  HttpSink sink{};uint8_t error=get(url,nullptr,sink);Snapshot s;portENTER_CRITICAL(&stateLock);s=snapshot;portEXIT_CRITICAL(&stateLock);s.weatherError=error;
  if(!error){JsonDocument doc;if(deserializeJson(doc,responseBody,sink.size)){error=7;s.weatherError=error;}else{JsonObjectConst current=doc["current"].as<JsonObjectConst>();const float temp=current["temperature_2m"]|NAN;const int code=current["weather_code"]|-1;if(!isfinite(temp)||temp< -100||temp>80||code<0||code>99){error=7;s.weatherError=error;}else{s.temperature=temp;s.weatherCode=code;s.weatherValid=true;s.weatherAt=millis();snprintf(s.weather,sizeof(s.weather),"%.1f C, %s",double(temp),weatherName(code));}}}
  portENTER_CRITICAL(&stateLock);snapshot=s;portEXIT_CRITICAL(&stateLock);return error;
}
inline uint8_t fetchEntity(const Config& c,uint8_t index) {
  if(index>=3||!validEntity(c.entities[index],sizeof(c.entities[index])))return 0;
  String url=c.homeAssistantBase;while(url.endsWith("/"))url.remove(url.length()-1);url+="/api/states/";url+=c.entities[index];
  HttpSink sink{};uint8_t error=get(url,c.homeAssistantToken,sink);Snapshot s;portENTER_CRITICAL(&stateLock);s=snapshot;portEXIT_CRITICAL(&stateLock);
  if(!error){JsonDocument doc;if(deserializeJson(doc,responseBody,sink.size)){error=7;}else{const char*value=doc["state"]|"";if(!*value||strlen(value)>64)error=7;else{scrub(s.entities[index].value,sizeof(s.entities[index].value),value,24);s.entities[index].valid=true;s.entities[index].updatedAt=millis();}}}
  s.homeAssistantError=error;portENTER_CRITICAL(&stateLock);snapshot=s;portEXIT_CRITICAL(&stateLock);return error;
}
inline bool parseUtc(const char* value,time_t* out) {
  if(!value||!out||strlen(value)<8)return false;int y=0,mo=0,d=0,h=0,m=0,s=0;const size_t length=strlen(value);
  if(length==8){if(sscanf(value,"%4d%2d%2d",&y,&mo,&d)!=3)return false;}
  else if(length>=15){if(sscanf(value,"%4d%2d%2dT%2d%2d%2d",&y,&mo,&d,&h,&m,&s)!=6)return false;}
  else return false;
  if(y<2020||y>2100||mo<1||mo>12||d<1||h>23||m>59||s>59)return false;
  const bool leap=(y%4==0&&y%100!=0)||y%400==0;const uint8_t monthDays[12]={31,uint8_t(leap?29:28),31,30,31,30,31,31,30,31,30,31};if(d>monthDays[mo-1])return false;
  int yy=y-(mo<=2);const int era=(yy>=0?yy:yy-399)/400;const unsigned yoe=unsigned(yy-era*400);const unsigned mp=unsigned(int(mo)+(mo>2?-3:9));const unsigned doy=(153*mp+2)/5+unsigned(d)-1;const unsigned doe=yoe*365+yoe/4-yoe/100+doy;const int64_t days=int64_t(era)*146097+int64_t(doe)-719468;
  int64_t epoch=days*86400+h*3600+m*60+s;if(length==15||length>16||(length==16&&value[15]!='Z'))return false;*out=time_t(epoch);return true;
}
struct Event {time_t start=0;char title[65]{};};
inline void addEvent(Event out[3],time_t start,const char* title) {
  char clean[65];scrub(clean,sizeof(clean),title,64);if(!clean[0])return;int slot=-1;for(int i=0;i<3;++i)if(!out[i].start||start<out[i].start){slot=i;break;}if(slot<0)return;for(int i=2;i>slot;--i)out[i]=out[i-1];out[slot].start=start;strcpy(out[slot].title,clean);
}
inline void parseIcsLine(const char*line,bool&inEvent,time_t&start,bool&hasStart,char title[128],Event out[3]) {
  if(!strcmp(line,"BEGIN:VEVENT")){inEvent=true;start=0;hasStart=false;title[0]=0;return;}if(!strcmp(line,"END:VEVENT")){if(inEvent&&hasStart&&start>=time(nullptr)&&start<=time(nullptr)+30*86400)addEvent(out,start,title);inEvent=false;return;}if(!inEvent)return;
  if(!strncmp(line,"DTSTART",7)){const char*colon=strchr(line,':');if(!colon||strstr(line,"TZID="))return;const char*v=colon+1;if(strlen(v)==8){char date[16];snprintf(date,sizeof(date),"%.8sT000000",v);hasStart=parseUtc(date,&start);}else hasStart=parseUtc(v,&start);}
  else if(!strncmp(line,"SUMMARY",7)&&(line[7]==':'||line[7]==';')){const char*colon=strchr(line,':');if(!colon)return;const char*p=colon+1;size_t n=0;for(;*p&&n+1<128;++p){if(*p=='\\'&&p[1]){++p;if(*p=='n'||*p=='N')title[n++]=' ';else title[n++]=*p;}else title[n++]=*p;}title[n]=0;}
}
inline uint8_t fetchCalendar(const Config& c) {
  HttpSink sink{};uint8_t error=get(c.calendarUrl,nullptr,sink);Snapshot s;portENTER_CRITICAL(&stateLock);s=snapshot;portEXIT_CRITICAL(&stateLock);s.calendarError=error;
  if(!error&&time(nullptr)<1700000000){error=8;s.calendarError=error;}
  if(!error){Event events[3]{};bool inEvent=false,hasStart=false;time_t start=0;char title[128]{};char line[384]{};size_t n=0;
    char logical[384]{};size_t logicalSize=0;
    auto flush=[&](){if(logicalSize){logical[logicalSize]=0;parseIcsLine(logical,inEvent,start,hasStart,title,events);logicalSize=0;}};
    for(size_t i=0;i<=sink.size;++i){char ch=i<sink.size?responseBody[i]:'\n';if(ch=='\r')continue;if(ch=='\n'){line[n]=0;if((line[0]==' '||line[0]=='\t')&&logicalSize){size_t tail=strlen(line+1);if(tail>sizeof(logical)-1-logicalSize){error=2;s.calendarError=error;break;}memcpy(logical+logicalSize,line+1,tail);logicalSize+=tail;}else{flush();logicalSize=n;if(logicalSize>=sizeof(logical)){error=2;s.calendarError=error;break;}memcpy(logical,line,n);}n=0;}else if(n+1<sizeof(line))line[n++]=ch;else{error=2;s.calendarError=error;break;}}
    flush();
    if(!error){String summary;for(const auto&e:events)if(e.start){if(summary.length())summary+="; ";struct tm tmv{};gmtime_r(&e.start,&tmv);char when[24];strftime(when,sizeof(when),"%Y-%m-%d %H:%MZ",&tmv);summary+=when;summary+=' ';summary+=e.title;}if(summary.length()){scrub(s.calendar,sizeof(s.calendar),summary.c_str(),sizeof(s.calendar)-1);s.calendarValid=true;s.calendarAt=millis();}else{s.calendar[0]=0;s.calendarValid=true;s.calendarAt=millis();}}
  }
  portENTER_CRITICAL(&stateLock);snapshot=s;portEXIT_CRITICAL(&stateLock);return error;
}
inline bool jobAvailable(uint8_t provider,uint32_t now) {
  if(provider==0)return configuredWeather()&&int32_t(now-nextWeatherAt)>=0;
  if(provider==1){if(!configuredHomeAssistant()||int32_t(now-nextHomeAssistantAt)<0)return false;for(const auto&e:config.entities)if(validEntity(e,sizeof(e)))return true;return false;}
  return configuredCalendar()&&time(nullptr)>=1700000000&&int32_t(now-nextCalendarAt)>=0;
}
inline void worker(void*) {
  responseBody=static_cast<char*>(std::malloc(BodyCapacity+1));
  const Config copy=config;const uint32_t now=millis();uint8_t error=0;const uint8_t provider=selectedProvider;
  if(provider==0){error=fetchWeather(copy);nextWeatherAt=now+WeatherPeriodMs;nextProvider=1;}
  else if(provider==1){uint8_t entity=nextEntity;for(uint8_t n=0;n<3&&!validEntity(copy.entities[entity],sizeof(copy.entities[entity]));++n)entity=uint8_t((entity+1)%3);error=fetchEntity(copy,entity);nextHomeAssistantAt=now+HomeAssistantPeriodMs;nextEntity=uint8_t((entity+1)%3);nextProvider=2;}
  else{error=fetchCalendar(copy);nextCalendarAt=now+CalendarPeriodMs;nextProvider=0;}
  (void)error;std::free(responseBody);responseBody=nullptr;requestBusy=false;vTaskDelete(nullptr);
}
inline void begin(){if(!load())config=Config{};}
inline void requestRefresh(){refreshRequested=true;nextWeatherAt=nextHomeAssistantAt=nextCalendarAt=0;}
inline bool busy(){return requestBusy.load();}
inline void service(uint32_t now,bool canRun) {
  if(configuredCalendar()&&time(nullptr)<1700000000&&int32_t(now-nextCalendarAt)>=0){nextCalendarAt=now+5000u;portENTER_CRITICAL(&stateLock);if(!snapshot.calendarValid)snapshot.calendarError=8;portEXIT_CRITICAL(&stateLock);}
  // Preserve the previous request reserve after allocating the formerly static body.
  // Remote fetch ends TLS on the S3, so the C3 only needs room for the body and the reply copy.
  const size_t freeNeeded=(remoteFetch?MinFreeHeapRemote:MinFreeHeapBeforeRequest)+BodyCapacity+1,blockNeeded=(remoteFetch?MinLargestBlockRemote:MinLargestBlockBeforeRequest)+BodyCapacity+1;
  if(!canRun||requestBusy||!WiFi.isConnected()||ESP.getFreeHeap()<freeNeeded||heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<blockNeeded)return;
  if(refreshRequested.exchange(false)){nextWeatherAt=nextHomeAssistantAt=nextCalendarAt=0;}
  for(uint8_t tries=0;tries<3;++tries){uint8_t p=uint8_t((nextProvider+tries)%3);if(!jobAvailable(p,now))continue;
    bool expected=false;if(!requestBusy.compare_exchange_strong(expected,true))return;
    selectedProvider=p;TaskHandle_t handle=nullptr;if(xTaskCreate(worker,"robodesk-ext",6144,nullptr,1,&handle)!=pdPASS){requestBusy=false;if(p==0)nextWeatherAt=now+WeatherPeriodMs;else if(p==1)nextHomeAssistantAt=now+HomeAssistantPeriodMs;else nextCalendarAt=now+CalendarPeriodMs;}return;
  }
}
inline void appendStatus(JsonObject root,uint32_t now) {
  Snapshot s;Config c;portENTER_CRITICAL(&stateLock);s=snapshot;c=config;portEXIT_CRITICAL(&stateLock);
  auto ext=root["external"].to<JsonObject>();ext["enabled"]=configuredWeather()||configuredHomeAssistant()||configuredCalendar();ext["busy"]=busy();
  auto errorName=[](uint8_t error)->const char*{switch(error){case 0:return"ok";case 1:return"client_init";case 2:return"response_too_large";case 3:return"tls_or_network";case 4:return"authorization_rejected";case 5:return"provider_rejected";case 6:return"http_status";case 7:return"invalid_response";case 8:return"waiting_for_clock";default:return"unknown";}};
  auto w=ext["weather"].to<JsonObject>();w["configured"]=configuredWeather();w["valid"]=s.weatherValid;w["summary"]=s.weather;w["ageMs"]=s.weatherAt?uint32_t(now-s.weatherAt):0;w["errorCode"]=s.weatherError;w["error"]=errorName(s.weatherError);
  auto h=ext["homeAssistant"].to<JsonObject>();h["configured"]=configuredHomeAssistant();h["errorCode"]=s.homeAssistantError;h["error"]=errorName(s.homeAssistantError);auto es=h["entities"].to<JsonArray>();for(uint8_t i=0;i<3;++i)if(validEntity(c.entities[i],sizeof(c.entities[i]))){auto e=es.add<JsonObject>();e["id"]=c.entities[i];e["valid"]=s.entities[i].valid;e["state"]=s.entities[i].value;e["ageMs"]=s.entities[i].updatedAt?uint32_t(now-s.entities[i].updatedAt):0;}
  auto cal=ext["calendar"].to<JsonObject>();cal["configured"]=configuredCalendar();cal["valid"]=s.calendarValid;cal["summary"]=s.calendar;cal["ageMs"]=s.calendarAt?uint32_t(now-s.calendarAt):0;cal["errorCode"]=s.calendarError;cal["error"]=errorName(s.calendarError);
}
inline size_t contextText(char*out,size_t cap,uint32_t now,const char*category) {
  if(!out||!cap)return 0;out[0]=0;if(!category)return 0;Snapshot s;Config c;portENTER_CRITICAL(&stateLock);s=snapshot;c=config;portEXIT_CRITICAL(&stateLock);size_t n=0;
  auto add=[&](const char*kind,const char*value,uint32_t age,bool known){if(n>=cap)return;const int w=known?snprintf(out+n,cap-n,"%s cache (%u min old): %s. ",kind,unsigned(age/60000),value):snprintf(out+n,cap-n,"%s cache (age unknown): %s. ",kind,value);if(w>0)n+=size_t(w)<cap-n?size_t(w):cap-n-1;};
  if(!strcmp(category,"weather")){
    if(configuredWeather())add("Weather",s.weatherValid&&s.weatherAt&&uint32_t(now-s.weatherAt)<45u*60u*1000u?s.weather:"stale/unavailable",s.weatherAt?uint32_t(now-s.weatherAt):0,s.weatherAt!=0);
  }else if(!strcmp(category,"home_assistant")){
    if(configuredHomeAssistant())for(uint8_t i=0;i<3;++i)if(validEntity(c.entities[i],sizeof(c.entities[i]))&&n<cap){const bool known=s.entities[i].updatedAt!=0;const int w=known?snprintf(out+n,cap-n,"Home Assistant %s=%s (%u min old). ",c.entities[i],s.entities[i].valid&&uint32_t(now-s.entities[i].updatedAt)<5u*60u*1000u?s.entities[i].value:"stale/unavailable",unsigned((now-s.entities[i].updatedAt)/60000)):snprintf(out+n,cap-n,"Home Assistant %s=stale/unavailable (age unknown). ",c.entities[i]);if(w>0)n+=size_t(w)<cap-n?size_t(w):cap-n-1;}
  }else if(!strcmp(category,"calendar")){
    if(configuredCalendar())add("Calendar",s.calendarValid&&s.calendarAt&&uint32_t(now-s.calendarAt)<2u*60u*60u*1000u?s.calendar:"stale/unavailable",s.calendarAt?uint32_t(now-s.calendarAt):0,s.calendarAt!=0);
  }
  return n;
}

inline bool authorize(WebServer& server,bool mutation,const char* adminPin){
  if(!server.authenticate("admin",adminPin)){server.requestAuthentication();return false;}
  if(mutation&&!roboSameOriginHttp(server.header("Host").c_str(),server.header("Origin").c_str())){server.send(403,"text/plain","Origin rejected");return false;}
  return true;
}
inline const __FlashStringHelper* dashboardHtml(){return F("<!doctype html><html lang='en'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>RoboDesk Integrations</title><style>body{font:16px system-ui;max-width:760px;margin:32px auto;padding:0 16px;background:#101923;color:#edf4fb}section{background:#192633;border:1px solid #334858;border-radius:14px;padding:18px;margin:16px 0}label{display:block;margin:14px 0 6px}input,textarea{box-sizing:border-box;width:100%;padding:10px;border-radius:8px;border:1px solid #536878;background:#0f1922;color:#fff}button{padding:10px 14px;border:0;border-radius:8px;margin:8px 8px 0 0;background:#f4a847;color:#17202a;font-weight:700}small,pre{color:#b7c8d5}a{color:#ffc16d}</style><h1>External integrations</h1><p>Read-only, optional data. Requests run one at a time on the C3; cached readings remain available when a provider is offline. Secrets are stored on the C3 and never shown here or sent to the robot.</p><form id='cfg'><section><h2>Home Assistant</h2><label>HTTPS base URL</label><input name='haBase' placeholder='https://home-assistant.example'><label>Long-lived access token <small id='tokenState'></small></label><input name='haToken' type='password' autocomplete='new-password' placeholder='Leave blank to keep saved token'><label>Allowlisted entity IDs (up to 3, one per line)</label><textarea name='entities' rows='3' placeholder='sensor.living_room_temperature'></textarea></section><section><h2>Weather - Open-Meteo</h2><label><input name='weatherEnabled' type='checkbox' style='width:auto'> Enable current conditions</label><label>Latitude</label><input name='latitude' type='number' min='-90' max='90' step='0.0001'><label>Longitude</label><input name='longitude' type='number' min='-180' max='180' step='0.0001'></section><section><h2>Calendar - read only</h2><label>HTTPS iCalendar feed URL (private feed URLs are secret)</label><input name='calendarUrl' type='password' autocomplete='new-password' placeholder='Leave blank to keep saved feed URL'><label><input name='clearCalendar' type='checkbox' style='width:auto'> Remove saved calendar URL</label><small id='calendarState'></small></section><button type='submit'>Save integrations</button><button type='button' id='refresh'>Refresh now</button><button type='button' id='clear'>Clear all integration settings</button><a href='/'>Back to dashboard</a></form><section><h2>Cached status</h2><pre id='status'>Loading...</pre><p id='message'></p></section><script>const f=document.querySelector('#cfg'),msg=document.querySelector('#message');async function status(){document.querySelector('#status').textContent=JSON.stringify((await(await fetch('/external/status')).json()).external,null,2)}async function load(){const c=await(await fetch('/external/config')).json();f.haBase.value=c.haBase||'';f.entities.value=(c.entities||[]).join('\\n');f.latitude.value=c.latitude??'';f.longitude.value=c.longitude??'';f.weatherEnabled.checked=!!c.weatherEnabled;document.querySelector('#tokenState').textContent=c.tokenConfigured?'(saved)':'';document.querySelector('#calendarState').textContent=c.calendarConfigured?'A feed URL is saved.':'';await status()}f.onsubmit=async e=>{e.preventDefault();const body=new URLSearchParams(new FormData(f));const r=await fetch('/external/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});msg.textContent=await r.text();await load()};document.querySelector('#refresh').onclick=async()=>{const r=await fetch('/external/refresh',{method:'POST'});msg.textContent=await r.text()};document.querySelector('#clear').onclick=async()=>{if(!confirm('Clear all external configuration and cached data?'))return;const r=await fetch('/external/clear',{method:'POST'});msg.textContent=await r.text();f.reset();await load()};load();setInterval(status,15000);</script></html>");}
inline void sendDashboard(WebServer& server){
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);server.send(200,"text/html; charset=utf-8","");
  const char* p=reinterpret_cast<const char*>(dashboardHtml());char chunk[384];size_t used=0;uint8_t ch;
  while((ch=pgm_read_byte(p++))!=0){chunk[used++]=char(ch);if(used==sizeof(chunk)){server.sendContent(chunk,used);used=0;}}
  if(used)server.sendContent(chunk,used);server.sendContent("",0);
}
inline void configureDashboardRoutes(WebServer& server,RuntimeSettings* settings){
  server.on("/integrations",HTTP_GET,[&server,settings](){if(!authorize(server,false,settings->adminPin))return;sendDashboard(server);});
  server.on("/external/config",HTTP_GET,[&server,settings](){if(!authorize(server,false,settings->adminPin))return;JsonDocument d;d["haBase"]=config.homeAssistantBase;d["tokenConfigured"]=config.homeAssistantToken[0]!=0;d["weatherEnabled"]=config.weatherEnabled!=0;d["latitude"]=config.latitude;d["longitude"]=config.longitude;d["calendarConfigured"]=config.calendarUrl[0]!=0;auto a=d["entities"].to<JsonArray>();for(const auto&e:config.entities)if(e[0])a.add(e);String out;serializeJson(d,out);server.send(200,"application/json",out);});
  server.on("/external/status",HTTP_GET,[&server,settings](){if(!authorize(server,false,settings->adminPin))return;JsonDocument d;appendStatus(d.to<JsonObject>(),millis());String out;serializeJson(d,out);server.send(200,"application/json",out);});
  server.on("/external/config",HTTP_POST,[&server,settings](){if(!authorize(server,true,settings->adminPin))return;if(busy()){server.send(409,"text/plain","An external request is in progress; retry shortly");return;}Config next=config;String base=server.arg("haBase");base.trim();if(base.length()>=sizeof(next.homeAssistantBase)||(base.length()&&!httpsUrl(base.c_str(),base.length()+1,false))){server.send(400,"text/plain","Home Assistant URL must be a valid HTTPS base URL");return;}strncpy(next.homeAssistantBase,base.c_str(),sizeof(next.homeAssistantBase)-1);String token=server.arg("haToken");token.trim();if(token.length()>sizeof(next.homeAssistantToken)-1){server.send(400,"text/plain","Token is too long");return;}if(token.length())strncpy(next.homeAssistantToken,token.c_str(),sizeof(next.homeAssistantToken)-1);
    memset(next.entities,0,sizeof(next.entities));String entities=server.arg("entities");unsigned count=0;for(size_t at=0;at<entities.length();){size_t end=entities.indexOf('\n',at);if(end<0)end=entities.length();String e=entities.substring(at,end);e.trim();if(e.length()){if(count>=3||e.length()>=sizeof(next.entities[0])||!validEntity(e.c_str(),e.length()+1)){server.send(400,"text/plain","Use up to 3 entity IDs in domain.object form with letters, digits, and underscores");return;}for(unsigned i=0;i<count;++i)if(!strcmp(next.entities[i],e.c_str())){server.send(400,"text/plain","Duplicate entity ID");return;}strcpy(next.entities[count++],e.c_str());}at=end+1;}
    const String lat=server.arg("latitude"),lon=server.arg("longitude");char*endp=nullptr;float latitude=strtof(lat.c_str(),&endp);if(lat.length()&&(!endp||*endp||!isfinite(latitude)||latitude< -90||latitude>90)){server.send(400,"text/plain","Invalid latitude");return;}float longitude=strtof(lon.c_str(),&endp);if(lon.length()&&(!endp||*endp||!isfinite(longitude)||longitude< -180||longitude>180)){server.send(400,"text/plain","Invalid longitude");return;}next.latitude=latitude;next.longitude=longitude;next.weatherEnabled=server.hasArg("weatherEnabled")&&lat.length()&&lon.length();
    if(server.hasArg("clearCalendar"))next.calendarUrl[0]=0;String calendar=server.arg("calendarUrl");calendar.trim();if(calendar.length()){if(calendar.length()>=sizeof(next.calendarUrl)||!httpsUrl(calendar.c_str(),calendar.length()+1,true)){server.send(400,"text/plain","Calendar feed must be a valid HTTPS URL");return;}strncpy(next.calendarUrl,calendar.c_str(),sizeof(next.calendarUrl)-1);}
    if(!save(next)){server.send(500,"text/plain","Could not persist integration configuration on C3");return;}Snapshot empty{};portENTER_CRITICAL(&stateLock);snapshot=empty;portEXIT_CRITICAL(&stateLock);requestRefresh();server.send(200,"text/plain","Saved on C3. Provider data will refresh in the background.");});
  server.on("/external/refresh",HTTP_POST,[&server,settings](){if(!authorize(server,true,settings->adminPin))return;requestRefresh();server.send(202,"text/plain","Refresh queued; provider requests run one at a time.");});
  server.on("/external/clear",HTTP_POST,[&server,settings](){if(!authorize(server,true,settings->adminPin))return;if(busy()){server.send(409,"text/plain","Wait for the current provider request before clearing");return;}if(!save(Config{})){server.send(500,"text/plain","Could not clear C3 integration settings");return;}portENTER_CRITICAL(&stateLock);snapshot=Snapshot{};portEXIT_CRITICAL(&stateLock);server.send(200,"text/plain","External settings and cached values cleared from C3 NVS/RAM.");});
}

} // namespace roboexternal
