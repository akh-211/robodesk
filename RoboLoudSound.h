#pragma once
#include <stdint.h>

// PD-1.13 sudden loud sound (clap, bang) from the mic frame RMS. Only short transients count: energy must jump far
// above the room baseline and fall back within a few frames, so speech and music do not trigger. Off while the robot
// converses, the mic is muted, or music has been heard recently. No audio is stored; one float per frame.
namespace roboloud {
class Detector {
 public:
  static constexpr float JumpRatio = 8.f, MinRms = 9000.f, DecayRatio = .35f, BaselineAlpha = .02f, MinBaseline = 50.f;
  static constexpr uint8_t MaxTransientFrames = 3;  // 40 ms frames: a clap is over in <=120 ms
  static constexpr uint8_t MusicWindow = 50, MusicBusyFrames = 30;  // ~2 s of frames, 60 % busy = music
  static constexpr uint32_t CooldownMs = 8000, MusicHoldMs = 5000;

  // Returns true once per loud transient (on its decay).
  bool frame(uint32_t now, float rms, bool conversation, bool muted) {
    if (!(rms >= 0)) return false;  // NaN guard
    if (conversation || muted) { loudFrames_ = 0; return false; }
    // Music/steady noise: many frames well above the baseline.
    const bool busy = rms > baseline_ * 2.5f || rms > MinRms * .25f;
    window_ = uint64_t(window_ << 1) | (busy ? 1u : 0u);
    if (popcount(window_ & mask()) >= MusicBusyFrames) musicAt_ = now, musicSeen_ = true;
    const bool music = musicSeen_ && uint32_t(now - musicAt_) < MusicHoldMs;
    const float threshold = baseline_ * JumpRatio > MinRms ? baseline_ * JumpRatio : MinRms;
    bool fire = false;
    if (loudFrames_) {
      if (rms > peak_) peak_ = rms;
      if (rms < peak_ * DecayRatio) {
        fire = !music && (!firedAt_ || uint32_t(now - firedAt_) >= CooldownMs);
        loudFrames_ = 0;
      } else if (++loudFrames_ > MaxTransientFrames) {
        loudFrames_ = 0;  // too long: speech, music or a held noise
        sustained_ = true;
      }
    } else if (rms >= threshold && !sustained_ && (window_ & 0xEu) == 0) {  // must rise out of a quiet room
      loudFrames_ = 1;
      peak_ = rms;
    }
    if (sustained_ && rms < threshold) sustained_ = false;
    if (!loudFrames_ && !sustained_) baseline_ += (rms - baseline_) * BaselineAlpha;
    if (baseline_ < MinBaseline) baseline_ = MinBaseline;
    if (fire) { firedAt_ = now; if (!firedAt_) firedAt_ = 1; }
    return fire;
  }
  float baseline() const { return baseline_; }
  bool musicRecently(uint32_t now) const { return musicSeen_ && uint32_t(now - musicAt_) < MusicHoldMs; }

 private:
  static uint64_t mask() { return MusicWindow >= 64 ? ~uint64_t(0) : ((uint64_t(1) << MusicWindow) - 1u); }
  static uint8_t popcount(uint64_t v) { uint8_t n = 0; while (v) { v &= v - 1u; ++n; } return n; }
  float baseline_ = 300.f, peak_ = 0;
  uint8_t loudFrames_ = 0;
  bool sustained_ = false, musicSeen_ = false;
  uint32_t firedAt_ = 0, musicAt_ = 0;
  uint64_t window_ = 0;
};
}  // namespace roboloud
