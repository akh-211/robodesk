#pragma once
#include <stdint.h>
#include <livingeyes/BehaviorNovelty.h>

// PD-1.4 idle layer B (PRODUCT_DIRECTION section 6): a small action every 15-40 s, only while awake, someone is
// around, nothing else runs and >=25 s after the last interaction. Never the same action twice in a row;
// LivingEyes' BehaviorNoveltyTracker also damps recently used ones.
namespace roboidle {
enum class Action : uint8_t { None, Scan, PeekLeft, PeekRight, GiggleShy, Yawn, DoubleBlink, Count };

inline const char* actionName(Action a) {
  switch (a) {
    case Action::Scan: return "scan";
    case Action::PeekLeft: return "peek_left";
    case Action::PeekRight: return "peek_right";
    case Action::GiggleShy: return "giggle_shy";
    case Action::Yawn: return "yawn";
    case Action::DoubleBlink: return "double_blink";
    default: return "none";
  }
}

struct Context {
  uint32_t now = 0;
  bool awake = true, presence = false, busy = false;
  uint32_t lastInteractionAt = 0;
  float energy = 1.f;
};

class Picker {
 public:
  static constexpr uint32_t MinGapMs = 15000, MaxGapMs = 40000, AfterInteractionMs = 25000;
  static constexpr float LowEnergy = .35f;

  explicit Picker(uint32_t seed = 0x9e3779b9u) : rng_(seed ? seed : 1u) {}
  void seed(uint32_t seed) { rng_ = seed ? seed : 1u; }
  void reset(uint32_t now) { novelty_.reset(); last_ = Action::None; started_ = true; schedule(now); }
  Action last() const { return last_; }
  uint32_t nextAt() const { return nextAt_; }

  Action update(const Context& c) {
    if (!started_) { started_ = true; schedule(c.now); }
    const bool eligible = c.awake && c.presence && !c.busy && uint32_t(c.now - c.lastInteractionAt) >= AfterInteractionMs;
    if (!eligible) {
      // Keep the gap honest: the next beat comes a full gap after things settle, not immediately.
      if (int32_t(nextAt_ - c.now) < int32_t(MinGapMs)) nextAt_ = c.now + MinGapMs;
      return Action::None;
    }
    if (int32_t(c.now - nextAt_) < 0) return Action::None;
    float best = -1.f;
    Action pick = Action::None;
    for (uint8_t i = 1; i < uint8_t(Action::Count); ++i) {
      const Action a = Action(i);
      if (a == last_) continue;
      const float utility = weight(a, c.energy);
      if (utility <= 0) continue;
      const float jitter = .75f + .5f * float(next() % 1000u) / 1000.f;
      const float score = novelty_.score(uint16_t(i), utility) * jitter;
      if (score > best) { best = score; pick = a; }
    }
    if (pick == Action::None) return pick;
    novelty_.commit(uint16_t(pick));
    last_ = pick;
    schedule(c.now);
    return pick;
  }

 private:
  static float weight(Action a, float energy) {
    switch (a) {
      case Action::Scan: return 1.f;
      case Action::PeekLeft:
      case Action::PeekRight: return .7f;
      case Action::GiggleShy: return energy >= .5f ? .55f : .2f;
      case Action::Yawn: return energy < LowEnergy ? 1.2f : 0.f;
      case Action::DoubleBlink: return .8f;
      default: return 0.f;
    }
  }
  uint32_t next() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  void schedule(uint32_t now) { nextAt_ = now + MinGapMs + next() % (MaxGapMs - MinGapMs + 1u); }

  livingeyes::BehaviorNoveltyTracker novelty_;
  uint32_t rng_ = 1, nextAt_ = 0;
  Action last_ = Action::None;
  bool started_ = false;
};
}  // namespace roboidle
