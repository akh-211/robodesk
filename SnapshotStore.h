#pragma once
#include <LittleFS.h>
#include <Preferences.h>
#include "CompanionCore.h"

// No format-on-failure. A completed, checksummed file is the commit record;
// writing the inactive slot never changes the last verified slot.
class SnapshotStore {
  struct Header {uint32_t magic,version,generation,size,crc,headerCrc;};
  bool mounted_=false;int active_=-1;uint32_t generation_=0,floor_=0;
  const char*path(int slot)const{return slot?"/brain-b.bin":"/brain-a.bin";}
  bool header(File&file,Header&h,size_t size){return file&&file.read(reinterpret_cast<uint8_t*>(&h),sizeof(h))==sizeof(h)&&h.magic==0x52424353u&&h.version==1&&h.generation>=floor_&&h.size==size&&file.size()==sizeof(h)+size&&h.headerCrc==companion::checksum(&h,sizeof(h)-sizeof(h.headerCrc));}
  bool read(int slot,void*out,size_t size,uint32_t&generation){File f=LittleFS.open(path(slot),FILE_READ);Header h{};if(!header(f,h,size))return false;size_t n=f.read(static_cast<uint8_t*>(out),size);f.close();if(n!=size||h.crc!=companion::checksum(out,size))return false;generation=h.generation;return true;}
  bool verify(int slot,const void*expected,size_t size,uint32_t generation){File f=LittleFS.open(path(slot),FILE_READ);Header h{};if(!header(f,h,size)||h.generation!=generation)return false;uint8_t chunk[256];const auto*p=static_cast<const uint8_t*>(expected);for(size_t offset=0;offset<size;){size_t n=size-offset;if(n>sizeof(chunk))n=sizeof(chunk);if(f.read(chunk,n)!=n||memcmp(chunk,p+offset,n))return false;offset+=n;}return h.crc==companion::checksum(expected,size);}
 public:
  bool begin(){Preferences p;if(!p.begin("brainmeta",false))return false;floor_=p.getUInt("floor",0);p.end();mounted_=LittleFS.begin(false,"/spiffs",5,"spiffs");return mounted_;}
  bool available()const{return mounted_;}
  uint32_t generation()const{return generation_;}
  bool legacyAllowed()const{return floor_==0&&generation_==0;}
  bool load(void*out,size_t size){if(!mounted_)return false;uint32_t a=0,b=0;bool va=read(0,out,size,a),vb=read(1,out,size,b);if(!va&&!vb)return false;active_=vb&&(!va||b>a)?1:0;generation_=active_?b:a;return read(active_,out,size,generation_);}
  bool save(const void*data,size_t size){if(!mounted_||generation_==UINT32_MAX)return false;int slot=active_==0?1:0;Header h{0x52424353u,1,generation_+1,uint32_t(size),companion::checksum(data,size),0};h.headerCrc=companion::checksum(&h,sizeof(h)-sizeof(h.headerCrc));File f=LittleFS.open(path(slot),FILE_WRITE);if(!f)return false;bool ok=f.write(reinterpret_cast<const uint8_t*>(&h),sizeof(h))==sizeof(h)&&f.write(static_cast<const uint8_t*>(data),size)==size;f.flush();f.close();if(!ok||!verify(slot,data,size,h.generation))return false;active_=slot;generation_=h.generation;return true;}
  // Privacy barrier rejects any old copy, even if deletion is interrupted.
  bool scrubRecovery(const void*sanitized,size_t size){if(!save(sanitized,size))return false;Preferences p;if(!p.begin("brainmeta",false))return false;bool ok=p.putUInt("floor",generation_)==sizeof(uint32_t);p.end();if(!ok)return false;floor_=generation_;int old=active_==0?1:0;if(LittleFS.exists(path(old))&&!LittleFS.remove(path(old)))return false;Preferences legacy;if(!legacy.begin("robobrain",false))return false;ok=legacy.clear();legacy.end();if(!ok)return false;return save(sanitized,size);}
};
