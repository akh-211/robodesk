#include "../RoboPersonalityPreset.h"

#include <cassert>
#include <iostream>

using robopreset::Preset;

int main() {
  assert(robopreset::clampPreset(0) == Preset::Kalem && robopreset::clampPreset(2) == Preset::Usil && robopreset::clampPreset(9) == Preset::Ceria);
  // Mirrors PersonalityPresets.kt (app moves these two knobs).
  for (uint8_t p = 0; p < 3; ++p) assert(robopreset::characterIntensity(Preset(p)) == p && robopreset::sonicFrequency(Preset(p)) == p);
  livingeyes::FaceLifeProfile base;
  livingeyes::FaceLifeProfile kalem = base, ceria = base, usil = base;
  robopreset::applyFace(Preset::Kalem, kalem);
  robopreset::applyFace(Preset::Ceria, ceria);
  robopreset::applyFace(Preset::Usil, usil);
  assert(kalem.microMotion < ceria.microMotion && ceria.microMotion < usil.microMotion && ceria.microMotion == base.microMotion);
  assert(usil.microMotion <= 1.f && kalem.yRange < usil.yRange);
  livingeyes::PersonalityProfile traits;
  const auto k = robopreset::applyTraits(Preset::Kalem, traits), c = robopreset::applyTraits(Preset::Ceria, traits), u = robopreset::applyTraits(Preset::Usil, traits);
  assert(k.playfulness < c.playfulness && c.playfulness < u.playfulness && k.patience > u.patience);
  assert(c.playfulness == traits.playfulness && c.patience == traits.patience);
  livingeyes::PersonalityProfile extreme;
  extreme.playfulness = .95f;
  assert(robopreset::applyTraits(Preset::Usil, extreme).playfulness <= 1.f);
  std::cout << "PASS: personality presets map intensity/frequency, face liveliness and bounded trait offsets\n";
  return 0;
}
