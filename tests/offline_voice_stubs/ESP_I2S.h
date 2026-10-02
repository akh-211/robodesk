#pragma once
#include <cstdint>
enum { I2S_DATA_BIT_WIDTH_32BIT=32, I2S_SLOT_MODE_STEREO=2, I2S_RX_TRANSFORM_32_TO_16=1, I2S_RX_TRANSFORM_NONE=0, I2S_STD_SLOT_LEFT=0 };
class I2SClass {
 public:
  bool configureRX(uint32_t rate,int width,int mode,int transform,int slot){lastRate=rate;lastWidth=width;lastMode=mode;lastTransform=transform;lastSlot=slot;++configureCalls;return configureOK;}
  uint32_t lastRate=0;int lastWidth=0,lastMode=0,lastTransform=-1,lastSlot=-1;unsigned configureCalls=0;bool configureOK=true;
};
