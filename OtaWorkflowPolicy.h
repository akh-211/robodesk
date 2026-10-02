#pragma once
#include <stdint.h>

enum class RoboOtaStartResult : uint8_t { Accepted, Busy, NoMemory, NoCandidate, RollbackDisabled };

inline RoboOtaStartResult roboOtaStartDecision(bool rollback, bool active, bool busyState, bool install, bool candidate) {
  if (!rollback) return RoboOtaStartResult::RollbackDisabled;
  if (active || busyState) return RoboOtaStartResult::Busy;
  if (install && !candidate) return RoboOtaStartResult::NoCandidate;
  return RoboOtaStartResult::Accepted;
}

inline const char* roboOtaStartResultName(RoboOtaStartResult result) {
  switch (result) {
    case RoboOtaStartResult::Accepted: return "accepted";
    case RoboOtaStartResult::Busy: return "busy";
    case RoboOtaStartResult::NoMemory: return "no_memory";
    case RoboOtaStartResult::NoCandidate: return "no_candidate";
    case RoboOtaStartResult::RollbackDisabled: return "rollback_disabled";
  }
  return "unknown";
}

inline bool roboOtaVersionEligible(uint32_t version, uint32_t installed, uint32_t minimumExclusive) {
  return version > installed && version > minimumExclusive;
}

enum class RoboOtaPreparationStep : uint8_t { Wait, Launch, Rejected, TimedOut };
inline RoboOtaPreparationStep roboOtaPreparationDecision(bool rejected,bool ready,uint32_t elapsed,bool readyObserved,uint32_t readyElapsed) {
  if(rejected)return RoboOtaPreparationStep::Rejected;
  if(!ready)return elapsed>=13000u?RoboOtaPreparationStep::TimedOut:RoboOtaPreparationStep::Wait;
  return readyObserved&&readyElapsed>=100u?RoboOtaPreparationStep::Launch:RoboOtaPreparationStep::Wait;
}
