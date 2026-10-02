#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#define FILE_READ "r"
#define FILE_WRITE "w"
inline std::map<std::string,std::vector<uint8_t>> mockFiles;
inline int mockWriteBudget=-1;
inline bool mockMountOK=true,mockFormat=false,mockRemoveOK=true;
class File {
  std::vector<uint8_t>*data_=nullptr;size_t position_=0;
 public:
  File()=default;explicit File(std::vector<uint8_t>*data):data_(data){}
  explicit operator bool()const{return data_!=nullptr;}
  size_t size()const{return data_?data_->size():0;}
  size_t read(uint8_t*out,size_t n){if(!data_)return 0;n=std::min(n,data_->size()-position_);memcpy(out,data_->data()+position_,n);position_+=n;return n;}
  size_t write(const uint8_t*p,size_t n){if(!data_)return 0;if(mockWriteBudget>=0)n=std::min(n,size_t(mockWriteBudget));data_->insert(data_->end(),p,p+n);if(mockWriteBudget>=0)mockWriteBudget-=int(n);return n;}
  void flush(){}void close(){}
};
class MockSPIFFS {
 public:
  bool begin(bool format,const char*,int,const char*){mockFormat=format;return mockMountOK;}
  bool exists(const char*p){return mockFiles.count(p)!=0;}
  bool remove(const char*p){if(!mockRemoveOK)return false;mockFiles.erase(p);return true;}
  File open(const char*p,const char*mode){if(!strcmp(mode,FILE_WRITE)){auto&v=mockFiles[p];v.clear();return File(&v);}auto it=mockFiles.find(p);return it==mockFiles.end()?File():File(&it->second);}
};
inline MockSPIFFS SPIFFS;
