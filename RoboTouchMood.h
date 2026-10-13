#pragma once
#include <stdint.h>

// PD-1.5 touch variety and tap-spam mood (PRODUCT_DIRECTION section 7). Pure logic, injected clock.
// The LivingEyes clip variant per interaction stays in the lib; this adds the SFX/intensity variant per touch kind,
// the >=5 taps in 3 s "annoyed" detector and a sulk that never outlasts 3 minutes.
namespace robotouch {
enum class Kind : uint8_t { Tap, DoubleTap, Pet, Reunion, Count };
enum class Mood : uint8_t { Normal, Annoyed, Sulking };
enum class Event : uint8_t { None, Annoyed, SulkStart, SulkEnd };

struct Variant {
  const char* emotion;  // RoboEmotionMap name
  float intensity;
  const char* cue;      // LivingEyes SonicCue name
  float strength;       // cue volume/strength
};
constexpr uint8_t VariantCount = 4;
inline const Variant& variant(Kind kind, uint8_t index) {
  static const Variant table[uint8_t(Kind::Count)][VariantCount] = {
      {{"happy", .55f, "happy", .70f}, {"surprised", .35f, "affection", .80f}, {"curious", .50f, "curious", .60f}, {"shy", .45f, "giggle", .55f}},
      {{"laugh", .70f, "giggle", .85f}, {"excited", .65f, "happy", .80f}, {"happy", .85f, "giggle", .70f}, {"wink", .60f, "happy", .65f}},
      {{"love", .70f, "affection", .60f}, {"love", .90f, "affection", .45f}, {"calm", .65f, "affection", .35f}, {"shy", .55f, "affection", .50f}},
      {{"love", 1.f, "greeting", .90f}, {"excited", 1.f, "greeting", .85f}, {"happy", 1.f, "giggle", .80f}, {"love", .90f, "happy", .90f}},
  };
  return table[uint8_t(kind) % uint8_t(Kind::Count)][index % VariantCount];
}

// Never repeats the previous variant of the same kind back to back.
class VariantPicker {
 public:
  uint8_t next(Kind kind, uint32_t random) {
    uint8_t& last = last_[uint8_t(kind) % uint8_t(Kind::Count)];
    uint8_t pick = uint8_t(random % VariantCount);
    if (pick == last) pick = uint8_t((pick + 1 + (random >> 8) % (VariantCount - 1)) % VariantCount);
    last = pick;
    return pick;
  }

 private:
  uint8_t last_[uint8_t(Kind::Count)] = {0xff, 0xff, 0xff, 0xff};
};

constexpr uint32_t ReunionMs = 2u * 3600u * 1000u;  // touched after >=2 h alone
inline bool reunion(uint32_t lastInteractionAt, uint32_t now) { return uint32_t(now - lastInteractionAt) >= ReunionMs; }

// Privacy hold (side 1.4 s) and the touch game always win over any touch mood reaction.
enum class Route : uint8_t { Privacy, Game, Mood };
inline Route route(bool privacyHoldReached, bool gameActive) {
  return privacyHoldReached ? Route::Privacy : gameActive ? Route::Game : Route::Mood;
}

class TouchMood {
 public:
  static constexpr uint32_t WindowMs = 3000, AnnoyedMs = 8000, SulkRepeatMs = 30000, SulkMaxMs = 180000;
  static constexpr uint8_t AnnoyedTaps = 5;

  // A head tap outside the touch game.
  Event tap(uint32_t now) {
    times_[head_] = now;
    head_ = uint8_t((head_ + 1) % Ring);
    if (count_ < Ring) ++count_;
    if (mood_ == Mood::Sulking) return Event::None;  // sulking ignores further poking (no escalation)
    if (burstArmed_ && tapsWithin(now) >= AnnoyedTaps) {
      burstArmed_ = false;
      const bool repeat = annoyedAt_ && uint32_t(now - annoyedAt_) <= SulkRepeatMs;
      annoyedAt_ = now;
      if (repeat) { mood_ = Mood::Sulking; sulkAt_ = now; return Event::SulkStart; }
      mood_ = Mood::Annoyed;
      return Event::Annoyed;
    }
    return Event::None;
  }
  // A patient long pet soothes a sulk early.
  Event pet(uint32_t now) {
    (void)now;
    if (mood_ == Mood::Sulking) { mood_ = Mood::Normal; annoyedAt_ = 0; return Event::SulkEnd; }
    if (mood_ == Mood::Annoyed) mood_ = Mood::Normal;
    return Event::None;
  }
  Event update(uint32_t now) {
    if (!burstArmed_ && tapsWithin(now) == 0) burstArmed_ = true;  // a new burst needs a calm window first
    if (mood_ == Mood::Annoyed && uint32_t(now - annoyedAt_) >= AnnoyedMs) mood_ = Mood::Normal;
    if (mood_ == Mood::Sulking && uint32_t(now - sulkAt_) >= SulkMaxMs) { mood_ = Mood::Normal; annoyedAt_ = 0; return Event::SulkEnd; }
    return Event::None;
  }
  Mood mood() const { return mood_; }
  uint32_t sulkingSince() const { return mood_ == Mood::Sulking ? sulkAt_ : 0; }

 private:
  static constexpr uint8_t Ring = 8;
  uint8_t tapsWithin(uint32_t now) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < count_; ++i)
      if (uint32_t(now - times_[i]) < WindowMs) ++n;
    return n;
  }
  uint32_t times_[Ring] = {};
  uint8_t head_ = 0, count_ = 0;
  bool burstArmed_ = true;
  Mood mood_ = Mood::Normal;
  uint32_t annoyedAt_ = 0, sulkAt_ = 0;
};
}  // namespace robotouch
