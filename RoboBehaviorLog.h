#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// PD-0.3 behavior evidence lines (LEV,BEH,...). tools/behavior_log_report.py parses exactly these
// formats; tests/robo_behavior_log_test.cpp pins them. Formatters only; the sketch owns the timing.
namespace robobeh {
// zone: head|side|double; latMs: raw touch edge -> first OLED present() after it.
inline int touch(char* out, size_t cap, const char* zone, uint32_t latMs) {
  return snprintf(out, cap, "LEV,BEH,TOUCH,zone=%s,latMs=%lu\n", zone ? zone : "?", static_cast<unsigned long>(latMs));
}
// id: touch zone that caused it; var: first LivingEyes cue id of the reaction (differs per clip variant/performance).
inline int react(char* out, size_t cap, const char* id, unsigned variant) {
  return snprintf(out, cap, "LEV,BEH,REACT,id=%s,var=%u\n", id ? id : "?", variant);
}
inline int sleep(char* out, size_t cap, const char* from, const char* to) {
  return snprintf(out, cap, "LEV,BEH,SLEEP,from=%s,to=%s\n", from ? from : "?", to ? to : "?");
}
// spont=1: not caused by the user (ambient chirp, greeting, activity cue).
inline int sound(char* out, size_t cap, const char* cue, bool spontaneous) {
  return snprintf(out, cap, "LEV,BEH,SOUND,cue=%s,spont=%u\n", cue ? cue : "?", spontaneous ? 1u : 0u);
}
// act: LivingEyes BehaviorKind of an autonomous idle clip; life: LifeActivity at that moment.
inline int idle(char* out, size_t cap, unsigned action, unsigned life) {
  return snprintf(out, cap, "LEV,BEH,IDLE,act=%u,life=%u\n", action, life);
}
}  // namespace robobeh
