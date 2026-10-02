#pragma once
#include <map>
#include <string>
#include <Arduino.h>

class Preferences {
  inline static std::map<std::string, std::string> values_;
  std::string prefix_;
  std::string scoped(const char*key)const{return prefix_+key;}
public:
  bool begin(const char*ns, bool = false) { prefix_=std::string(ns)+"/";return true; }
  void end() {}
  bool getBool(const char* key, bool fallback) const {
    auto it=values_.find(scoped(key));return it==values_.end()?fallback:it->second=="1";
  }
  String getString(const char* key, const char* fallback = "") const {
    String value;auto it=values_.find(scoped(key));value=it==values_.end()?fallback:it->second.c_str();return value;
  }
  uint16_t getUShort(const char* key, uint16_t fallback) const {
    auto it=values_.find(scoped(key));return it==values_.end()?fallback:uint16_t(std::stoul(it->second));
  }
  int16_t getShort(const char* key, int16_t fallback=0) const {
    auto it=values_.find(scoped(key));return it==values_.end()?fallback:int16_t(std::stoi(it->second));
  }
  uint8_t getUChar(const char* key, uint8_t fallback) const {
    auto it=values_.find(scoped(key));return it==values_.end()?fallback:uint8_t(std::stoul(it->second));
  }
  size_t putBool(const char* key, bool value) { values_[scoped(key)]=value?"1":"0";return 1; }
  size_t putString(const char* key, const char* value) { values_[scoped(key)]=value;return values_[scoped(key)].size()+1; }
  size_t putUShort(const char* key, uint16_t value) { values_[scoped(key)]=std::to_string(value);return sizeof(value); }
  size_t putShort(const char* key, int16_t value) { values_[scoped(key)]=std::to_string(value);return sizeof(value); }
  size_t putUChar(const char* key, uint8_t value) { values_[scoped(key)]=std::to_string(value);return sizeof(value); }
  uint32_t getUInt(const char*key,uint32_t fallback=0)const{auto it=values_.find(scoped(key));return it==values_.end()?fallback:uint32_t(std::stoul(it->second));}
  size_t putUInt(const char*key,uint32_t value){values_[scoped(key)]=std::to_string(value);return sizeof(value);}
  size_t getBytesLength(const char*key)const{auto it=values_.find(scoped(key));return it==values_.end()?0:it->second.size();}
  size_t getBytes(const char*key,void*out,size_t cap)const{auto it=values_.find(scoped(key));if(it==values_.end())return 0;size_t n=std::min(cap,it->second.size());memcpy(out,it->second.data(),n);return n;}
  size_t putBytes(const char*key,const void*data,size_t n){values_[scoped(key)]=std::string(static_cast<const char*>(data),n);return n;}
  bool remove(const char*key){return values_.erase(scoped(key))!=0;}
  float getFloat(const char*key,float fallback=0)const{auto it=values_.find(scoped(key));return it==values_.end()?fallback:std::stof(it->second);}
  uint32_t getULong(const char*key,uint32_t fallback=0)const{return getUInt(key,fallback);}
  bool clear(){for(auto it=values_.begin();it!=values_.end();)if(it->first.rfind(prefix_,0)==0)it=values_.erase(it);else ++it;return true;}
};
