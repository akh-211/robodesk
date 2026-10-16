#pragma once
#include <stdint.h>
#include <livingeyes/BehaviorNovelty.h>

// PD-1.4 idle layer B (PRODUCT_DIRECTION section 6): a small action every 30-120 s (user feedback 2026-10-15: 15-40 s looked repetitive), only while awake, someone is
// around, nothing else runs and >=25 s after the last interaction. Never any of the last 3 actions again (when alternatives exist);
// LivingEyes' BehaviorNoveltyTracker also damps recently used ones.
namespace roboidle {
enum class Action : uint8_t { None, Scan, PeekLeft, PeekRight, GiggleShy, Yawn, DoubleBlink, Wink, EyeRoll, Nod, Confident, Sly, Count };

inline const char* actionName(Action a) {
  switch (a) {
    case Action::Scan: return "scan";
    case Action::PeekLeft: return "peek_left";
    case Action::PeekRight: return "peek_right";
    case Action::GiggleShy: return "giggle_shy";
    case Action::Yawn: return "yawn";
    case Action::DoubleBlink: return "double_blink";
    case Action::Wink: return "wink";
    case Action::EyeRoll: return "eye_roll";
    case Action::Nod: return "nod";
    case Action::Confident: return "confident";
    case Action::Sly: return "sly";
    default: return "none";
  }
}

struct Context {
  uint32_t now = 0;
  bool awake = true, presence = false, busy = false;
  uint32_t lastInteractionAt = 0;
  float energy = 1.f, boredom = 0.f;
};

class Picker {
 public:
  static constexpr uint32_t MinGapMs = 30000, MaxGapMs = 120000, AfterInteractionMs = 25000, AbsentExtraMs = 60000;
  static constexpr float LowEnergy = .35f;

  explicit Picker(uint32_t seed = 0x9e3779b9u) : rng_(seed ? seed : 1u) {}
  void seed(uint32_t seed) { rng_ = seed ? seed : 1u; }
  void reset(uint32_t now) { novelty_.reset(); last_ = Action::None; recent_[0] = recent_[1] = recent_[2] = Action::None; started_ = true; schedule(now); }
  Action last() const { return last_; }
  uint32_t nextAt() const { return nextAt_; }

  Action update(const Context& c) {
    if (!started_) { started_ = true; schedule(c.now); }
    const bool eligible = c.awake && !c.busy && uint32_t(c.now - c.lastInteractionAt) >= AfterInteractionMs;
    // Nobody detected by the PIR: still alive, but accents come a full minute later than when someone is around.
    const bool settled = c.presence || int32_t(c.now - (nextAt_ + AbsentExtraMs)) >= 0;
    if (!eligible) {
      // Keep the gap honest: the next beat comes a full gap after things settle, not immediately.
      if (int32_t(nextAt_ - c.now) < int32_t(MinGapMs)) nextAt_ = c.now + MinGapMs;
      return Action::None;
    }
    if (int32_t(c.now - nextAt_) < 0 || !settled) return Action::None;
    float best = -1.f;
    Action pick = Action::None;
    for (uint8_t i = 1; i < uint8_t(Action::Count); ++i) {
      const Action a = Action(i);
      if (a == last_) continue;
      if (recentlyUsed(a)) continue;
      const float utility = weight(a, c.energy, c.boredom);
      if (utility <= 0) continue;
      const float jitter = .75f + .5f * float(next() % 1000u) / 1000.f;
      const float score = novelty_.score(uint16_t(i), utility) * jitter;
      if (score > best) { best = score; pick = a; }
    }
    if (pick == Action::None) {  // everything else was excluded: relax to just 'not the last one'
      for (uint8_t i = 1; i < uint8_t(Action::Count); ++i) {
        const Action a = Action(i);
        if (a == last_) continue;
        const float utility = weight(a, c.energy, c.boredom);
        if (utility > best) { best = utility; pick = a; }
      }
      if (pick == Action::None) return pick;
    }
    novelty_.commit(uint16_t(pick));
    recent_[2] = recent_[1]; recent_[1] = recent_[0]; recent_[0] = pick;
    last_ = pick;
    schedule(c.now);
    return pick;
  }

 private:
  static float weight(Action a, float energy, float boredom) {
    switch (a) {
      case Action::Scan: return 1.f;
      case Action::PeekLeft:
      case Action::PeekRight: return .7f;
      case Action::GiggleShy: return energy >= .5f ? .75f : .2f;
      case Action::Yawn: return energy < LowEnergy ? 1.2f : 0.f;
      case Action::DoubleBlink: return .8f;
      case Action::Wink: return energy >= .5f ? .75f : .25f;
      case Action::EyeRoll: return .3f + .4f * (boredom > 1.f ? 1.f : boredom);  // PD-6.3: bored robots roll their eyes
      case Action::Nod: return .8f;
      case Action::Confident: return energy >= .6f ? .75f : 0.f;
      case Action::Sly: return energy >= .5f ? .7f : 0.f;
      default: return 0.f;
    }
  }
  uint32_t next() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  bool recentlyUsed(Action a) const { return a == recent_[0] || a == recent_[1] || a == recent_[2]; }
  // Irregular timing: mostly 30-120 s, one in five followed by an extra quiet spell so it never feels like a metronome.
  void schedule(uint32_t now) { nextAt_ = now + MinGapMs + next() % (MaxGapMs - MinGapMs + 1u); if (next() % 5u == 0u) nextAt_ += 30000u + next() % 60001u; }

  livingeyes::BehaviorNoveltyTracker novelty_;
  uint32_t rng_ = 1, nextAt_ = 0;
  Action last_ = Action::None, recent_[3] = {Action::None, Action::None, Action::None};
  bool started_ = false;
};
}  // namespace roboidle
