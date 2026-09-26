#pragma once
#include <Arduino.h>

// Keep the frame type visible to Arduino's generated function prototypes.
constexpr size_t MIC_FRAME_SAMPLES = 320; // 20 ms at 16 kHz
struct MicCapturedFrame {
  int16_t pcm[MIC_FRAME_SAMPLES];
  float rms;
  uint32_t capturedAt;
  uint8_t clipped;
};

// Assemble only contiguous I2S bytes. Call reset after capture ownership changes.
struct MicRawFrameAssembler {
  size_t bytes=0;
  uint32_t startedAt=0;
  void reset(){bytes=0;startedAt=0;}
  template<class Reader,class Clock>
  bool read(uint8_t* out,size_t capacity,Reader reader,Clock clock){
    const uint32_t now=clock();
    if(bytes&&uint32_t(now-startedAt)>40u)reset();
    if(!bytes)startedAt=now;
    const size_t n=reader(out+bytes,capacity-bytes);
    if(n>capacity-bytes){reset();return false;}
    bytes+=n;
    if(uint32_t(clock()-startedAt)>40u){reset();return false;}
    if(bytes<capacity)return false;
    reset();return true;
  }
};
