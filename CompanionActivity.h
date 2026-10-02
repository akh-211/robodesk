#pragma once
#include <stdint.h>

namespace companion {

// Mirrors the coarse activities exposed by LivingEyes' LifeIntent without
// introducing a second visual behavior director.
enum class ActivityState : uint8_t { Idle, Running, Paused, Completed, Interrupted, Cancelled };
enum class ActivityBlock : uint8_t {
  None, Safety, Busy, PausedByOwner, Disabled, MicrophonePrivate,
  NoPresence, ClockInvalid, StaleSensor, QuietHours, Cooldown, NoEligibleActivity
};
enum class ActivityOutcomeKind : uint8_t { Completed, Interrupted, Cancelled };
enum class ActivityTransitionReason : uint8_t { None, Started, Paused, Resumed, Completed, Interrupted, Cancelled };

struct ActivityOutcome {
  uint8_t activity = 0;
  ActivityOutcomeKind kind = ActivityOutcomeKind::Completed;
  ActivityBlock reason = ActivityBlock::None;
  uint32_t startedAt = 0;
  uint32_t endedAt = 0;
  uint32_t durationMs = 0;
};

struct ActivityPromptContext {
  ActivityState state = ActivityState::Idle;
  uint8_t currentActivity = 0;
  uint32_t currentAgeMs = 0;
  bool hasOutcome = false;
  bool outcomeRecent = false;
  ActivityOutcome outcome{};
  uint32_t outcomeAgeMs = 0;
};

struct BehaviorContext {
  bool busy = false;
  bool ownerPaused = false;
  bool enabled = true;
  bool safetyRecovery = false;
  bool microphonePrivate = false;
  bool probablePresence = false;
  bool touched = false;
  bool clockValid = false;
  bool environmentFresh = false;
  bool pressureFresh = false;
  bool imuFresh = false;
  uint16_t localMinute = 0;

  bool allowsAutonomousActivity() const {
    return enabled && !busy && !ownerPaused && !safetyRecovery;
  }
  ActivityBlock activityBlock() const {
    if (safetyRecovery) return ActivityBlock::Safety;
    if (busy) return ActivityBlock::Busy;
    if (ownerPaused) return ActivityBlock::PausedByOwner;
    if (!enabled) return ActivityBlock::Disabled;
    return ActivityBlock::None;
  }
};

class ActivityTracker {
 public:
  static constexpr uint8_t OutcomeCapacity = 16;

  // LifeIntent is a short-lived renderer hint. Observe it for truthful current
  // status, but do not count its expiry or replacement as a user-visible macro
  // activity outcome.
  void syncIntent(uint8_t lifeActivity, uint32_t now, bool allowed,
                  ActivityBlock block = ActivityBlock::None) {
    if (lifeActivity > 9) lifeActivity = 0;
    const bool wasActive = isActive();
    if (!allowed || lifeActivity == 0) {
      if (wasActive && !allowed && lifeActivity == activity_) {
        if (state_ == ActivityState::Running) {
          state_ = ActivityState::Paused;
          block_ = block;
          changedAt_ = now;
          lastTransition_ = ActivityTransitionReason::Paused;
          lastTransitionAt_ = now;
        }
      } else {
        activity_ = 0;
        state_ = ActivityState::Idle;
        block_ = allowed ? ActivityBlock::None : block;
      }
      return;
    }
    block_ = ActivityBlock::None;
    if (!wasActive || activity_ != lifeActivity) {
      activity_ = lifeActivity;
      state_ = ActivityState::Running;
      startedAt_ = changedAt_ = now;
      ++transitions_;
      lastTransition_ = ActivityTransitionReason::Started;
      lastTransitionAt_ = now;
    } else if (state_ != ActivityState::Running) {
      state_ = ActivityState::Running;
      changedAt_ = now;
      ++transitions_;
      lastTransition_ = ActivityTransitionReason::Resumed;
      lastTransitionAt_ = now;
    }
  }

  void sync(uint8_t lifeActivity, uint32_t now, bool allowed,
            ActivityBlock block = ActivityBlock::None) {
    if (lifeActivity > 9) lifeActivity = 0;
    if (!allowed) {
      if (isActive() && lifeActivity == 0) {
        finish(ActivityOutcomeKind::Interrupted, now, block);
      } else if (isActive() && lifeActivity != activity_) {
        finish(ActivityOutcomeKind::Interrupted, now, block);
      } else if (activity_ != 0 && state_ == ActivityState::Running) {
        state_ = ActivityState::Paused;
        block_ = block;
        changedAt_ = now;
        pausedAt_ = now;
        lastTransition_ = ActivityTransitionReason::Paused;
        lastTransitionAt_ = now;
      } else if (activity_ == 0) {
        state_ = ActivityState::Idle;
        block_ = block;
      }
      return;
    }

    block_ = ActivityBlock::None;
    if (lifeActivity == 0) {
      if (isActive()) {
        finish(ActivityOutcomeKind::Completed, now, ActivityBlock::None);
      } else if (activity_ == 0) {
        state_ = ActivityState::Idle;
      } else if (state_ == ActivityState::Completed || state_ == ActivityState::Interrupted ||
                 state_ == ActivityState::Cancelled) {
        state_ = ActivityState::Idle;
        block_ = ActivityBlock::None;
      }
      return;
    }

    if (isActive() && activity_ != lifeActivity) {
      finish(ActivityOutcomeKind::Interrupted, now, ActivityBlock::None);
    }
    if (activity_ != lifeActivity || !isActive()) {
      activity_ = lifeActivity;
      state_ = ActivityState::Running;
      pausedAt_ = 0;
      pausedAccumulatedMs_ = 0;
      startedAt_ = changedAt_ = now;
      block_ = ActivityBlock::None;
      ++transitions_;
      lastTransition_ = ActivityTransitionReason::Started;
      lastTransitionAt_ = now;
      return;
    }
    if (state_ != ActivityState::Running) {
      if (state_ == ActivityState::Paused) pausedAccumulatedMs_ += uint32_t(now - pausedAt_);
      state_ = ActivityState::Running;
      pausedAt_ = 0;
      changedAt_ = now;
      ++transitions_;
      lastTransition_ = ActivityTransitionReason::Resumed;
      lastTransitionAt_ = now;
    }
  }

  void cancel(uint32_t now) {
    if (isActive()) finish(ActivityOutcomeKind::Cancelled, now, ActivityBlock::None);
  }
  void clearHistory() { outcomeCount_ = 0; outcomeNext_ = 0; }

  uint8_t activity() const { return activity_; }
  ActivityState state() const { return state_; }
  ActivityBlock block() const { return block_; }
  uint32_t startedAt() const { return startedAt_; }
  uint32_t changedAt() const { return changedAt_; }
  uint32_t transitions() const { return transitions_; }
  ActivityTransitionReason lastTransition() const { return lastTransition_; }
  uint32_t lastTransitionAt() const { return lastTransitionAt_; }
  uint8_t outcomeCount() const { return outcomeCount_; }
  bool latestOutcome(ActivityOutcome& out) const {
    if (!outcomeCount_) return false;
    const uint8_t index = uint8_t((outcomeNext_ + OutcomeCapacity - 1) % OutcomeCapacity);
    out = outcomes_[index];
    return true;
  }
  bool outcomeNewest(uint8_t newestIndex, ActivityOutcome& out) const {
    if (newestIndex >= outcomeCount_) return false;
    const uint8_t index = uint8_t((outcomeNext_ + OutcomeCapacity - 1u - newestIndex) % OutcomeCapacity);
    out = outcomes_[index];
    return true;
  }
  ActivityPromptContext promptContext(uint32_t now, uint32_t recentWindowMs = 60000u) const {
    ActivityPromptContext context;
    context.state = state_;
    if (state_ == ActivityState::Running || state_ == ActivityState::Paused) {
      context.currentActivity = activity_;
      context.currentAgeMs = uint32_t(now - startedAt_);
    }
    context.hasOutcome = latestOutcome(context.outcome);
    if (context.hasOutcome) {
      context.outcomeAgeMs = uint32_t(now - context.outcome.endedAt);
      context.outcomeRecent = context.outcomeAgeMs <= recentWindowMs;
    }
    return context;
  }

 private:
  bool isActive() const {
    return activity_ != 0 && (state_ == ActivityState::Running || state_ == ActivityState::Paused);
  }
  void finish(ActivityOutcomeKind kind, uint32_t now, ActivityBlock reason) {
    ActivityOutcome& out = outcomes_[outcomeNext_];
    out.activity = activity_;
    out.kind = kind;
    out.reason = reason;
    out.startedAt = startedAt_;
    out.endedAt = now;
    const uint32_t pausedNow = state_ == ActivityState::Paused ? uint32_t(now - pausedAt_) : 0u;
    out.durationMs = uint32_t(now - startedAt_) - pausedAccumulatedMs_ - pausedNow;
    outcomeNext_ = uint8_t((outcomeNext_ + 1) % OutcomeCapacity);
    if (outcomeCount_ < OutcomeCapacity) ++outcomeCount_;
    state_ = kind == ActivityOutcomeKind::Completed ? ActivityState::Completed :
             kind == ActivityOutcomeKind::Interrupted ? ActivityState::Interrupted : ActivityState::Cancelled;
    block_ = reason;
    changedAt_ = now;
    pausedAt_ = 0;
    pausedAccumulatedMs_ = 0;
    ++transitions_;
    lastTransition_ = kind == ActivityOutcomeKind::Completed ? ActivityTransitionReason::Completed :
                      kind == ActivityOutcomeKind::Interrupted ? ActivityTransitionReason::Interrupted :
                      ActivityTransitionReason::Cancelled;
    lastTransitionAt_ = now;
  }

  uint8_t activity_ = 0;
  ActivityState state_ = ActivityState::Idle;
  ActivityBlock block_ = ActivityBlock::None;
  uint32_t startedAt_ = 0;
  uint32_t changedAt_ = 0;
  uint32_t transitions_ = 0;
  ActivityTransitionReason lastTransition_ = ActivityTransitionReason::None;
  uint32_t lastTransitionAt_ = 0;
  uint32_t pausedAt_ = 0;
  uint32_t pausedAccumulatedMs_ = 0;
  ActivityOutcome outcomes_[OutcomeCapacity]{};
  uint8_t outcomeCount_ = 0;
  uint8_t outcomeNext_ = 0;
};

inline const char* activityStateName(ActivityState state) {
  switch (state) {
    case ActivityState::Running: return "running";
    case ActivityState::Paused: return "paused";
    case ActivityState::Completed: return "completed";
    case ActivityState::Interrupted: return "interrupted";
    case ActivityState::Cancelled: return "cancelled";
    default: return "idle";
  }
}

inline const char* activityOutcomeName(ActivityOutcomeKind kind) {
  switch (kind) {
    case ActivityOutcomeKind::Interrupted: return "interrupted";
    case ActivityOutcomeKind::Cancelled: return "cancelled";
    default: return "completed";
  }
}

inline const char* activityTransitionReasonName(ActivityTransitionReason reason) {
  switch (reason) {
    case ActivityTransitionReason::Started: return "started";
    case ActivityTransitionReason::Paused: return "paused";
    case ActivityTransitionReason::Resumed: return "resumed";
    case ActivityTransitionReason::Completed: return "completed";
    case ActivityTransitionReason::Interrupted: return "interrupted";
    case ActivityTransitionReason::Cancelled: return "cancelled";
    default: return "none";
  }
}

inline const char* activityBlockName(ActivityBlock block) {
  switch (block) {
    case ActivityBlock::Safety: return "safety";
    case ActivityBlock::Busy: return "busy";
    case ActivityBlock::PausedByOwner: return "paused";
    case ActivityBlock::Disabled: return "disabled";
    case ActivityBlock::MicrophonePrivate: return "mic_private";
    case ActivityBlock::NoPresence: return "no_presence";
    case ActivityBlock::ClockInvalid: return "clock_invalid";
    case ActivityBlock::StaleSensor: return "stale_sensor";
    case ActivityBlock::QuietHours: return "quiet_hours";
    case ActivityBlock::Cooldown: return "cooldown";
    case ActivityBlock::NoEligibleActivity: return "no_activity";
    default: return "none";
  }
}

inline const char* lifeActivityName(uint8_t activity) {
  switch (activity) {
    case 1: return "rest";
    case 2: return "nap";
    case 3: return "seek_social";
    case 4: return "explore";
    case 5: return "observe";
    case 6: return "play";
    case 7: return "self_entertain";
    case 8: return "recover";
    case 9: return "recharge";
    default: return "none";
  }
}

inline const char* currentActivityName(ActivityState state, uint8_t activity) {
  return state == ActivityState::Running || state == ActivityState::Paused
           ? lifeActivityName(activity)
           : lifeActivityName(0);
}

enum class HeadTouchGesture : uint8_t { None, Tap, DoubleTap, Petting, PettingEnd };

class HeadTouchGestureDetector {
 public:
  static constexpr uint32_t DoubleTapWindowMs = 420;
  static constexpr uint32_t PettingThresholdMs = 1200;

  HeadTouchGesture update(bool pressed, uint32_t now) {
    if (pressed && !down_) {
      down_ = true;
      petting_ = false;
      pressedAt_ = now;
      return HeadTouchGesture::None;
    }
    if (pressed && down_) {
      if (!petting_ && uint32_t(now - pressedAt_) >= PettingThresholdMs) {
        petting_ = true;
        return HeadTouchGesture::Petting;
      }
      return HeadTouchGesture::None;
    }
    if (!pressed && down_) {
      down_ = false;
      if (petting_) {
        petting_ = false;
        return HeadTouchGesture::PettingEnd;
      }
      if (hasTap_ && uint32_t(now - lastTapAt_) <= DoubleTapWindowMs) {
        hasTap_ = false;
        return HeadTouchGesture::DoubleTap;
      }
      hasTap_ = true;
      lastTapAt_ = now;
      return HeadTouchGesture::Tap;
    }
    if (hasTap_ && uint32_t(now - lastTapAt_) > DoubleTapWindowMs) hasTap_ = false;
    return HeadTouchGesture::None;
  }

 private:
  bool down_ = false;
  bool petting_ = false;
  bool hasTap_ = false;
  uint32_t pressedAt_ = 0;
  uint32_t lastTapAt_ = 0;
};

} // namespace companion
