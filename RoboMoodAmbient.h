#pragma once
#include <stdint.h>
#include <string.h>

// PD-6.1: CharacterMind state -> an occasional ambient emotion, so a lonely/bored/sleepy robot looks it.
// Pure policy: at most one emotion per 2-3 min, never the same one twice within 3 gaps.
namespace roboambient {
struct Input {
  uint32_t now = 0;
  bool allowed = false;  // awake, not busy/DND, proactive visual on
  bool presence = false;
  float energy = .7f, boredom = 0.f, socialNeed = 0.f, curiosity = 0.f, affection = 0.f, valence = .5f, trust = .5f;
};

class Picker {
 public:
  static constexpr uint32_t BaseGapMs = 120000u, JitterMs = 60000u;
  // Returns an emotion name from RoboEmotionMap, or nullptr when nothing is due.
  const char* update(const Input& in, uint32_t rnd) {
    if (!in.allowed) return nullptr;
    const uint32_t gap = BaseGapMs + rnd % JitterMs;
    if (started_ && uint32_t(in.now - lastAt_) < gap) return nullptr;
    const char* name = choose(in, rnd);
    if (!name) return nullptr;
    if (started_ && last_ == name && uint32_t(in.now - lastAt_) < gap * 3u) return nullptr;
    started_ = true;
    lastAt_ = in.now;
    last_ = name;
    return name;
  }

 private:
  // Candidates in priority order; the first one that is not the previous emotion wins, so saturated drives still vary.
  const char* choose(const Input& in, uint32_t rnd) const {
    const char* c[8];
    unsigned n = 0;
    if (in.energy < .3f) c[n++] = "sleepy";
    if (in.socialNeed > .5f && !in.presence) c[n++] = "lonely";
    if (in.boredom > .5f) c[n++] = "bored";
    if (in.curiosity > .72f) c[n++] = "curious";
    if (in.affection > .65f && in.valence > .6f) c[n++] = (rnd & 1u) ? "love" : "happy";
    if (in.trust < .35f && in.presence) c[n++] = "shy";  // lonely for long enough: a bit wary of the owner again
    if (in.valence > .7f && in.energy > .7f && (rnd % 4u) == 0u) c[n++] = "mischievous";
    for (unsigned i = 0; i < n; ++i) if (!last_ || strcmp(c[i], last_)) return c[i];
    return n ? c[0] : nullptr;
  }
  bool started_ = false;
  uint32_t lastAt_ = 0;
  const char* last_ = nullptr;
};

// PD-6.4: an emotion the character shows on request (Gemini express_character) nudges the mood a little, scaled by intensity.
inline void feedback(const char* e, float intensity, float& valence, float& curiosity, float& boredom) {
  valence = curiosity = boredom = 0.f;
  if (!e) return;
  const float k = (intensity < 0.f ? 0.f : (intensity > 1.f ? 1.f : intensity)) * .04f;
  auto is = [&](const char* n) { return !strcmp(e, n); };
  if (is("happy") || is("love") || is("laugh") || is("excited") || is("proud") || is("relieved") || is("mischievous") || is("wink") || is("agree")) { valence = k; boredom = -k; }
  else if (is("sad") || is("lonely") || is("anxious") || is("dizzy")) { valence = -k; }
  else if (is("annoyed") || is("disagree")) { valence = -k * .5f; }
  else if (is("curious") || is("thinking") || is("surprised")) { curiosity = k; boredom = -k; }
  else if (is("bored")) { boredom = k; }
}
}  // namespace roboambient
