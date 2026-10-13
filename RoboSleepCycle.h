#pragma once
#include <stdint.h>

// PD-1.1 energy/sleep cycle (PRODUCT_DIRECTION section 5). Pure policy: the caller injects the clock and
// every input, so transitions are deterministic and host-testable. All time math is millis-wrap safe.
namespace robosleep {
enum class State : uint8_t { Awake, Drowsy, LightSleep, DeepSleep, Waking };
// RemoteSleep/RemoteWake (PD-2.5): the app's Tidurkan/Bangunkan. A remote sleep ignores PIR until a real wake event.
enum class Event : uint8_t { None, Touch, WakeWord, Pir, Alarm, RemoteSleep, RemoteWake };

struct Input {
  uint32_t now = 0;
  float energy = 1.f;               // LivingEyes mood energy 0..1
  bool clockSynced = false;         // quiet hours only count with a synced wall clock
  bool quietHours = false;          // inside the quiet window (ignored when !clockSynced)
  bool presence = false;            // PIR says someone is near
  uint32_t lastInteractionAt = 0;   // millis of the last touch/voice/app interaction
  bool hold = false;                // navigation, focus session, alarm/reminder active: never sleep
  Event event = Event::None;
};

struct Config {
  float drowsyEnergy = .25f;
  uint32_t idleToDrowsyMs = 600000;  // nobody around and no interaction for 10 min
  uint32_t drowsyMs = 120000;        // drowsy phase (spec: 1-3 min)
  uint32_t snoozeMs = 60000;         // a touch cancels drowsiness for a while
  uint32_t lightMinMs = 60000;       // quiet hours deepen light sleep after this
  uint32_t lightToDeepMs = 1800000;  // otherwise after 30 min
  uint32_t wakingMs = 3000;          // wake-up performance
  uint32_t redozeMs = 20000;         // woken at night/by an alarm: back to sleep after 20 s alone
};

inline const char* stateName(State s) {
  switch (s) {
    case State::Awake: return "awake";
    case State::Drowsy: return "drowsy";
    case State::LightSleep: return "lightsleep";
    case State::DeepSleep: return "deepsleep";
    case State::Waking: return "waking";
  }
  return "?";
}

class SleepCycle {
 public:
  explicit SleepCycle(const Config& config = Config()) : config_(config) {}
  void reset(uint32_t now) { state_ = State::Awake; enteredAt_ = now; changed_ = false; redoze_ = redozeQuietOnly_ = pirIgnored_ = false; }
  State state() const { return state_; }
  bool changed() const { return changed_; }          // the last update() changed state
  State previous() const { return previous_; }
  uint32_t enteredAt() const { return enteredAt_; }
  bool asleep() const { return state_ == State::LightSleep || state_ == State::DeepSleep; }
  bool redozePending() const { return redoze_; }      // woken at night/by an alarm, will sleep again when left alone

  State update(const Input& in) {
    changed_ = false;
    const bool quiet = in.clockSynced && in.quietHours;
    const uint32_t inState = in.now - enteredAt_;
    if (in.event == Event::RemoteSleep) {
      if (!in.hold && !asleep()) { redoze_ = false; pirIgnored_ = true; enter(State::LightSleep, in.now); }
      return state_;
    }
    const bool wake = in.hold || in.event == Event::Touch || in.event == Event::WakeWord || in.event == Event::Alarm ||
                      in.event == Event::RemoteWake || (in.event == Event::Pir && !quiet && !pirIgnored_);
    switch (state_) {
      case State::Awake: {
        if (in.hold) break;
        const uint32_t sinceInteraction = in.now - in.lastInteractionAt;
        if (redoze_ && redozeQuietOnly_ && !quiet) redoze_ = false;  // night ended while awake
        if (redoze_) {
          const uint32_t alone = sinceInteraction < inState ? sinceInteraction : inState;
          if (alone >= config_.redozeMs) { redoze_ = false; enter(State::LightSleep, in.now); }
          break;
        }
        // Snooze after any interaction and after every return to Awake (a cancelled drowsy phase must not flap back).
        if (sinceInteraction < config_.snoozeMs || inState < config_.snoozeMs) break;
        const bool tired = in.energy < config_.drowsyEnergy;
        const bool lonely = !in.presence && sinceInteraction >= config_.idleToDrowsyMs;
        if (lonely || tired || quiet) enter(State::Drowsy, in.now);
        break;
      }
      case State::Drowsy:
        if (wake)
          enter(State::Awake, in.now);
        else if (inState >= config_.drowsyMs)
          enter(State::LightSleep, in.now);
        break;
      case State::LightSleep:
      case State::DeepSleep:
        if (wake) {
          redoze_ = in.event != Event::RemoteWake && (quiet || in.event == Event::Alarm);
          redozeQuietOnly_ = quiet && in.event != Event::Alarm;
          enter(State::Waking, in.now);
        } else if (state_ == State::LightSleep && (inState >= config_.lightToDeepMs || (quiet && inState >= config_.lightMinMs))) {
          enter(State::DeepSleep, in.now);
        }
        break;
      case State::Waking:
        if (inState >= config_.wakingMs) enter(State::Awake, in.now);
        break;
    }
    return state_;
  }

 private:
  void enter(State next, uint32_t now) {
    previous_ = state_; state_ = next; enteredAt_ = now; changed_ = true;
    if (next == State::Waking || next == State::Awake) pirIgnored_ = false;
  }
  Config config_;
  State state_ = State::Awake, previous_ = State::Awake;
  uint32_t enteredAt_ = 0;
  bool changed_ = false, redoze_ = false, redozeQuietOnly_ = false, pirIgnored_ = false;
};
}  // namespace robosleep
