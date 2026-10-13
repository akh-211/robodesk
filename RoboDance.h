#pragma once
#include <stdint.h>

// PD-3.2 dance along with music: a steady beat from the mic frame RMS (one float per 40 ms frame, no audio kept).
// Starts after 8 onsets with a consistent tempo (60-200 BPM), only while not conversing and not muted; stops after
// 4 s without a beat, when blocked, or after 60 s, then rests 5 minutes so it never becomes a party trick on loop.
namespace robodance {
enum class Event : uint8_t { None, Start, Beat, Stop };

class BeatTracker {
 public:
  static constexpr float OnsetRatio = 1.6f, MinRms = 600.f, AvgAlpha = .08f, Tolerance = .15f;
  static constexpr uint32_t MinIntervalMs = 300, MaxIntervalMs = 1000, StopSilenceMs = 4000, MaxDanceMs = 60000,
                            CooldownMs = 300000;
  static constexpr uint8_t StartBeats = 8;

  Event frame(uint32_t now, float rms, bool blocked) {
    if (!(rms >= 0)) rms = 0;  // NaN guard
    const bool loud = rms > avg_ * OnsetRatio && rms > MinRms;
    const bool onset = loud && !wasLoud_ && (!onsetAt_ || uint32_t(now - onsetAt_) >= MinIntervalMs * 4u / 5u);
    wasLoud_ = loud;
    avg_ += (rms - avg_) * AvgAlpha;
    if (dancing_) {
      if (blocked || uint32_t(now - beatAt_) >= StopSilenceMs || uint32_t(now - startedAt_) >= MaxDanceMs) return stop(now);
      if (onset) { note(now); beatAt_ = now; return Event::Beat; }
      return Event::None;
    }
    if (blocked) { count_ = 0; onsetAt_ = 0; return Event::None; }
    if (!onset) return Event::None;
    note(now);
    if (count_ + 1u < StartBeats || !steady() || (stoppedAt_ && uint32_t(now - stoppedAt_) < CooldownMs)) return Event::None;
    dancing_ = true;
    startedAt_ = beatAt_ = now;
    return Event::Start;
  }
  bool dancing() const { return dancing_; }
  uint32_t intervalMs() const { return mean(); }

 private:
  static constexpr uint8_t Ring = StartBeats - 1;
  Event stop(uint32_t now) {
    dancing_ = false;
    stoppedAt_ = now ? now : 1;
    count_ = 0;
    onsetAt_ = 0;
    return Event::Stop;
  }
  void note(uint32_t now) {
    if (onsetAt_) {
      const uint32_t gap = now - onsetAt_;
      if (gap < MinIntervalMs || gap > MaxIntervalMs) count_ = 0;  // a pause or a double hit restarts the count
      else { intervals_[head_] = gap; head_ = uint8_t((head_ + 1) % Ring); if (count_ < Ring) ++count_; }
    }
    onsetAt_ = now;
  }
  uint32_t mean() const {
    if (!count_) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < count_; ++i) sum += intervals_[i];
    return sum / count_;
  }
  bool steady() const {
    const uint32_t m = mean();
    for (uint8_t i = 0; i < count_; ++i) {
      const uint32_t d = intervals_[i] > m ? intervals_[i] - m : m - intervals_[i];
      if (float(d) > float(m) * Tolerance) return false;
    }
    return true;
  }
  float avg_ = 300.f;
  bool wasLoud_ = false, dancing_ = false;
  uint32_t onsetAt_ = 0, beatAt_ = 0, startedAt_ = 0, stoppedAt_ = 0;
  uint32_t intervals_[Ring] = {};
  uint8_t head_ = 0, count_ = 0;
};
}  // namespace robodance
