#include "../RoboTouchMood.h"

#include <cassert>
#include <cstring>
#include <iostream>

using robotouch::Event;
using robotouch::Kind;
using robotouch::Mood;
using robotouch::TouchMood;

namespace {
Event taps(TouchMood& m, uint32_t& now, unsigned n, uint32_t gap) {
  Event last = Event::None;
  for (unsigned i = 0; i < n; ++i) {
    now += gap;
    m.update(now);
    const Event e = m.tap(now);
    if (e != Event::None) last = e;
  }
  return last;
}
void idle(TouchMood& m, uint32_t& now, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 100) { now += 100; m.update(now); }
}

void rapidTapDetector(uint32_t start) {
  TouchMood m;
  uint32_t now = start;
  assert(taps(m, now, 4, 500) == Event::None);       // 4 taps in 2 s: fine
  idle(m, now, 4000);
  assert(taps(m, now, 5, 900) == Event::None);       // 5 taps but spread over 4.5 s
  idle(m, now, 4000);
  assert(taps(m, now, 5, 500) == Event::Annoyed);    // 5 taps within 3 s
  assert(m.mood() == Mood::Annoyed);
  assert(taps(m, now, 3, 300) == Event::None);       // same burst does not fire twice
  idle(m, now, TouchMood::AnnoyedMs + 3000);
  assert(m.mood() == Mood::Normal);                  // annoyance fades on its own
}

void sulkIsBoundedAndSoothable() {
  TouchMood m;
  uint32_t now = 1000;
  assert(taps(m, now, 5, 400) == Event::Annoyed);
  idle(m, now, 4000);
  assert(taps(m, now, 5, 400) == Event::SulkStart);  // it kept going within 30 s
  assert(m.mood() == Mood::Sulking && m.sulkingSince());
  assert(taps(m, now, 10, 200) == Event::None);      // no escalation while sulking
  // Left alone: ends by itself by 3 minutes.
  Event ended = Event::None;
  for (uint32_t t = 0; t <= TouchMood::SulkMaxMs; t += 100) { now += 100; if (m.update(now) == Event::SulkEnd) ended = Event::SulkEnd; }
  assert(ended == Event::SulkEnd && m.mood() == Mood::Normal);
  // A patient pet soothes early.
  idle(m, now, 4000);
  assert(taps(m, now, 5, 400) == Event::Annoyed);
  idle(m, now, 4000);
  assert(taps(m, now, 5, 400) == Event::SulkStart);
  idle(m, now, 20000);
  assert(m.pet(now) == Event::SulkEnd && m.mood() == Mood::Normal);
  // A burst long after the last annoyance is only annoyance again, not a sulk.
  idle(m, now, 40000);
  assert(taps(m, now, 5, 400) == Event::Annoyed);
}

void variantsNeverRepeatBackToBack() {
  robotouch::VariantPicker picker;
  for (uint8_t k = 0; k < uint8_t(Kind::Count); ++k) {
    uint8_t previous = 0xff;
    unsigned seen[robotouch::VariantCount] = {};
    uint32_t r = 12345u + k;
    for (unsigned i = 0; i < 200; ++i) {
      r = r * 1103515245u + 12345u;
      const uint8_t v = picker.next(Kind(k), r);
      assert(v < robotouch::VariantCount && v != previous);
      previous = v;
      ++seen[v];
      const auto& variant = robotouch::variant(Kind(k), v);
      assert(variant.emotion && variant.cue && variant.intensity > 0 && variant.intensity <= 1 && variant.strength > 0 && variant.strength <= 1);
    }
    for (unsigned v = 0; v < robotouch::VariantCount; ++v) assert(seen[v] > 0);  // 3-5 variants each, all used
  }
  // Variants of one kind differ in clip/intensity/cue.
  for (uint8_t k = 0; k < uint8_t(Kind::Count); ++k)
    for (uint8_t a = 0; a < robotouch::VariantCount; ++a)
      for (uint8_t b = uint8_t(a + 1); b < robotouch::VariantCount; ++b) {
        const auto& x = robotouch::variant(Kind(k), a);
        const auto& y = robotouch::variant(Kind(k), b);
        assert(strcmp(x.emotion, y.emotion) || x.intensity != y.intensity || strcmp(x.cue, y.cue) || x.strength != y.strength);
      }
}

void privacyAndGameFirst() {
  assert(robotouch::route(true, true) == robotouch::Route::Privacy);
  assert(robotouch::route(true, false) == robotouch::Route::Privacy);
  assert(robotouch::route(false, true) == robotouch::Route::Game);
  assert(robotouch::route(false, false) == robotouch::Route::Mood);
  assert(!robotouch::reunion(0, robotouch::ReunionMs - 1) && robotouch::reunion(0, robotouch::ReunionMs));
  assert(robotouch::reunion(0xFFFFFFFFu - 1000u, robotouch::ReunionMs));  // wrap safe
}
}  // namespace

int main() {
  rapidTapDetector(0);
  rapidTapDetector(0xFFFFFFFFu - 9000u);
  sulkIsBoundedAndSoothable();
  variantsNeverRepeatBackToBack();
  privacyAndGameFirst();
  std::cout << "PASS: tap burst 5/3 s, annoyed fade, sulk <=3 min and pet soothing, touch variants without repeats, privacy/game priority\n";
  return 0;
}
