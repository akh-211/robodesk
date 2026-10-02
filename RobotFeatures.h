#pragma once

#include <stdint.h>

// Small, non-blocking state machine for local focus sessions and scheduled
// comfort/briefing events. Call service() from the main loop.
class RobotFeatures {
 public:
  enum Phase : uint8_t { Idle=0, Focus=1, Break=2, LongBreak=3, Paused=4 };
  struct Settings {
    bool comfortEnabled=false;
    float minTemp=18.f, maxTemp=30.f;
    float minHumidity=30.f, maxHumidity=70.f;
    bool briefingEnabled=false;
    uint16_t briefingMinute=480;
    uint16_t focusMinutes=25, breakMinutes=5, longBreakMinutes=15;
  };
  typedef void (*EventCallback)(void*, const char*);

  void begin(uint32_t now, bool wasPersistedActive, EventCallback cb, void* context) {
    callback_=cb; context_=context; phase_=Idle; round_=0; deadline_=remaining_=0;
    interrupted_=wasPersistedActive; lastComfortAt_=now-900000u;
  }
  bool startPomodoro(uint32_t now, uint16_t focusMinutes=25) {
    if (!focusMinutes || focusMinutes>90 || phase_!=Idle) return false;
    interrupted_=false; round_=0; phase_=Focus; deadline_=now+uint32_t(focusMinutes)*60000u; return true;
  }
  bool pausePomodoro(uint32_t now) {
    if (phase_!=Focus&&phase_!=Break&&phase_!=LongBreak) return false;
    remaining_=int32_t(deadline_-now)>0?deadline_-now:0; pausedFrom_=phase_;phase_=Paused; return true;
  }
  bool resumePomodoro(uint32_t now) {
    if (phase_!=Paused) return false;
    deadline_=now+remaining_; remaining_=0; phase_=pausedFrom_; return true;
  }
  bool stopPomodoro() { if(phase_==Idle)return false;phase_=Idle;round_=0;deadline_=remaining_=0;interrupted_=false;return true; }
  bool active() const { return phase_!=Idle; }
  bool interrupted() const { return interrupted_; }
  Phase phase() const { return phase_; }
  uint8_t round() const { return round_; }
  uint32_t remainingMs(uint32_t now) const {
    if(phase_==Paused)return remaining_;
    return (phase_==Idle||int32_t(deadline_-now)<=0)?0:deadline_-now;
  }

  void service(uint32_t now, const Settings& s, bool sensorsFresh, float temperature,
               float humidity, bool clockValid, uint16_t minute, uint32_t localDay,
               const char* briefingText=nullptr) {
    if((phase_==Focus||phase_==Break||phase_==LongBreak)&&int32_t(now-deadline_)>=0){
      if(phase_==Focus){++round_; if(round_%4u==0){phase_=LongBreak;deadline_=now+uint32_t(s.longBreakMinutes)*60000u;emit_("Focus complete. Long break started");}
        else{phase_=Break;deadline_=now+uint32_t(s.breakMinutes)*60000u;emit_("Focus complete. Break started");}}
      else{phase_=Focus;deadline_=now+uint32_t(s.focusMinutes)*60000u;emit_("Break complete. Focus started");}
    }
    if(s.comfortEnabled&&sensorsFresh&&uint32_t(now-lastComfortAt_)>=900000u){
      const char* msg=nullptr;
      if(temperature<s.minTemp)msg="Room temperature is below your comfort range";
      else if(temperature>s.maxTemp)msg="Room temperature is above your comfort range";
      else if(humidity<s.minHumidity)msg="Room humidity is below your comfort range";
      else if(humidity>s.maxHumidity)msg="Room humidity is above your comfort range";
      if(msg){emit_(msg);lastComfortAt_=now;}
    }
    if(s.briefingEnabled&&clockValid&&minute>=s.briefingMinute&&localDay!=briefingDay_){
      emit_(briefingText&&*briefingText?briefingText:"Daily briefing: check your reminders and room conditions");briefingDay_=localDay;
    }
  }

 private:
  Phase phase_=Idle,pausedFrom_=Idle; uint8_t round_=0; uint32_t deadline_=0,remaining_=0,lastComfortAt_=0,briefingDay_=0xffffffffu;
  bool interrupted_=false; EventCallback callback_=nullptr;void* context_=nullptr;
  void emit_(const char* text){if(callback_)callback_(context_,text);}
};
