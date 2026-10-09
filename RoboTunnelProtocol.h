#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "RoboLinkProtocol.h"

// TcpOpen v2: generation(4) port(2) flags(1) hostLen(1) host(hostLen). A bare
// 4-byte TcpOpen is the legacy Google-pinned request terminated on the C3.
// RawTcp asks the C3 to relay bytes only; TLS then ends on the S3.
namespace robotunnel {
constexpr size_t HostMax=64,OpenFixed=8,OpenMax=OpenFixed+HostMax,AllowMax=6;
enum OpenFlag:uint8_t { RawTcp=1 };
struct OpenRequest {uint32_t generation=0;uint16_t port=0;uint8_t flags=0;bool legacy=true;char host[HostMax+1]{};};
inline bool validHost(const char*h,size_t n){
  if(!h||!n||n>HostMax||h[0]=='.'||h[0]=='-'||h[n-1]=='.'||h[n-1]=='-')return false;
  for(size_t i=0;i<n;++i){const char c=h[i];const bool ok=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.';if(!ok||(c=='.'&&i&&h[i-1]=='.'))return false;}
  return true;
}
inline size_t encodeOpen(uint8_t*out,size_t cap,uint32_t generation,const char*host,uint16_t port,uint8_t flags){
  const size_t n=host?strlen(host):0;if(!out||!validHost(host,n)||!port||cap<OpenFixed+n)return 0;
  robolink::put32(out,generation);robolink::put16(out+4,port);out[6]=flags;out[7]=uint8_t(n);memcpy(out+OpenFixed,host,n);return OpenFixed+n;
}
inline bool decodeOpen(const uint8_t*p,size_t len,OpenRequest&r){
  r=OpenRequest{};
  if(!p||len<4)return false;
  r.generation=robolink::get32(p);
  if(!r.generation)return false;
  if(len==4)return true;
  if(len<OpenFixed+1)return false;
  const size_t n=p[7];
  if(len!=OpenFixed+n||!validHost(reinterpret_cast<const char*>(p+OpenFixed),n))return false;
  r.port=robolink::get16(p+4);r.flags=p[6];if(!r.port||(r.flags&~uint8_t(RawTcp)))return false;
  memcpy(r.host,p+OpenFixed,n);r.host[n]=0;r.legacy=false;return true;
}
// Gemini API keys are URL-safe tokens; the same alphabet the C3 enforces before sending.
inline bool validKey(const char*k,size_t n){
  if(!k||!n||n>96)return false;
  for(size_t i=0;i<n;++i){const char c=k[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;}
  return true;
}
// C3 refuses any raw relay target that is not listed. Entries are copied in.
class Allowlist {
  struct Entry{char host[HostMax+1];uint16_t port;};Entry e_[AllowMax]{};size_t n_=0;
  static bool same(const char*a,const char*b){for(;*a&&*b;++a,++b){char x=*a,y=*b;if(x>='A'&&x<='Z')x=char(x+32);if(y>='A'&&y<='Z')y=char(y+32);if(x!=y)return false;}return !*a&&!*b;}
public:
  void clear(){n_=0;}size_t size()const{return n_;}
  bool add(const char*host,uint16_t port){const size_t l=host?strlen(host):0;if(!validHost(host,l)||!port||n_>=AllowMax)return false;for(size_t i=0;i<n_;++i)if(e_[i].port==port&&same(e_[i].host,host))return true;memcpy(e_[n_].host,host,l+1);e_[n_++].port=port;return true;}
  bool allowed(const char*host,uint16_t port)const{if(!host||!port)return false;for(size_t i=0;i<n_;++i)if(e_[i].port==port&&same(e_[i].host,host))return true;return false;}
};
// Extracts host and port from https://host[:port]/... ; rejects userinfo, IPv6 literals and other schemes.
inline bool parseHttpsUrl(const char*url,char*host,size_t cap,uint16_t&port){
  static const char scheme[]="https://";if(!url||!host||cap<2||strncmp(url,scheme,8))return false;
  const char*s=url+8,*p=s;while(*p&&*p!='/'&&*p!=':'&&*p!='?'&&*p!='#')++p;const size_t n=size_t(p-s);if(!validHost(s,n)||n>=cap)return false;
  port=443;if(*p==':'){++p;uint32_t v=0;size_t d=0;while(*p>='0'&&*p<='9'){v=v*10+uint32_t(*p-'0');if(v>65535)return false;++p;++d;}if(!d||!v)return false;port=uint16_t(v);}
  if(*p&&*p!='/'&&*p!='?'&&*p!='#')return false;
  memcpy(host,s,n);host[n]=0;return true;
}
}
