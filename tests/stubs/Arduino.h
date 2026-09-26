#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
inline uint32_t mockNow=100, mockConnectCalls=0, mockWriteCalls=0;
inline bool mockConnectOK=true, mockWriteStalled=false, mockTaskOK=true;
inline size_t mockWriteLimit=0;
inline std::vector<uint8_t> mockTx,mockRx;
inline void (*mockTask)(void*)=nullptr;
inline void* mockTaskArg=nullptr;
inline uint32_t millis(){return mockNow;}
inline void delay(uint32_t n){mockNow+=n;}
inline uint32_t esp_random(){return 0x12345678;}
struct MockSerial { template<class... T> void printf(const char*,T...){} void println(const char*){} };
inline MockSerial Serial;
class String {
  std::string value;
public:
  String& operator=(const char* s){value=s;return *this;}
  size_t length()const{return value.size();}
  const char* c_str()const{return value.c_str();}
};
