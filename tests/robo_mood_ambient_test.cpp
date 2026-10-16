#include "../RoboMoodAmbient.h"

#include <cassert>
#include <cstring>
#include <iostream>

using roboambient::Input;
using roboambient::Picker;

namespace {
Input base(uint32_t now) { Input i; i.now = now; i.allowed = true; i.presence = true; return i; }

void gatedAndQuietByDefault() {
  Picker p;
  assert(!p.update(base(1000), 1));  // neutral mood -> nothing
  Input i = base(1000); i.boredom = .9f; i.allowed = false;
  assert(!p.update(i, 1));  // not allowed (busy/asleep/DND)
}

void moodMapsToEmotion() {
  Input i = base(500000);
  { Picker p; i.energy = .2f; assert(!strcmp(p.update(i, 3), "sleepy")); }
  { Picker p; Input j = base(500000); j.socialNeed = .8f; j.presence = false; assert(!strcmp(p.update(j, 3), "lonely")); }
  { Picker p; Input j = base(500000); j.socialNeed = .8f; j.boredom = .1f; assert(!p.update(j, 3)); }  // social need with owner present: not lonely
  { Picker p; Input j = base(500000); j.boredom = .8f; assert(!strcmp(p.update(j, 3), "bored")); }
  { Picker p; Input j = base(500000); j.trust = .2f; assert(!strcmp(p.update(j, 3), "shy")); }
  { Picker p; Input j = base(500000); j.curiosity = .9f; assert(!strcmp(p.update(j, 3), "curious")); }
}

void spacingAndNoRepeat() {
  Picker p;
  Input i = base(0); i.boredom = .9f;
  uint32_t last = 0; unsigned picks = 0;
  for (uint32_t t = 0; t < 3u * 3600u * 1000u; t += 1000u) {
    i.now = t;
    if (p.update(i, t * 2654435761u)) {
      if (picks) assert(t - last >= Picker::BaseGapMs * 3u);  // same emotion only after 3 gaps
      last = t; ++picks;
    }
  }
  assert(picks >= 20 && picks <= 40);  // ~1 per 6-9 min over 3 h
}

void millisWrap() {
  Picker p;
  Input i = base(0xFFFFFF00u); i.boredom = .9f;
  assert(p.update(i, 1));
  i.now = 0xFFFFFF00u + 1000u;
  assert(!p.update(i, 1));  // gap respected across wrap
  i.now = 0xFFFFFF00u + 400000u;  // wrapped
  i.boredom = .9f; i.curiosity = .9f;
  assert(p.update(i, 1));
}
void feedbackNudges() {
  float v, c, b;
  roboambient::feedback("happy", 1.f, v, c, b); assert(v > 0 && b < 0 && c == 0);
  roboambient::feedback("sad", .5f, v, c, b); assert(v < 0 && b == 0);
  roboambient::feedback("bored", 1.f, v, c, b); assert(b > 0 && v == 0);
  roboambient::feedback("curious", 9.f, v, c, b); assert(c > 0 && c <= .04f + 1e-6f);  // intensity clamped
  roboambient::feedback("nope", 1.f, v, c, b); assert(v == 0 && c == 0 && b == 0);
  roboambient::feedback(nullptr, 1.f, v, c, b); assert(v == 0);
}
}  // namespace

int main() {
  gatedAndQuietByDefault();
  moodMapsToEmotion();
  spacingAndNoRepeat();
  millisWrap();
  feedbackNudges();
  std::cout << "robo_mood_ambient_test OK\n";
}
