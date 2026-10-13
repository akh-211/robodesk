#include "../RoboSulkFace.h"
#include "../RoboEmotionMap.h"
#include "../RoboTouchMood.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using robosulk::SulkFace;

int main() {
  SulkFace face;
  face.start(1000, 7);
  robosulk::Gaze g = face.gaze(1000);
  assert(g.update && std::fabs(g.x) == SulkFace::AwayX);  // turns away at once
  const float side = g.x;
  unsigned glances = 0, aways = 1;
  uint32_t lastGlanceAt = 0;
  for (uint32_t t = 1000; t < 1000 + 60000; t += 50) {
    const robosulk::Gaze s = face.gaze(t);
    if (!s.update) continue;
    if (s.x == 0) {
      ++glances;
      assert(s.holdMs == SulkFace::GlanceMs);
      if (lastGlanceAt) assert(t - lastGlanceAt >= SulkFace::GlanceEveryMinMs);
      lastGlanceAt = t;
    } else {
      ++aways;
      assert(s.x == side);  // always the same side: reads as "turned away", not as looking around
    }
  }
  assert(glances >= 5 && glances <= 9 && aways >= glances);
  // Recovery is gradual: shy, then happy.
  face.stop(70000);
  assert(!face.active() && !face.gaze(70000).update);
  assert(!strcmp(face.recovery(70000), "shy"));
  assert(face.recovery(70000 + SulkFace::RecoverHappyMs - 1) == nullptr);
  assert(!strcmp(face.recovery(70000 + SulkFace::RecoverHappyMs), "happy"));
  assert(face.recovery(90000) == nullptr);
  // Ethics fence: a sulk blocks nothing the owner relies on, has no sound of its own and stays low-key.
  for (uint8_t d = 0; d < uint8_t(robosulk::Duty::Count); ++d) assert(!robosulk::sulkBlocks(robosulk::Duty(d)));
  const roboemotion::Emotion* sulk = roboemotion::find("sulking");
  assert(sulk && !sulk->hasCue && sulk->expression == livingeyes::Expression::Suspicious);
  assert(roboemotion::clampFor(*sulk, 1.f) <= .45f);
  assert(robotouch::TouchMood::SulkMaxMs <= 180000u);
  std::cout << "PASS: sulk face turns away with glances back, shy->happy recovery, ethics fence (no blocking, no sound, low intensity, <=3 min)\n";
  return 0;
}
