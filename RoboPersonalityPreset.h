#pragma once
#include <stdint.h>
#include <livingeyes/FaceLife.h>
#include <livingeyes/Personality.h>

// PD-2.3 personality presets (Kalem/Ceria/Usil). The app moves characterIntensity and sonicFrequency with the preset
// (PersonalityPresets.kt); the robot applies what only it owns: face-life liveliness and the starting trait offsets.
namespace robopreset {
enum class Preset : uint8_t { Kalem = 0, Ceria = 1, Usil = 2, Count };
constexpr uint8_t Default = uint8_t(Preset::Ceria);
inline Preset clampPreset(uint8_t v) { return v < uint8_t(Preset::Count) ? Preset(v) : Preset::Ceria; }
inline uint8_t characterIntensity(Preset p) { return uint8_t(p); }  // mirrors PersonalityPresets.kt
inline uint8_t sonicFrequency(Preset p) { return uint8_t(p); }

// Scales the user's face-life profile; Ceria leaves it untouched.
inline void applyFace(Preset p, livingeyes::FaceLifeProfile& f) {
  const float k = p == Preset::Kalem ? .7f : p == Preset::Usil ? 1.25f : 1.f;
  f.microMotion *= k;
  f.yRange *= k;
  f.spacingRange *= k;
  f.clampToBounds();
}
// Starting-trait offsets on top of the evolved profile (bounded, so evolution still shapes the character).
inline livingeyes::PersonalityProfile applyTraits(Preset p, livingeyes::PersonalityProfile v) {
  const float s = p == Preset::Kalem ? -1.f : p == Preset::Usil ? 1.f : 0.f;
  v.playfulness += .18f * s;
  v.reactivity += .15f * s;
  v.expressiveness += .15f * s;
  v.patience -= .15f * s;
  v.energyBias += .10f * s;
  v.clampToBounds();
  return v;
}
}  // namespace robopreset
