#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Short-lived notification queue. Payloads are never written to NVS/LittleFS.
class PhoneNotificationBridge {
 public:
  static constexpr unsigned Capacity=6;
  struct Item { uint32_t id=0, receivedAt=0; char appId[64]={},app[40]={},title[80]={},snippet[181]={}; };
  bool accept(const char* appId,const char* app,const char* title,const char* body,
              const char* allowlist,uint32_t now) {
    if(!allowed_(appId,allowlist)||!app||!*app)return false;
    for(const auto& old:items_)if(old.id&&uint32_t(now-old.receivedAt)<5000u&&!strcmp(old.appId,appId)&&!strcmp(old.title,title?title:"")&&!strcmp(old.snippet,body?body:""))return false;
    Item item;item.id=++nextId_;item.receivedAt=now;
    copy_(item.appId,sizeof(item.appId),appId);copy_(item.app,sizeof(item.app),app);
    copy_(item.title,sizeof(item.title),title);copy_(item.snippet,sizeof(item.snippet),body);
    items_[nextId_%Capacity]=item;return true;
  }
  const Item& at(unsigned i)const{return items_[i%Capacity];}
  unsigned count()const{unsigned n=0;for(const auto&item:items_)n+=item.id!=0;return n;}
  void expire(uint32_t now,uint32_t maxAge=300000u){for(auto&item:items_)if(item.id&&uint32_t(now-item.receivedAt)>maxAge)item=Item();}
  void clear(){for(auto&item:items_)item=Item();}
  static bool allowed_(const char* appId,const char* list){
    if(!appId||!*appId||!list||!*list)return false;
    const char* p=list;while(*p){while(*p=='\n'||*p=='\r'||*p==';'||*p==' '||*p=='\t')++p;const char* start=p;while(*p&&*p!='\n'&&*p!='\r'&&*p!=';'&&*p!=' '&&*p!='\t')++p;const size_t n=size_t(p-start);if(n&&strlen(appId)==n&&!strncmp(start,appId,n))return true;}
    return false;
  }
 private:
  Item items_[Capacity]{};uint32_t nextId_=0;
  static void copy_(char*out,size_t cap,const char*in){if(!cap)return;if(!in)in="";size_t n=strlen(in);if(n>=cap)n=cap-1;memcpy(out,in,n);out[n]=0;}
};
