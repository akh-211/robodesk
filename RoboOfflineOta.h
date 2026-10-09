#pragma once
#include "OtaWorkflowPolicy.h"
// S3 OTA is supplied over UART by the C3, never fetched over Wi-Fi.
class GitHubOtaUpdate {
public:
  typedef bool(*ControlCallback)(void*,bool);
  enum State:uint8_t {Idle,Checking,UpToDate,UpdateAvailable,Downloading,Rebooting,Failed};
  using StartResult=RoboOtaStartResult;
  enum class Preparation:uint8_t{Pending,Ready,Rejected};
  using PreparationCallback=Preparation(*)(void*,bool);
  struct Snapshot{State state=Idle;uint32_t version=0;bool active=false;StartResult startResult=StartResult::Accepted;char stage[24]="uart";char failure[32]="none";char message[112]="Updates are coordinated by the C3 gateway.";};
  void begin(ControlCallback,void*,PreparationCallback=nullptr){}void service(){}
  StartResult requestCheck(){return StartResult::Busy;}StartResult requestInstall(){return StartResult::Busy;}
  StartResult requestDiagnostics(uint32_t,long){return StartResult::Busy;}
  bool active()const{return false;}Snapshot snapshot()const{return Snapshot();}
  bool trackedVersion(uint32_t*v){if(!v)return false;Preferences p;if(!p.begin("robodesk_ota",true))return false;*v=p.getUInt("version",ROBODESK_OTA_INITIAL_VERSION);p.end();return true;}
};
