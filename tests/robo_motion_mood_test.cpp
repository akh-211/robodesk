#include "../RoboMotionMood.h"

#include <cassert>
#include <iostream>

using robomotion::Event;
using robomotion::MotionMood;

namespace {
constexpr float FallG = .22f, ShakeGyro = 2.8f;
Event still(MotionMood& m, uint32_t now, bool picked = false) { return m.sample(now, 1.f, 0.f, picked, FallG, ShakeGyro); }
Event shake(MotionMood& m, uint32_t now) { return m.sample(now, 1.f, 4.f, false, FallG, ShakeGyro); }

void repeatedShakeCounter(uint32_t t) {
  MotionMood m;
  assert(shake(m, t) == Event::Shake);
  assert(shake(m, t + 500) == Event::None);  // one shake gesture, cooldown
  assert(shake(m, t + 10000) == Event::Shake);
  assert(shake(m, t + 20000) == Event::Annoyed);  // third within 30 s
  assert(shake(m, t + 22000) == Event::Shake);    // counter restarted
  // Spread-out shakes never annoy.
  MotionMood calm;
  for (uint32_t i = 0; i < 6; ++i) assert(shake(calm, t + i * 31000u) == Event::Shake);
}

void fallThenSad() {
  MotionMood m;
  assert(m.sample(1000, .1f, 0.f, false, FallG, ShakeGyro) == Event::Fall);
  assert(m.sample(1100, .1f, 0.f, false, FallG, ShakeGyro) == Event::None);  // one fall
  assert(m.update(2000) == Event::None);
  assert(m.update(2600) == Event::FallSad && m.sad());
  assert(m.update(2600 + MotionMood::SadMs - 100) == Event::None);
  assert(m.update(2600 + MotionMood::SadMs) == Event::Recovered && !m.sad());
  // Petted: recovers much faster.
  MotionMood p;
  p.sample(50000, .1f, 0.f, false, FallG, ShakeGyro);
  assert(p.update(51600) == Event::FallSad);
  p.pet(52000);
  assert(p.update(52000 + MotionMood::PettedSadMs - 1) == Event::None);
  assert(p.update(52000 + MotionMood::PettedSadMs) == Event::Recovered);
  p.pet(60000);  // not sad: no effect
  assert(p.update(70000) == Event::None);
}

void putDownRelief() {
  MotionMood m;
  assert(still(m, 0, true) == Event::None);
  assert(still(m, 100, true) == Event::None);
  assert(still(m, 200, false) == Event::PutDown);
  assert(still(m, 300, false) == Event::None);
}
}  // namespace

int main() {
  repeatedShakeCounter(1000);
  repeatedShakeCounter(0xFFFFFFFFu - 15000u);
  fallThenSad();
  putDownRelief();
  std::cout << "PASS: repeated shake counter (3 in 30 s), fall shock->sad with faster recovery when petted, put-down relief\n";
  return 0;
}
