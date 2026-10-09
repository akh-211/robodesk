#pragma once
#include <cstddef>
#include <cstdint>
constexpr int SERIAL_8N1=0;
class HardwareSerial {
public:
  explicit HardwareSerial(int){}
  void setRxBufferSize(size_t){}
  void setTxBufferSize(size_t){}
  void begin(unsigned,int,int,int){}
  size_t write(const uint8_t*,size_t n){return n;}
  int available(){return 0;}
  int read(){return -1;}
};
