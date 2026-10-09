#include "RoboTunnelProtocol.h"
#include <cassert>
#include <cstdio>
using namespace robotunnel;
int main(){
  uint8_t b[OpenMax+4];OpenRequest r;
  // Legacy 4-byte open stays valid; zero generation never does.
  robolink::put32(b,7);assert(decodeOpen(b,4,r)&&r.legacy&&r.generation==7&&!r.port);
  robolink::put32(b,0);assert(!decodeOpen(b,4,r));assert(!decodeOpen(b,3,r));assert(!decodeOpen(nullptr,4,r));
  // Round trip across host lengths.
  for(size_t n=1;n<=HostMax;++n){char h[HostMax+1];for(size_t i=0;i<n;++i)h[i]=(i==0||i==n-1)?'a':((i%7==3)?'-':'b');h[n]=0;
    const size_t w=encodeOpen(b,sizeof(b),0x01020304,h,443,RawTcp);assert(w==OpenFixed+n);assert(decodeOpen(b,w,r)&&!r.legacy&&r.port==443&&r.flags==RawTcp&&r.generation==0x01020304&&!strcmp(r.host,h));}
  // Encoder refusals: bad host, port 0, small buffer, overlong host.
  assert(!encodeOpen(b,sizeof(b),1,"bad host",443,0));assert(!encodeOpen(b,sizeof(b),1,"a.com",0,0));assert(!encodeOpen(b,8,1,"a.com",443,0));
  char longHost[HostMax+2];memset(longHost,'a',sizeof(longHost));longHost[HostMax+1]=0;assert(!encodeOpen(b,sizeof(b),1,longHost,443,0));
  // Decoder refusals: length mismatch, hostLen 0, unknown flag, bad characters, port 0.
  size_t w=encodeOpen(b,sizeof(b),1,"api.open-meteo.com",443,RawTcp);assert(w);
  assert(!decodeOpen(b,w-1,r));assert(!decodeOpen(b,w+1,r));
  uint8_t c[OpenMax];memcpy(c,b,w);c[6]=2;assert(!decodeOpen(c,w,r));
  memcpy(c,b,w);c[7]=0;assert(!decodeOpen(c,OpenFixed,r));
  memcpy(c,b,w);c[OpenFixed+3]='/';assert(!decodeOpen(c,w,r));
  memcpy(c,b,w);robolink::put16(c+4,0);assert(!decodeOpen(c,w,r));
  memcpy(c,b,w);c[OpenFixed+3]=0;assert(!decodeOpen(c,w,r));
  assert(!validHost("a..b",4)&&!validHost(".a",2)&&!validHost("a-",2)&&!validHost("a b",3)&&validHost("192.168.1.5",11)&&validHost("A.b-c.d",7));
  // Allowlist: exact host+port, case-insensitive, capped, no wildcard or suffix match.
  Allowlist a;assert(a.add("generativelanguage.googleapis.com",443)&&a.add("api.open-meteo.com",443)&&a.add("API.open-meteo.com",443)&&a.size()==2);
  assert(a.allowed("GenerativeLanguage.googleapis.com",443)&&!a.allowed("generativelanguage.googleapis.com",80)&&!a.allowed("evil.generativelanguage.googleapis.com",443)&&!a.allowed("googleapis.com",443)&&!a.allowed("api.open-meteo.com.evil.io",443)&&!a.allowed(nullptr,443)&&!a.allowed("api.open-meteo.com",0));
  for(unsigned i=a.size();i<AllowMax;++i){char h[16];snprintf(h,sizeof(h),"h%u.example",i);assert(a.add(h,8443));}assert(!a.add("extra.example",443)&&a.size()==AllowMax);a.clear();assert(!a.allowed("api.open-meteo.com",443));
  // URL parsing for configured provider hosts.
  char h[HostMax+1];uint16_t p=0;
  assert(parseHttpsUrl("https://ha.local:8123/api/states/x",h,sizeof(h),p)&&!strcmp(h,"ha.local")&&p==8123);
  assert(parseHttpsUrl("https://calendar.example.com/a.ics?token=1",h,sizeof(h),p)&&!strcmp(h,"calendar.example.com")&&p==443);
  assert(parseHttpsUrl("https://192.168.1.20",h,sizeof(h),p)&&!strcmp(h,"192.168.1.20")&&p==443);
  assert(!parseHttpsUrl("http://a.com/",h,sizeof(h),p)&&!parseHttpsUrl("https://user@a.com/",h,sizeof(h),p)&&!parseHttpsUrl("https://[::1]/",h,sizeof(h),p)&&!parseHttpsUrl("https://a.com:0/",h,sizeof(h),p)&&!parseHttpsUrl("https://a.com:70000/",h,sizeof(h),p)&&!parseHttpsUrl("https://a.com:/",h,sizeof(h),p)&&!parseHttpsUrl("https:///x",h,sizeof(h),p)&&!parseHttpsUrl(nullptr,h,sizeof(h),p));
  char tiny[4];assert(!parseHttpsUrl("https://abcd.com/",tiny,sizeof(tiny),p));
  std::puts("PASS: tunnel open v1/v2 codec, host validation, allowlist, URL parsing");
}
