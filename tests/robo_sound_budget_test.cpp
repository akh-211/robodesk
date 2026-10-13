#include "../RoboSoundBudget.h"

#include <cassert>
#include <iostream>

using robosound::SoundBudget;

namespace {
// Tries a spontaneous chirp every `every` ms for `span` ms; returns how many played.
unsigned run(SoundBudget& b, uint32_t start, uint32_t span, uint32_t every, bool quiet = false) {
  unsigned played = 0;
  for (uint32_t t = 0; t < span; t += every)
    if (b.allow(start + t, quiet)) { b.commit(start + t); ++played; }
  return played;
}
}  // namespace

int main() {
  // A chatty source (every 2 min) is held to 6 per hour, hour after hour.
  for (uint32_t start : {0u, 0xFFFFFFFFu - 5400000u}) {
    SoundBudget b;
    assert(run(b, start, 3600000, 120000) == SoundBudget::MaxPerHour);
    assert(run(b, start + 3600000, 3600000, 120000) == SoundBudget::MaxPerHour);
    // Any rolling hour holds at most 6.
    SoundBudget r;
    unsigned at[64] = {};
    unsigned n = 0;
    for (uint32_t t = 0; t < 4 * 3600000u; t += 60000)
      if (r.allow(start + t, false)) { r.commit(start + t); at[n++] = t; }
    for (unsigned i = 0; i + SoundBudget::MaxPerHour < n; ++i) assert(at[i + SoundBudget::MaxPerHour] - at[i] >= SoundBudget::WindowMs);
  }
  // Quiet hours: zero.
  SoundBudget q;
  assert(run(q, 0, 3600000, 60000, true) == 0 && q.usedLastHour(3600000) == 0);
  // A sparse source (every 15 min) is untouched.
  SoundBudget s;
  assert(run(s, 0, 2 * 3600000u, 900000) == 8);
  std::cout << "PASS: spontaneous sounds <=6 per rolling hour (wrap-safe), zero in quiet hours\n";
  return 0;
}
