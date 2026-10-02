#pragma once

#include <stdint.h>
#include "CompanionActivity.h"
#include "CompanionActivityCatalog.h"

namespace companion {

struct SchedulerContext {
  bool enabled = false;
  bool busy = false;
  bool ownerPaused = false;
  bool safetyRecovery = false;
  bool microphonePrivate = false;
  bool probablePresence = false;
  bool clockValid = false;
  bool sensorsFresh = false;
  bool localAudioAllowed = false;
  uint32_t idleMs = 0;
  float energy = 0.7f;
  float curiosity = 0.6f;
  float boredom = 0.1f;
  float socialNeed = 0.2f;
  int8_t preferenceBias[8]{};
};

enum class SchedulerEvent : uint8_t { None, Started, Paused, Resumed, Completed, Interrupted, Cancelled, Rejected };

struct SchedulerDecision {
  SchedulerEvent event = SchedulerEvent::None;
  ActivityBlock reason = ActivityBlock::None;
  uint8_t activity = 0;
  uint32_t durationMs = 0;
  uint32_t nextEligibleMs = 0;
};

class CompanionScheduler {
 public:
  static constexpr uint32_t MinimumActivityMs = 20000u;
  static constexpr uint32_t MaximumActivityMs = 90000u;
  static constexpr uint32_t MinimumGapMs = 120000u;
  static constexpr uint32_t MaximumGapMs = 300000u;
  static constexpr uint32_t MinimumIdleMs = 30000u;

  explicit CompanionScheduler(uint32_t seed = 0x524f424fu) : random_(seed ? seed : 1u) {}

  SchedulerDecision update(uint32_t now, const SchedulerContext& context,
                           const uint8_t* recentIds = nullptr, uint8_t recentCount = 0,
                           uint8_t explicitActivity = 0) {
    if (activeId_) {
      const ActivityBlock blocked = eligibility(context, activityDefinition(activeId_));
      if (blocked == ActivityBlock::PausedByOwner) {
        if (!paused_) {
          const uint32_t used = elapsed(now, startedAt_);
          if (used >= durationMs_) return end(now, SchedulerEvent::Completed, ActivityBlock::None);
          pausedRemainingMs_ = durationMs_ - used;
          paused_ = true;
          return decision(SchedulerEvent::Paused, blocked, activeId_, pausedRemainingMs_);
        }
        return decision(SchedulerEvent::None, blocked, activeId_, pausedRemainingMs_);
      }
      if (blocked != ActivityBlock::None) return end(now, SchedulerEvent::Interrupted, blocked);
      if (paused_) {
        paused_ = false;
        startedAt_ = now;
        durationMs_ = pausedRemainingMs_;
        pausedRemainingMs_ = 0;
        return decision(SchedulerEvent::Resumed, ActivityBlock::None, activeId_, durationMs_);
      }
      if (elapsed(now, startedAt_) >= durationMs_)
        return end(now, SchedulerEvent::Completed, ActivityBlock::None);
      return decision(SchedulerEvent::None, ActivityBlock::None, activeId_, durationMs_);
    }

    if (explicitActivity) {
      const ActivityDefinition* definition = activityDefinition(explicitActivity);
      if (!definition) return decision(SchedulerEvent::Rejected, ActivityBlock::NoEligibleActivity, explicitActivity, 0);
      const ActivityBlock blocked = eligibility(context, definition);
      if (blocked != ActivityBlock::None)
        return decision(SchedulerEvent::Rejected, blocked, explicitActivity, 0);
      return begin(now, *definition);
    }

    const ActivityBlock blocked = eligibility(context, nullptr);
    if (blocked != ActivityBlock::None) return decision(SchedulerEvent::None, blocked, 0, 0);
    if (context.idleMs < MinimumIdleMs) return decision(SchedulerEvent::None, ActivityBlock::Busy, 0, 0);
    if (hasSelection_ && !due(now, nextEligibleAt_))
      return decision(SchedulerEvent::None, ActivityBlock::Cooldown, 0, 0);

    const ActivityDefinition* candidates[7]{};
    uint8_t weights[7]{};
    uint8_t count = 0;
    for (uint8_t id = 1; id <= activityCatalogSize(); ++id) {
      const ActivityDefinition* candidate = activityDefinition(id);
      if (!candidate || eligibility(context, candidate) != ActivityBlock::None) continue;
      bool recent = false;
      const uint8_t recentLimit = recentCount < 3 ? recentCount : 3;
      for (uint8_t i = 0; recentIds && i < recentLimit; ++i)
        if (recentIds[i] == id) { recent = true; break; }
      if (!recent) {
        candidates[count] = candidate;
        weights[count] = selectionWeight(*candidate, context);
        ++count;
      }
    }
    if (!count) return decision(SchedulerEvent::None, ActivityBlock::NoEligibleActivity, 0, 0);

    uint16_t totalWeight = 0;
    for (uint8_t i = 0; i < count; ++i) totalWeight = uint16_t(totalWeight + weights[i]);
    uint16_t choice = uint16_t(random32() % totalWeight);
    uint8_t selected = 0;
    while (selected + 1u < count && choice >= weights[selected]) {
      choice = uint16_t(choice - weights[selected]);
      ++selected;
    }
    return begin(now, *candidates[selected]);
  }

  SchedulerDecision cancel(uint32_t now, ActivityBlock reason = ActivityBlock::None) {
    return activeId_ ? end(now, SchedulerEvent::Cancelled, reason)
                     : decision(SchedulerEvent::None, reason, 0, 0);
  }

  bool active() const { return activeId_ != 0; }
  bool paused() const { return paused_; }
  uint8_t activity() const { return activeId_; }
  uint32_t startedAt() const { return startedAt_; }
  uint32_t durationMs() const { return durationMs_; }
  uint32_t remainingMs(uint32_t now) const {
    if (paused_) return pausedRemainingMs_;
    const uint32_t used = elapsed(now, startedAt_);
    return !activeId_ || used >= durationMs_ ? 0u : durationMs_ - used;
  }
  uint32_t nextEligibleAt() const { return nextEligibleAt_; }
  uint32_t nextEligibleIn(uint32_t now) const {
    return !hasSelection_ || due(now, nextEligibleAt_) ? 0u : uint32_t(nextEligibleAt_ - now);
  }

 private:
  static uint32_t elapsed(uint32_t now, uint32_t then) { return uint32_t(now - then); }
  static bool due(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }

  static ActivityBlock eligibility(const SchedulerContext& context, const ActivityDefinition* definition) {
    if (context.safetyRecovery) return ActivityBlock::Safety;
    if (context.busy) return ActivityBlock::Busy;
    if (context.ownerPaused) return ActivityBlock::PausedByOwner;
    if (!context.enabled) return ActivityBlock::Disabled;
    if (context.microphonePrivate) return ActivityBlock::MicrophonePrivate;
    if (!context.probablePresence) return ActivityBlock::NoPresence;
    if (!context.clockValid) return ActivityBlock::ClockInvalid;
    if (!context.sensorsFresh) return ActivityBlock::StaleSensor;
    if (definition && (definition->resources & ActivityResourceLocalAudio) && !context.localAudioAllowed)
      return ActivityBlock::QuietHours;
    return ActivityBlock::None;
  }

  static uint8_t selectionWeight(const ActivityDefinition& definition, const SchedulerContext& context) {
    float need = 1.0f;
    switch (definition.id) {
      case CompanionActivityId::CuriousLook: need += context.curiosity * 3.0f; break;
      case CompanionActivityId::ExpressionPractice: need += context.boredom * 2.0f + context.energy; break;
      case CompanionActivityId::RhythmPlay: need += context.energy * 2.0f + context.boredom; break;
      case CompanionActivityId::Daydream: need += context.boredom * 2.0f + (1.0f - context.energy); break;
      case CompanionActivityId::StretchReset: need += context.energy < 0.6f ? 2.0f : 0.5f; break;
      case CompanionActivityId::Rest: need += (1.0f - context.energy) * 3.0f; break;
      case CompanionActivityId::QuietCompany: need += context.socialNeed * 2.0f; break;
    }
    const int8_t preference = context.preferenceBias[uint8_t(definition.id)];
    need += float(preference) * 0.35f;
    if (need < 0.25f) need = 0.25f;
    if (need > 8.0f) need = 8.0f;
    return uint8_t(need * 10.0f);
  }

  uint32_t random32() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_;
  }

  SchedulerDecision begin(uint32_t now, const ActivityDefinition& definition) {
    const uint32_t span = definition.maxDurationMs - definition.minDurationMs;
    durationMs_ = definition.minDurationMs + (span ? random32() % (span + 1u) : 0u);
    if (durationMs_ < MinimumActivityMs) durationMs_ = MinimumActivityMs;
    if (durationMs_ > MaximumActivityMs) durationMs_ = MaximumActivityMs;
    activeId_ = uint8_t(definition.id);
    startedAt_ = now;
    paused_ = false;
    pausedRemainingMs_ = 0;
    return decision(SchedulerEvent::Started, ActivityBlock::None, activeId_, durationMs_);
  }

  SchedulerDecision end(uint32_t now, SchedulerEvent event, ActivityBlock reason) {
    const uint8_t endedId = activeId_;
    activeId_ = 0;
    paused_ = false;
    pausedRemainingMs_ = 0;
    const uint32_t gap = MinimumGapMs + random32() % (MaximumGapMs - MinimumGapMs + 1u);
    nextEligibleAt_ = now + gap;
    hasSelection_ = true;
    return decision(event, reason, endedId, durationMs_);
  }

  SchedulerDecision decision(SchedulerEvent event, ActivityBlock reason, uint8_t activity, uint32_t duration) const {
    SchedulerDecision result;
    result.event = event;
    result.reason = reason;
    result.activity = activity;
    result.durationMs = duration;
    result.nextEligibleMs = nextEligibleAt_;
    return result;
  }

  uint32_t random_;
  uint32_t startedAt_ = 0;
  uint32_t durationMs_ = 0;
  uint32_t nextEligibleAt_ = 0;
  uint8_t activeId_ = 0;
  bool hasSelection_ = false;
  bool paused_ = false;
  uint32_t pausedRemainingMs_ = 0;
};

}  // namespace companion
