#pragma once
#include <stdint.h>
#include <string.h>
#include <livingeyes/Character.h>
#include <livingeyes/SonicCharacter.h>

// PD-1.12 single emotion map (PRODUCT_DIRECTION section 4). express(), the express_character tool, CharacterMind
// mood names and "sulking" all resolve here. Level 1..3 sets size/volume/duration through one intensity.
namespace roboemotion {
struct Emotion {
  const char* name;
  livingeyes::Expression expression;
  bool hasClip;
  livingeyes::Clip clip;
  bool hasCue;
  livingeyes::SonicCue cue;
};
using livingeyes::Clip;
using livingeyes::Expression;
using livingeyes::SonicCue;
// Order = express_character enum order. Names are the tool/wire vocabulary; do not rename.
inline const Emotion* table(size_t& count) {
  static const Emotion emotions[] = {
      {"happy", Expression::Happy, true, Clip::Happy, true, SonicCue::Happy},
      {"curious", Expression::Curious, true, Clip::Curious, true, SonicCue::Curious},
      {"shy", Expression::Nervous, true, Clip::Shy, true, SonicCue::Giggle},
      {"love", Expression::Love, true, Clip::Love, true, SonicCue::Affection},
      {"sad", Expression::Sad, true, Clip::Sad, true, SonicCue::Sad},
      {"surprised", Expression::Surprised, true, Clip::Surprise, true, SonicCue::Surprise},
      {"thinking", Expression::Thinking, true, Clip::Think, true, SonicCue::Thinking},
      {"agree", Expression::Smile, true, Clip::Agree, true, SonicCue::Success},
      {"disagree", Expression::Normal, true, Clip::Disagree, true, SonicCue::Failure},
      {"wink", Expression::Smile, true, Clip::Wink, false, SonicCue::Happy},
      {"laugh", Expression::Happy, true, Clip::Giggle, true, SonicCue::Giggle},
      {"sleepy", Expression::Tired, true, Clip::Sleepy, true, SonicCue::Yawn},
      {"annoyed", Expression::Suspicious, true, Clip::Annoyed, false, SonicCue::Sad},  // "hmph" cue arrives with PD-1.8b
      {"dizzy", Expression::Nervous, false, Clip::Curious, true, SonicCue::Shake},     // spiral Nervous; no clip
      {"excited", Expression::Excited, true, Clip::Laugh, true, SonicCue::Giggle},
      {"calm", Expression::Normal, true, Clip::DoubleBlink, false, SonicCue::Happy},
  };
  count = sizeof(emotions) / sizeof(emotions[0]);
  return emotions;
}
// CharacterMind mood names (and legacy labels) -> canonical emotion name; nullptr when unknown.
inline const char* canonical(const char* name) {
  if (!name || !*name) return nullptr;
  static const char* const aliases[][2] = {
      {"sulking", "annoyed"}, {"subdued", "sad"}, {"affectionate", "love"}, {"restless", "curious"},
      {"cheerful", "happy"}, {"neutral", "calm"}, {"tired", "sleepy"},
  };
  for (const auto& a : aliases)
    if (!strcmp(name, a[0])) return a[1];
  size_t count = 0;
  const Emotion* emotions = table(count);
  for (size_t i = 0; i < count; ++i)
    if (!strcmp(name, emotions[i].name)) return emotions[i].name;
  return nullptr;
}
inline const Emotion* find(const char* name) {
  const char* key = canonical(name);
  if (!key) return nullptr;
  size_t count = 0;
  const Emotion* emotions = table(count);
  for (size_t i = 0; i < count; ++i)
    if (!strcmp(key, emotions[i].name)) return &emotions[i];
  return nullptr;
}
// Level 1..3 (clamped) -> eye intensity; levels outside the range fall back to 2.
inline float levelIntensity(int level) { return level == 1 ? .35f : level == 3 ? 1.f : .65f; }
// "sulking"/"annoyed" stay low-key by design (ethics fence: never guilt-tripping).
inline float clampFor(const Emotion& e, float intensity) {
  const float cap = !strcmp(e.name, "annoyed") ? .45f : 1.f;
  return intensity < 0 ? 0 : intensity > cap ? cap : intensity;
}
}  // namespace roboemotion
