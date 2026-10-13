#include "../RoboLoudSound.h"

#include <cassert>
#include <cmath>
#include <iostream>

using roboloud::Detector;

namespace {
constexpr uint32_t Frame = 40;
// Feeds n frames of rms produced by f(i); returns how many loud events fired.
template <typename F>
unsigned feed(Detector& d, uint32_t& now, unsigned n, F f, bool conversation = false, bool muted = false) {
  unsigned fired = 0;
  for (unsigned i = 0; i < n; ++i) { now += Frame; if (d.frame(now, f(i), conversation, muted)) ++fired; }
  return fired;
}
float quiet(unsigned i) { return 250.f + float((i * 37u) % 60u); }
float clap(unsigned i) { return i == 0 ? 22000.f : i == 1 ? 9000.f : quiet(i); }  // sharp hit, gone in ~80 ms

void clapTriggersOnceWithCooldown() {
  Detector d;
  uint32_t now = 0;
  assert(feed(d, now, 200, quiet) == 0);
  assert(feed(d, now, 10, clap) == 1);
  assert(feed(d, now, 25, quiet) == 0);
  assert(feed(d, now, 10, clap) == 0);  // inside the 8 s cooldown
  assert(feed(d, now, 200, quiet) == 0);
  assert(feed(d, now, 10, clap) == 1);  // after cooldown
}

void speechDoesNotTrigger() {
  Detector d;
  uint32_t now = 0;
  feed(d, now, 200, quiet);
  // Loud close speech: syllables of 160-320 ms at up to 14000 rms, never a <=120 ms spike.
  auto speech = [](unsigned i) { const unsigned phase = i % 10u; return phase < 6u ? 6000.f + 8000.f * std::sin(float(phase) * .5f) : 400.f; };
  assert(feed(d, now, 400, speech) == 0);
  // Ordinary speech level never crosses the absolute floor.
  auto normal = [](unsigned i) { return (i % 8u) < 5u ? 3500.f : 300.f; };
  assert(feed(d, now, 400, normal) == 0);
}

void musicSuppresses() {
  Detector d;
  uint32_t now = 0;
  feed(d, now, 200, quiet);
  auto music = [](unsigned i) { return 4000.f + 2500.f * std::sin(float(i) * .9f) + ((i % 12u) == 0 ? 20000.f : 0.f); };  // beats on a steady bed
  assert(feed(d, now, 300, music) <= 1);  // the very first beat of a song is a sudden sound
  unsigned base = 300;
  assert(feed(d, now, 300, [&](unsigned i) { return music(base + i); }) == 0);
  assert(d.musicRecently(now));
  assert(feed(d, now, 10, clap) == 0);  // right after music: suppressed
}

void gatedByConversationAndMute() {
  Detector d;
  uint32_t now = 0;
  feed(d, now, 200, quiet);
  assert(feed(d, now, 10, clap, true, false) == 0);
  assert(feed(d, now, 10, clap, false, true) == 0);
  assert(feed(d, now, 10, clap) == 1);
  assert(!d.frame(now + Frame, NAN, false, false));
}
}  // namespace

int main() {
  clapTriggersOnceWithCooldown();
  speechDoesNotTrigger();
  musicSuppresses();
  gatedByConversationAndMute();
  std::cout << "PASS: loud transient (clap) fires once with cooldown; speech, music, conversation and mute do not\n";
  return 0;
}
