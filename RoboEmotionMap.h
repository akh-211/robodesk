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
  livingeyes::Expression strong = livingeyes::Expression::Normal;  // PD-5.1: used from strongFrom intensity upward
  float strongFrom = 2.f;                                          // >1 = never (default for rows without a strong look)
};
using livingeyes::Clip;
using livingeyes::Expression;
using livingeyes::SonicCue;
// Order = express_character enum order. Names are the tool/wire vocabulary; do not rename.
inline const Emotion* table(size_t& count) {
  static const Emotion emotions[] = {
      {"happy", Expression::Smile, true, Clip::Happy, true, SonicCue::Happy, Expression::Happy, .5f},  // level 1 = Smile, 2-3 = Happy
      {"curious", Expression::Wonder, true, Clip::Curious, true, SonicCue::Curious},
      {"shy", Expression::Shy, true, Clip::Shy, false, SonicCue::Giggle},
      {"love", Expression::Adoring, true, Clip::Love, true, SonicCue::Affection, Expression::Heart, .85f},
      {"sad", Expression::Sad, true, Clip::Sad, true, SonicCue::Sad, Expression::Crying, .85f},
      {"surprised", Expression::Startled, true, Clip::Surprise, true, SonicCue::Surprise, Expression::Shock, .85f},
      {"thinking", Expression::Thinking, true, Clip::Think, true, SonicCue::Thinking},
      {"agree", Expression::Agree, true, Clip::Agree, true, SonicCue::Success},
      {"disagree", Expression::Disagree, true, Clip::Disagree, true, SonicCue::Failure},
      {"wink", Expression::WinkLeft, true, Clip::Wink, true, SonicCue::Notification},
      {"laugh", Expression::Laugh, true, Clip::Giggle, true, SonicCue::Giggle},
      {"sleepy", Expression::Tired, true, Clip::Sleepy, true, SonicCue::Yawn},
      {"annoyed", Expression::Annoyed, true, Clip::Annoyed, false, SonicCue::Hmph},  // silent in the map (sulk fence); touch events request the one Hmph themselves
      {"dizzy", Expression::Nervous, false, Clip::Curious, true, SonicCue::Dizzy, Expression::Fail, .85f},  // spiral; X X when level 3
      {"excited", Expression::Excited, true, Clip::Laugh, true, SonicCue::Greeting},
      {"calm", Expression::Calm, true, Clip::Breathe, false, SonicCue::Happy},  // silent on purpose
      // PD-5.1 new emotions: each reuses clips/cues the map did not use yet
      {"bored", Expression::Bored, true, Clip::Scan, false, SonicCue::Yawn},
      {"proud", Expression::Proud, true, Clip::Hello, true, SonicCue::Success, Expression::Cool, .85f},
      {"anxious", Expression::Anxious, true, Clip::PeekLeft, true, SonicCue::Shake},
      {"relieved", Expression::Relieved, true, Clip::Wake, true, SonicCue::Sleepy},
      {"mischievous", Expression::Mischievous, true, Clip::PeekRight, true, SonicCue::Giggle},
      {"lonely", Expression::Lonely, true, Clip::Listen, false, SonicCue::Sad},
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
      {"bashful", "shy"}, {"worried", "anxious"}, {"playful", "mischievous"},
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
// Eye look for an emotion at a given (already clamped) intensity: strong variant when the row has one.
inline Expression expressionFor(const Emotion& e, float intensity) { return intensity >= e.strongFrom ? e.strong : e.expression; }
inline float clampFor(const Emotion& e, float intensity) {
  const float cap = !strcmp(e.name, "annoyed") ? .45f : 1.f;
  return intensity < 0 ? 0 : intensity > cap ? cap : intensity;
}
}  // namespace roboemotion
