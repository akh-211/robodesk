#include "PhoneNavigationBridge.h"
#include "PhoneNotificationBridge.h"
#include "PhoneAncsProtocol.h"
#include <cassert>
#include <iostream>
#include <vector>
int main(){
  PhoneNavigationBridge nav;assert(nav.accept("NAV1\nactive\nleft\n250\nJalan A\n5 min",100));assert(nav.visible(101)&&!nav.stale(101));
  assert(nav.stale(30100)&&!nav.visible(120100));auto revision=nav.current().revision;
  assert(!nav.accept("NAV1\nactive\ndanger\n10\nA\n",200));assert(!nav.accept("NAV1\nactive\nleft\n-1\nA\n",200));assert(!nav.accept("NAV1\nactive\nleft\n10\nA\n\nextra",200));assert(nav.current().revision==revision);
  assert(!nav.accept("NAV1\nactive\nleft\n10\nA\n",200,120000));assert(nav.accept("NAV1\narrived\nunknown\n0\nHome\n",0xfffffff0));assert(nav.visible(0x10));assert(!nav.visible(10000));
  assert(nav.accept("NAV1\nended\nunknown\n0\n\n",100));assert(!nav.visible(101));
  nav.clear();assert(nav.accept("NAV1\nactive\nunknown\n?\nRoad\n",200,40));assert(!nav.current().distanceKnown&&nav.age(200)==40&&nav.current().revision>revision);
  nav.expire(120160);assert(!nav.current().revision);
  PhoneNotificationBridge q;assert(!q.accept("bad.app","Bad","x","y","good.app",0));assert(q.accept("good.app","Good","x","y","good.app",1,"a"));auto id=q.at(1).id;
  assert(q.accept("good.app","Good","x","y","good.app",2,"a")&&q.count()==1&&q.at(1).id==id);
  assert(q.at(1).receivedAt==1); // Deduplication must not silently extend the S3 lifetime.
  assert(q.accept("good.app","Good","updated","new","good.app",3,"a")&&q.count()==1&&q.at(1).id!=id);
  assert(q.remove("a","good.app")&&q.count()==0);assert(q.accept("good.app","Good","x","y","good.app",4,"a"));q.filter("other.app");assert(q.count()==0);
  q.accept("good.app","Good","x","y","good.app",10);q.expire(300011);assert(q.count()==0);
  PhoneNotificationBridge remainingTtl;assert(remainingTtl.accept("good.app","Good","near expiry","body","good.app",500000,"ttl",299000));
  bool foundRemainingTtl=false;for(unsigned i=0;i<PhoneNotificationBridge::Capacity;++i){const auto&item=remainingTtl.at(i);if(item.id&&!strcmp(item.key,"ttl")){assert(item.receivedAt==201000u);foundRemainingTtl=true;}}
  assert(foundRemainingTtl);
  remainingTtl.expire(501000);assert(remainingTtl.count()==1);remainingTtl.expire(501001);assert(remainingTtl.count()==0);
  PhoneNotificationBridge full;
  for(unsigned i=0;i<6;++i){char key[8],title[8];snprintf(key,sizeof(key),"k%u",i);snprintf(title,sizeof(title),"t%u",i);assert(full.accept("good.app","Good",title,"body","good.app",i+1,key));assert(full.count()==i+1);}
  assert(full.accept("good.app","Good","seventh","body","good.app",20,"k6")&&full.count()==6);
  assert(full.remove("k0","good.app")&&full.count()==6); // Idempotent purge accepts a key already evicted from the six-entry cache.
  assert(!full.remove("k6","bad.app")&&full.count()==6);
  assert(full.remove("k6","good.app")&&full.count()==5);
  assert(!full.remove("", "good.app"));
  PhoneNotificationBridge purgeApp;assert(purgeApp.accept("good.app","Good","private","body","good.app",1,"p1"));assert(purgeApp.accept("other.app","Other","keep","body","good.app;other.app",2,"p2"));assert(purgeApp.removeApp("good.app")&&purgeApp.count()==1);bool keptOther=false;for(unsigned i=0;i<PhoneNotificationBridge::Capacity;++i){const auto&item=purgeApp.at(i);if(item.id){assert(!strcmp(item.appId,"other.app"));keptOther=true;}}assert(keptOther);assert(purgeApp.removeApp("good.app")&&purgeApp.count()==1);assert(!purgeApp.removeApp(""));
  PhoneAncsProtocol p;const uint32_t uid=0x12345678;
  p.begin(PhoneAncsProtocol::Kind::Identifier,uid);std::vector<uint8_t> identifier={0,0x78,0x56,0x34,0x12,0,8,0,'g','o','o','d','.','a','p','p'};
  for(uint8_t b:identifier){assert(p.feed(&b,1));}assert(p.complete()&&!strcmp(p.appId,"good.app"));
  p.begin(PhoneAncsProtocol::Kind::Content,uid);uint8_t content[]={0,0x78,0x56,0x34,0x12,1,2,0,'H','i',3,3,0,'A','\n','B'};
  assert(p.feed(content,7)&&!p.complete());assert(p.feed(content+7,sizeof(content)-7)&&p.complete());assert(!strcmp(p.title,"Hi")&&!strcmp(p.body,"A B"));
  p.begin(PhoneAncsProtocol::Kind::AppName,uid,"good.app");uint8_t name[]={1,'g','o','o','d','.','a','p','p',0,0,4,0,'G','o','o','d'};assert(p.feed(name,sizeof(name))&&p.complete()&&!strcmp(p.appName,"Good"));
  p.begin(PhoneAncsProtocol::Kind::Content,uid+1);assert(!p.feed(content,sizeof(content))&&p.failed());
  p.begin(PhoneAncsProtocol::Kind::Identifier,uid);uint8_t huge[]={0,0x78,0x56,0x34,0x12,0,255,255};assert(!p.feed(huge,sizeof(huge)));
  p.begin(PhoneAncsProtocol::Kind::Identifier,uid);identifier.push_back(0);assert(!p.feed(identifier.data(),identifier.size()));
  std::cout<<"PASS: navigation lifetime and rejection, notification coalescing, fragmented ANCS identity/content bounds\n";
}
