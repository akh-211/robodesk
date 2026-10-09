#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Short-lived notification queue. Payloads are never written to NVS/LittleFS.
class PhoneNotificationBridge {
 public:
  static constexpr unsigned Capacity=6;
  struct Item { uint32_t id=0, receivedAt=0; char key[33]={},appId[64]={},app[40]={},title[80]={},snippet[181]={}; };
  bool accept(const char* appId,const char* app,const char* title,const char* body,
              const char* allowlist,uint32_t now,const char*key="",uint32_t ageMs=0) {
    if(!allowed_(appId,allowlist)||!app||!*app)return false;
    if(!key||strlen(key)>32||strlen(appId)>=64||ageMs>=300000)return false;
    Item item;item.receivedAt=now-ageMs;
    copy_(item.key,sizeof(item.key),key);
    copy_(item.appId,sizeof(item.appId),appId);copy_(item.app,sizeof(item.app),app);
    copy_(item.title,sizeof(item.title),title);copy_(item.snippet,sizeof(item.snippet),body);
    for(auto&old:items_)if(old.id&&*key&&!strcmp(old.key,key)&&!strcmp(old.appId,appId)){
      if(!strcmp(old.app,item.app)&&!strcmp(old.title,item.title)&&!strcmp(old.snippet,item.snippet)){return true;}
      item.id=nextId();old=item;return true;
    }
    for(const auto& old:items_)if(old.id&&uint32_t(now-old.receivedAt)<5000u&&!strcmp(old.appId,appId)&&!strcmp(old.title,title?title:"")&&!strcmp(old.snippet,body?body:""))return false;
    item.id=nextId();
    items_[nextId_%Capacity]=item;return true;
  }
  bool remove(const char*key,const char*appId=nullptr){if(!key||!*key||strlen(key)>32||(appId&&(!*appId||strlen(appId)>=64)))return false;for(auto&item:items_)if(item.id&&!strcmp(item.key,key)){if(appId&&strcmp(item.appId,appId))return false;item=Item{};}return true;}
  bool removeApp(const char*appId){if(!appId||!*appId||strlen(appId)>=64)return false;for(auto&item:items_)if(item.id&&!strcmp(item.appId,appId))item=Item{};return true;}
  void filter(const char*list){for(auto&item:items_)if(item.id&&!allowed_(item.appId,list))item=Item{};}
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
  uint32_t nextId(){if(!++nextId_)++nextId_;return nextId_;}
  static void copy_(char*out,size_t cap,const char*in){if(!cap)return;if(!in)in="";size_t n=strlen(in);if(n>=cap){n=cap-1;while(n&&(uint8_t(in[n])&0xc0)==0x80)--n;}for(size_t i=0;i<n;++i)out[i]=(uint8_t(in[i])<32||uint8_t(in[i])==127)?' ':in[i];out[n]=0;}
};
