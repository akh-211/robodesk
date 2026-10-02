#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace companion {

// Opt-in, aggregate-only learning. Payload format is independent from user
// memory/reminders and contains no timestamps, audio, or presence history.
class PreferenceLearner {
 public:
  static constexpr uint8_t ActivitySlots = 10;  // LivingEyes LifeActivity IDs 0..9
  static constexpr uint8_t TimeBuckets = 8;     // Eight coarse 3-hour periods
  static constexpr uint8_t MaxEvidence = 15;
  static constexpr size_t PayloadSize = 40;

  bool enabled() const { return enabled_; }
  bool dirty() const { return dirty_; }
  void markSaved() { dirty_ = false; }

  bool setEnabled(bool enabled) {
    if (enabled_ == enabled) return false;
    enabled_ = enabled;
    dirty_ = true;
    return true;
  }

  void resetLearned() {
    memset(favorites_, 0, sizeof(favorites_));
    memset(skips_, 0, sizeof(skips_));
    memset(timeBuckets_, 0, sizeof(timeBuckets_));
    dirty_ = true;
  }

  bool recordFeedback(uint8_t activity, bool favorite, uint16_t minuteOfDay, bool clockValid = true) {
    if (!enabled_ || activity == 0 || activity >= ActivitySlots || (clockValid && minuteOfDay >= 1440)) return false;
    uint8_t& count = favorite ? favorites_[activity] : skips_[activity];
    bool changed = increment(count);
    if (clockValid) changed = increment(timeBuckets_[minuteOfDay / 180]) || changed;
    dirty_ = dirty_ || changed;
    return changed;
  }

  int16_t score(uint8_t activity) const {
    if (activity == 0 || activity >= ActivitySlots) return 0;
    return int16_t(favorites_[activity]) - int16_t(skips_[activity]);
  }
  uint8_t scheduleProbability(uint8_t activity, uint8_t routineEvidence) const {
    const int16_t value = score(activity);
    if (!enabled_ || value <= 0 || routineEvidence < 3) return 0;
    return uint8_t(value > 5 ? 5 : value);
  }
  uint8_t favorites(uint8_t activity) const { return activity < ActivitySlots ? favorites_[activity] : 0; }
  uint8_t skips(uint8_t activity) const { return activity < ActivitySlots ? skips_[activity] : 0; }
  uint8_t timeBucket(uint8_t bucket) const { return bucket < TimeBuckets ? timeBuckets_[bucket] : 0; }
  uint16_t favoriteTotal() const { return total(favorites_); }
  uint16_t skipTotal() const { return total(skips_); }
  uint16_t routineTotal() const { return total(timeBuckets_); }

  bool encode(uint8_t* out, size_t capacity) const {
    if (!out || capacity < PayloadSize) return false;
    memset(out, 0, PayloadSize);
    out[0] = 'R'; out[1] = 'L'; out[2] = 'P'; out[3] = 'F';
    out[4] = 1;  // Format version
    out[5] = enabled_ ? 1 : 0;
    out[6] = ActivitySlots;
    out[7] = TimeBuckets;
    memcpy(out + 8, favorites_, ActivitySlots);
    memcpy(out + 8 + ActivitySlots, skips_, ActivitySlots);
    memcpy(out + 8 + ActivitySlots * 2, timeBuckets_, TimeBuckets);
    const uint32_t crc = checksum(out, PayloadSize - 4);
    out[PayloadSize - 4] = uint8_t(crc);
    out[PayloadSize - 3] = uint8_t(crc >> 8);
    out[PayloadSize - 2] = uint8_t(crc >> 16);
    out[PayloadSize - 1] = uint8_t(crc >> 24);
    return true;
  }

  bool restore(const uint8_t* in, size_t size) {
    if (!in || size != PayloadSize || in[0] != 'R' || in[1] != 'L' ||
        in[2] != 'P' || in[3] != 'F' || in[4] != 1 || in[5] > 1 ||
        in[6] != ActivitySlots || in[7] != TimeBuckets) return false;
    const uint32_t got = uint32_t(in[PayloadSize - 4]) |
                         uint32_t(in[PayloadSize - 3]) << 8 |
                         uint32_t(in[PayloadSize - 2]) << 16 |
                         uint32_t(in[PayloadSize - 1]) << 24;
    if (checksum(in, PayloadSize - 4) != got) return false;
    for (uint8_t i = 0; i < ActivitySlots; ++i)
      if (in[8 + i] > MaxEvidence || in[8 + ActivitySlots + i] > MaxEvidence) return false;
    for (uint8_t i = 0; i < TimeBuckets; ++i)
      if (in[8 + ActivitySlots * 2 + i] > MaxEvidence) return false;
    enabled_ = in[5] != 0;
    memcpy(favorites_, in + 8, ActivitySlots);
    memcpy(skips_, in + 8 + ActivitySlots, ActivitySlots);
    memcpy(timeBuckets_, in + 8 + ActivitySlots * 2, TimeBuckets);
    dirty_ = false;
    return true;
  }

 private:
  static bool increment(uint8_t& value) {
    if (value >= MaxEvidence) return false;
    ++value;
    return true;
  }
  template <size_t N>
  static uint16_t total(const uint8_t (&values)[N]) {
    uint16_t sum = 0;
    for (size_t i = 0; i < N; ++i) sum = uint16_t(sum + values[i]);
    return sum;
  }
  static uint32_t checksum(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    while (size--) {
      crc ^= *data++;
      for (uint8_t bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
  }

  bool enabled_ = false;
  bool dirty_ = false;
  uint8_t favorites_[ActivitySlots]{};
  uint8_t skips_[ActivitySlots]{};
  uint8_t timeBuckets_[TimeBuckets]{};
};

template <class PreferencesType>
class PreferenceLearnerStore {
 public:
  explicit PreferenceLearnerStore(PreferencesType& preferences) : preferences_(preferences) {}

  bool load(PreferenceLearner& learner) {
    if (!preferences_.begin("rdlearn", true)) return false;
    uint8_t payload[PreferenceLearner::PayloadSize]{};
    const size_t length = preferences_.getBytesLength("payload");
    const size_t read = length == sizeof(payload)
                          ? preferences_.getBytes("payload", payload, sizeof(payload)) : 0;
    preferences_.end();
    return read == sizeof(payload) && learner.restore(payload, sizeof(payload));
  }

  bool save(PreferenceLearner& learner) {
    if (!learner.dirty()) return true;
    uint8_t payload[PreferenceLearner::PayloadSize]{};
    if (!learner.encode(payload, sizeof(payload)) || !preferences_.begin("rdlearn", false)) return false;
    const size_t written = preferences_.putBytes("payload", payload, sizeof(payload));
    preferences_.end();
    if (written != sizeof(payload)) return false;
    learner.markSaved();
    return true;
  }

  bool clear() {
    if (!preferences_.begin("rdlearn", false)) return false;
    const bool ok = preferences_.clear();
    preferences_.end();
    return ok;
  }

 private:
  PreferencesType& preferences_;
};

}  // namespace companion
