#pragma once

#include <stdint.h>

namespace companion {

enum class CompanionActivityId : uint8_t {
  CuriousLook = 1,
  ExpressionPractice = 2,
  RhythmPlay = 3,
  Daydream = 4,
  StretchReset = 5,
  Rest = 6,
  QuietCompany = 7
};

enum ActivityResource : uint8_t {
  ActivityResourceDisplay = 1u << 0,
  ActivityResourceLocalAudio = 1u << 1
};

enum class ActivityCancelPolicy : uint8_t { Immediate, FinishCurrentCue };

struct ActivityDefinition {
  CompanionActivityId id;
  const char* name;
  uint8_t lifeActivity;
  uint32_t minDurationMs;
  uint32_t defaultDurationMs;
  uint32_t maxDurationMs;
  uint8_t resources;
  ActivityCancelPolicy cancelPolicy;
};

inline const ActivityDefinition* activityDefinition(uint8_t id) {
  static const ActivityDefinition definitions[] = {
    {CompanionActivityId::CuriousLook, "curious_look", 5, 20000u, 30000u, 60000u,
      ActivityResourceDisplay, ActivityCancelPolicy::Immediate},
    {CompanionActivityId::ExpressionPractice, "expression_practice", 6, 20000u, 30000u, 60000u,
      ActivityResourceDisplay, ActivityCancelPolicy::Immediate},
    {CompanionActivityId::RhythmPlay, "rhythm_play", 6, 20000u, 30000u, 60000u,
      ActivityResourceDisplay | ActivityResourceLocalAudio, ActivityCancelPolicy::FinishCurrentCue},
    {CompanionActivityId::Daydream, "daydream", 7, 20000u, 30000u, 60000u,
      ActivityResourceDisplay, ActivityCancelPolicy::Immediate},
    {CompanionActivityId::StretchReset, "stretch_reset", 1, 20000u, 30000u, 60000u,
      ActivityResourceDisplay, ActivityCancelPolicy::Immediate},
    {CompanionActivityId::Rest, "rest", 1, 20000u, 60000u, 90000u,
      ActivityResourceDisplay, ActivityCancelPolicy::Immediate},
    {CompanionActivityId::QuietCompany, "quiet_company", 1, 20000u, 60000u, 90000u,
      ActivityResourceDisplay, ActivityCancelPolicy::Immediate}
  };
  for (const auto& definition : definitions)
    if (uint8_t(definition.id) == id) return &definition;
  return nullptr;
}

inline uint8_t activityCatalogSize() { return 7; }

inline const char* companionActivityName(uint8_t id) {
  const ActivityDefinition* definition = activityDefinition(id);
  return definition ? definition->name : "none";
}

}  // namespace companion
