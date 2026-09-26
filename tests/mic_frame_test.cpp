#include "MicCapturedFrame.h"
#include <cassert>
#include <iostream>
int main(){
  MicRawFrameAssembler a;uint8_t out[8]={};uint32_t now=100;
  auto clock=[&](){return now;};
  auto first=[](uint8_t* p,size_t n){assert(n==8);std::memset(p,1,3);return size_t(3);};
  auto rest=[](uint8_t* p,size_t n){assert(n==5);std::memset(p,2,n);return n;};
  assert(!a.read(out,8,first,clock));now+=10;
  assert(a.read(out,8,rest,clock));assert(out[0]==1&&out[2]==1&&out[3]==2&&out[7]==2);
  assert(a.bytes==0);
  auto full=[](uint8_t* p,size_t n){assert(n==8);std::memset(p,3,n);return n;};
  assert(!a.read(out,8,first,clock));a.reset(); // pause/resume handoff
  assert(a.read(out,8,full,clock));for(auto b:out)assert(b==3);
  assert(!a.read(out,8,first,clock));now+=121; // old partial must expire
  assert(a.read(out,8,full,clock));for(auto b:out)assert(b==3);
  auto slow=[&](uint8_t* p,size_t n){std::memset(p,4,n);now+=41;return n;};
  assert(!a.read(out,8,slow,clock));assert(a.bytes==0);
  now=UINT32_MAX-10;assert(!a.read(out,8,first,clock));now+=20;
  assert(a.read(out,8,rest,clock)); // millis wrap remains safe
  std::cout<<"PASS: 5 partial microphone frame regression scenarios\n";
}
