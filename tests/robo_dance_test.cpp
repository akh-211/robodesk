#include "../RoboDance.h"

#include <cassert>
#include <iostream>

using robodance::BeatTracker;
using robodance::Event;

namespace {
constexpr uint32_t Frame = 40;

struct Counts { unsigned start = 0, beat = 0, stop = 0; uint32_t startAt = 0, stopAt = 0; };
// Synthetic music: a loud frame every `period` ms over a quiet floor, for `ms`.
void music(BeatTracker& t, uint32_t& now, uint32_t ms, uint32_t period, Counts& c, bool blocked = false) {
  for (uint32_t e = 0; e < ms; e += Frame, now += Frame) {
    const float rms = (e % period) < Frame ? 8000.f : 300.f;
    const Event ev = t.frame(now, rms, blocked);
    if (ev == Event::Start) { ++c.start; c.startAt = now; }
    if (ev == Event::Beat) ++c.beat;
    if (ev == Event::Stop) { ++c.stop; c.stopAt = now; }
  }
}
void quiet(BeatTracker& t, uint32_t& now, uint32_t ms, Counts& c) {
  for (uint32_t e = 0; e < ms; e += Frame, now += Frame) {
    const Event ev = t.frame(now, 300.f, false);
    if (ev == Event::Stop) { ++c.stop; c.stopAt = now; }
    assert(ev != Event::Start && ev != Event::Beat);
  }
}

void steadyBeatStartsThenStops(uint32_t start) {
  BeatTracker t;
  uint32_t now = start;
  Counts c;
  music(t, now, 20000, 480, c);  // 125 BPM
  assert(c.start == 1 && t.dancing());
  assert(uint32_t(c.startAt - start) <= 8 * 480 + Frame);  // 8 onsets
  assert(c.beat >= 30);
  assert(t.intervalMs() >= 440 && t.intervalMs() <= 520);
  quiet(t, now, 5000, c);
  assert(c.stop == 1 && !t.dancing() && uint32_t(c.stopAt - (start + 20000)) <= BeatTracker::StopSilenceMs + 480);
  // Resting: the same song again soon does not restart the dance.
  Counts again;
  music(t, now, 20000, 480, again);
  assert(again.start == 0);
  quiet(t, now, BeatTracker::CooldownMs, again);
  music(t, now, 10000, 480, again);
  assert(again.start == 1);
}

void irregularSoundsDoNotDance() {
  BeatTracker t;
  uint32_t now = 1000, r = 99;
  for (unsigned i = 0; i < 3000; ++i, now += Frame) {
    r = r * 1103515245u + 12345u;
    const float rms = (r >> 16) % 7 == 0 ? 7000.f : 300.f;  // speech-like random bursts
    assert(t.frame(now, rms, false) != Event::Start);
  }
  // A rattle (120 ms; a 200 ms beat is simply danced at half time) or a beat slower than 60 BPM is not danced to.
  BeatTracker fast, slow;
  Counts a, b;
  uint32_t n1 = 0, n2 = 0;
  music(fast, n1, 20000, 120, a);
  music(slow, n2, 30000, 1200, b);
  assert(a.start == 0 && b.start == 0);
}

void conversationAndMuteBlock() {
  BeatTracker t;
  uint32_t now = 0;
  Counts c;
  music(t, now, 20000, 480, c, true);
  assert(c.start == 0);
  music(t, now, 8000, 480, c);
  assert(c.start == 1);
  music(t, now, 2000, 480, c, true);  // the robot starts talking: dance stops at once
  assert(c.stop == 1 && !t.dancing());
}

void boundedLength() {
  BeatTracker t;
  uint32_t now = 0;
  Counts c;
  music(t, now, 90000, 520, c);
  assert(c.start == 1 && c.stop == 1 && uint32_t(c.stopAt - c.startAt) <= BeatTracker::MaxDanceMs + Frame);
}
}  // namespace

int main() {
  steadyBeatStartsThenStops(5000);
  steadyBeatStartsThenStops(0xFFFFFFFFu - 10000u);
  irregularSoundsDoNotDance();
  conversationAndMuteBlock();
  boundedLength();
  std::cout << "PASS: dance starts on a steady 60-200 BPM beat, stops on silence/conversation/60 s, 5 min rest, ignores speech-like bursts\n";
  return 0;
}
