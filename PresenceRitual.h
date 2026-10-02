#pragma once

#include <stdint.h>

namespace companion {

enum class PresenceEvent : uint8_t { None, Arrived, Returned };

// Debounces the PIR's probable-presence signal and emits one local arrival cue.
// It does not identify a person or retain a presence history.
class PresenceRitual {
 public:
  static constexpr uint32_t DebounceMs = 250;
  static constexpr uint32_t AbsenceDebounceMs = 2000;
  static constexpr uint32_t ReturnAfterMs = 120000;
  static constexpr uint32_t GreetingCooldownMs = 1800000;

  void reset(uint32_t now, bool present) {
    initialized_ = true;
    stablePresent_ = present;
    candidatePresent_ = present;
    candidateAt_ = now;
    absentSince_ = present ? 0 : now;
    hasGreeted_ = false;
  }

  PresenceEvent update(bool rawPresent, uint32_t now, bool eligible) {
    if (!initialized_) {
      reset(now, rawPresent);
      return PresenceEvent::None;
    }
    if (rawPresent != candidatePresent_) {
      candidatePresent_ = rawPresent;
      candidateAt_ = now;
    }
    const uint32_t debounce = candidatePresent_ ? DebounceMs : AbsenceDebounceMs;
    if (candidatePresent_ != stablePresent_ && uint32_t(now - candidateAt_) >= debounce) {
      stablePresent_ = candidatePresent_;
      if (!stablePresent_) {
        absentSince_ = now;
        return PresenceEvent::None;
      }
      const bool returned = absentSince_ && uint32_t(now - absentSince_) >= ReturnAfterMs;
      absentSince_ = 0;
      pendingEvent_ = returned ? PresenceEvent::Returned : PresenceEvent::Arrived;
      pendingAt_ = now;
    }
    return pending(eligible, now);
  }

  PresenceEvent pending(bool eligible, uint32_t now) {
    if (pendingEvent_ == PresenceEvent::None) return PresenceEvent::None;
    if (uint32_t(now - pendingAt_) > 5000u ||
        (hasGreeted_ && uint32_t(now - lastGreetingAt_) < GreetingCooldownMs)) {
      pendingEvent_ = PresenceEvent::None;
      return PresenceEvent::None;
    }
    if (!eligible) return PresenceEvent::None;
    return pendingEvent_;
  }

  void acknowledge(uint32_t now) {
    if (pendingEvent_ == PresenceEvent::None) return;
    hasGreeted_ = true;
    lastGreetingAt_ = now;
    pendingEvent_ = PresenceEvent::None;
  }

  bool present() const { return stablePresent_; }
  bool initialized() const { return initialized_; }
  uint32_t lastGreetingAt() const { return lastGreetingAt_; }

 private:
  bool initialized_ = false;
  bool stablePresent_ = false;
  bool candidatePresent_ = false;
  bool hasGreeted_ = false;
  uint32_t candidateAt_ = 0;
  uint32_t absentSince_ = 0;
  uint32_t lastGreetingAt_ = 0;
  uint32_t pendingAt_ = 0;
  PresenceEvent pendingEvent_ = PresenceEvent::None;
};

}  // namespace companion
