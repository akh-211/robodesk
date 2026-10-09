#pragma once

#include <Arduino.h>

// Measures a protected 1S cell through an external 100k:100k divider.
// The reported value is advisory and is never used to switch off the robot.
class RoboBatteryMonitor {
 public:
  static constexpr uint16_t LowMillivolts=3500;
  static constexpr uint16_t CriticalMillivolts=3300;

  bool begin(int pin) {
    pin_=pin;
    if(pin_<0)return false;
    pinMode(pin_,INPUT);
    analogSetPinAttenuation(uint8_t(pin_),ADC_11db);
    return true;
  }

  void service(uint32_t now) {
    if(pin_<0||uint32_t(now-sampledAt_)<1000u)return;
    sampledAt_=now;
    uint32_t sum=0;
    for(unsigned i=0;i<8;++i)sum+=analogReadMilliVolts(uint8_t(pin_));
    const uint32_t cellMv=(sum*2u+4u)/8u;
    if(cellMv<2500u||cellMv>4500u){valid_=false;return;}
    millivolts_=uint16_t((uint32_t(millivolts_)*3u+cellMv+2u)/4u);
    if(!valid_)millivolts_=uint16_t(cellMv);
    valid_=true;
  }

  bool valid()const{return valid_;}
  bool low()const{return valid_&&millivolts_<=LowMillivolts;}
  bool critical()const{return valid_&&millivolts_<=CriticalMillivolts;}
  uint16_t millivolts()const{return valid_?millivolts_:0;}
  uint32_t ageMs(uint32_t now)const{return sampledAt_?uint32_t(now-sampledAt_):0;}

 private:
  int pin_=-1;
  uint16_t millivolts_=0;
  uint32_t sampledAt_=0;
  bool valid_=false;
};
