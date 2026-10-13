#include "../RoboOledProtection.h"

#include <cassert>
#include <iostream>

using robooled::Plan;
using robosleep::State;

int main() {
  // Day/night/drowsy/sleep contrast and panel power.
  assert(robooled::plan(State::Awake, false, 0, false).contrast == robooled::ContrastDay);
  assert(robooled::plan(State::Awake, true, 0, false).contrast == robooled::ContrastNight);
  assert(robooled::plan(State::Waking, true, 0, false).contrast == robooled::ContrastNight);
  assert(robooled::plan(State::Drowsy, false, 0, false).contrast == robooled::ContrastDrowsy);
  const Plan light = robooled::plan(State::LightSleep, true, 0, false);
  assert(light.on && light.contrast == robooled::ContrastSleep);
  assert(!robooled::plan(State::DeepSleep, false, 0, false).on);
  assert(robooled::ContrastDay > robooled::ContrastNight && robooled::ContrastNight > robooled::ContrastDrowsy &&
         robooled::ContrastDrowsy > robooled::ContrastSleep);
  // Pairing/notification/passkey overlays always light the panel and stay readable.
  const Plan overlay = robooled::plan(State::DeepSleep, true, 0, true);
  assert(overlay.on && overlay.contrast == robooled::ContrastOverlay);
  assert(robooled::plan(State::Awake, false, 0, true).contrast == robooled::ContrastDay);  // never dimmer than awake

  // Eye shift: 1-2 px, changes every few minutes, mean zero over a cycle.
  int sum = 0, changes = 0, previous = robooled::eyeShift(0);
  for (uint32_t m = 0; m < 8 * robooled::ShiftStepMinutes; ++m) {
    const int s = robooled::eyeShift(m);
    assert(s >= -2 && s <= 2);
    if (m % robooled::ShiftStepMinutes == 0) sum += s;
    if (s != previous) ++changes;
    previous = s;
  }
  assert(sum == 0 && changes == 7);
  assert(robooled::eyeShift(0) == robooled::eyeShift(0xFFFFFFFFu / robooled::ShiftStepMinutes / 8u * 8u * robooled::ShiftStepMinutes));

  // Applier: commands only on change; panel back on => full frame.
  robooled::Applier applier;
  auto s = applier.next(robooled::plan(State::Awake, false, 0, false));
  assert(s.sendContrast && s.sendPower && s.fullFrame && s.layout);
  s = applier.next(robooled::plan(State::Awake, false, 1, false));
  assert(!s.sendContrast && !s.sendPower && !s.fullFrame && !s.layout);
  s = applier.next(robooled::plan(State::Awake, true, 1, false));
  assert(s.sendContrast && !s.sendPower && !s.layout);
  s = applier.next(robooled::plan(State::DeepSleep, true, 1, false));
  assert(s.sendContrast && s.sendPower && !s.fullFrame);
  s = applier.next(robooled::plan(State::DeepSleep, true, 3, false));
  assert(!s.sendPower && s.layout);
  s = applier.next(robooled::plan(State::DeepSleep, true, 3, true));  // passkey arrives while the panel is off
  assert(s.sendPower && s.fullFrame && s.sendContrast);
  applier.forget();
  assert(applier.next(robooled::plan(State::DeepSleep, true, 3, true)).fullFrame);
  std::cout << "PASS: OLED contrast/power plan per sleep state, overlay wake, 1-2 px shift, change-only commands and full frame after power on\n";
  return 0;
}
