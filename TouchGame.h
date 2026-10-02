#pragma once

#include <stdint.h>

namespace companion {

enum class TouchGameState : uint8_t { Idle, Active, Completed, Failed, Cancelled, TimedOut };
enum class TouchGameSymbol : uint8_t { Tap, Hold };

inline bool touchGameTouchGate(bool environmentEligible, bool headTouchStable, bool sessionActive) {
  return environmentEligible && (sessionActive || !headTouchStable);
}

// A short local pattern game. The caller must explicitly start it and feed
// debounced head-touch press durations. It owns no audio or visual renderer.
class TouchGame {
 public:
  static constexpr uint32_t SessionLimitMs = 45000;
  static constexpr uint32_t ShortPressMaxMs = 350;
  static constexpr uint32_t HoldPressMinMs = 600;
  static constexpr uint32_t HoldPressMaxMs = 1050;
  static constexpr uint8_t PatternLength = 3;

  bool start(uint32_t now, bool acceptedAndEligible) {
    if (!acceptedAndEligible || state_ == TouchGameState::Active) return false;
    state_ = TouchGameState::Active;
    step_ = 0;
    startedAt_ = changedAt_ = now;
    return true;
  }

  TouchGameState update(uint32_t now, bool eligible) {
    if (state_ != TouchGameState::Active) return state_;
    if (!eligible) {
      state_ = TouchGameState::Cancelled;
      changedAt_ = now;
    } else if (uint32_t(now - startedAt_) >= SessionLimitMs) {
      state_ = TouchGameState::TimedOut;
      changedAt_ = now;
    }
    return state_;
  }

  TouchGameState press(uint32_t heldMs, uint32_t now, bool eligible) {
    update(now, eligible);
    if (state_ != TouchGameState::Active) return state_;
    TouchGameSymbol symbol;
    if (heldMs >= 1 && heldMs <= ShortPressMaxMs) symbol = TouchGameSymbol::Tap;
    else if (heldMs >= HoldPressMinMs && heldMs <= HoldPressMaxMs) symbol = TouchGameSymbol::Hold;
    else {
      state_ = TouchGameState::Failed;
      changedAt_ = now;
      return state_;
    }
    static constexpr TouchGameSymbol Pattern[PatternLength] = {
      TouchGameSymbol::Tap, TouchGameSymbol::Hold, TouchGameSymbol::Tap
    };
    if (symbol != Pattern[step_]) {
      state_ = TouchGameState::Failed;
      changedAt_ = now;
      return state_;
    }
    ++step_;
    changedAt_ = now;
    if (step_ == PatternLength) state_ = TouchGameState::Completed;
    return state_;
  }

  void cancel(uint32_t now) {
    if (state_ != TouchGameState::Active) return;
    state_ = TouchGameState::Cancelled;
    changedAt_ = now;
  }

  TouchGameState state() const { return state_; }
  uint8_t step() const { return step_; }
  uint32_t startedAt() const { return startedAt_; }
  uint32_t changedAt() const { return changedAt_; }
  bool active() const { return state_ == TouchGameState::Active; }

 private:
  TouchGameState state_ = TouchGameState::Idle;
  uint8_t step_ = 0;
  uint32_t startedAt_ = 0;
  uint32_t changedAt_ = 0;
};

inline const char* touchGameStateName(TouchGameState state) {
  switch (state) {
    case TouchGameState::Active: return "active";
    case TouchGameState::Completed: return "completed";
    case TouchGameState::Failed: return "failed";
    case TouchGameState::Cancelled: return "cancelled";
    case TouchGameState::TimedOut: return "timed_out";
    default: return "idle";
  }
}

} // namespace companion
