#pragma once
#include <cstddef>
#include <cstring>
#include "ESP_I2S.h"
struct sr_cmd_t { int command_id; char str[256]; };
enum sr_event_t { SR_EVENT_WAKEWORD, SR_EVENT_WAKEWORD_CHANNEL, SR_EVENT_COMMAND, SR_EVENT_TIMEOUT, SR_EVENT_MAX };
enum sr_mode_t { SR_MODE_OFF, SR_MODE_WAKEWORD, SR_MODE_COMMAND, SR_MODE_MAX };
enum sr_channels_t { SR_CHANNELS_MONO, SR_CHANNELS_STEREO, SR_CHANNELS_TRIPLE, SR_CHANNELS_QUAD, SR_CHANNELS_MAX };
using sr_cb=void(*)(sr_event_t,int,int);
class ESP_SR_Class {
 public:
  void onEvent(sr_cb callback){cb=callback;}
  bool begin(I2SClass&,const sr_cmd_t* commands,size_t count,sr_channels_t,sr_mode_t mode,const char*){lastMode=mode;commandCount=count;firstCommand=commands;beginCalls++;return beginOK;}
  bool end(){endCalls++;return endOK;}
  bool setMode(sr_mode_t mode){lastMode=mode;setModeCalls++;return true;}
  void emit(sr_event_t event,int commandId=0,int phraseId=0){if(cb)cb(event,commandId,phraseId);}
  sr_cb cb=nullptr;sr_mode_t lastMode=SR_MODE_OFF;size_t commandCount=0;const sr_cmd_t* firstCommand=nullptr;
  unsigned beginCalls=0,endCalls=0,setModeCalls=0;bool beginOK=true,endOK=true;
};
inline ESP_SR_Class ESP_SR;
