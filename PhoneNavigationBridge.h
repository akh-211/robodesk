#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// One transient instruction, never a route database or a persistent location.
class PhoneNavigationBridge {
 public:
  static constexpr size_t WireMax=180;
  static constexpr uint32_t StaleMs=30000, ExpireMs=120000;
  struct Instruction {
    char state[15]{},turn[16]{},road[49]{},eta[25]{};
    uint32_t distanceMeters=0,receivedAt=0,revision=0;bool distanceKnown=false;
  };
  bool accept(const char* wire,uint32_t now,uint32_t ageMs=0) {
    if(!wire||strlen(wire)>WireMax||ageMs>=ExpireMs)return false;
    char buf[WireMax+1];strcpy(buf,wire);char* f[6]{buf};unsigned count=1;
    for(char*p=buf;*p;++p)if(*p=='\n'){if(count==6)return false;*p=0;f[count++]=p+1;}
    if(count!=6||strcmp(f[0],"NAV1"))return false;
    if(!oneOf(f[1],"active|rerouting|arrived|ended")||!oneOf(f[2],"left|right|straight|uturn|slight_left|slight_right|roundabout|unknown"))return false;
    if(!*f[3]||strlen(f[3])>7)return false;
    const bool distanceKnown=strcmp(f[3],"?")!=0;
    if(distanceKnown)for(const char*p=f[3];*p;++p)if(*p<'0'||*p>'9')return false;
    const unsigned long distance=strtoul(f[3],nullptr,10);if(distance>1000000)return false;
    if(strlen(f[4])>=sizeof(current_.road)||strlen(f[5])>=sizeof(current_.eta))return false;
    for(unsigned i=4;i<6;++i)for(const unsigned char*p=(unsigned char*)f[i];*p;++p)if(*p<32||*p==127)return false;
    Instruction next;strcpy(next.state,f[1]);strcpy(next.turn,f[2]);strcpy(next.road,f[4]);strcpy(next.eta,f[5]);
    next.distanceMeters=uint32_t(distance);next.distanceKnown=distanceKnown;next.receivedAt=now-ageMs;next.revision=++sequence_;if(!next.revision)next.revision=++sequence_;
    current_=next;return true;
  }
  bool visible(uint32_t now)const{return current_.revision&&strcmp(current_.state,"ended")&&age(now)<(!strcmp(current_.state,"arrived")?10000u:ExpireMs);}
  bool stale(uint32_t now)const{return visible(now)&&age(now)>=StaleMs;}
  uint32_t age(uint32_t now)const{return uint32_t(now-current_.receivedAt);}
  const Instruction& current()const{return current_;}
  void clear(){current_=Instruction{};}
  void expire(uint32_t now){if(current_.revision&&age(now)>=(!strcmp(current_.state,"arrived")||!strcmp(current_.state,"ended")?10000u:ExpireMs))clear();}
 private:
  Instruction current_{};uint32_t sequence_=0;
  static bool oneOf(const char*s,const char*list){while(*list){const char*e=strchr(list,'|');size_t n=e?size_t(e-list):strlen(list);if(strlen(s)==n&&!strncmp(s,list,n))return true;if(!e)break;list=e+1;}return false;}
};
