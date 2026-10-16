#include "../RoboIdleActions.h"

#include <cassert>
#include <iostream>
#include <string>

using roboidle::Action;
using roboidle::Context;
using roboidle::Picker;

namespace {
Context ready(uint32_t now) { Context c; c.now = now; c.presence = true; c.lastInteractionAt = now - 60000u; return c; }

void gapsAndNoRepeats(uint32_t start, uint32_t seed) {
  Picker p(seed);
  p.reset(start);
  uint32_t lastAt = start;
  Action previous = Action::None, older1 = Action::None, older2 = Action::None;
  unsigned picks = 0, counts[unsigned(Action::Count)] = {};
  for (uint32_t t = 0; t < 2u * 3600u * 1000u; t += 250u) {
    const uint32_t now = start + t;
    const Action a = p.update(ready(now));
    if (a == Action::None) continue;
    const uint32_t gap = now - lastAt;
    assert(gap >= Picker::MinGapMs && gap <= Picker::MaxGapMs + 90000u + 250u);
    assert(a != previous);                 // never the same action twice in a row
    assert(a != older1 && a != older2);    // nor any of the 3 most recent
    older2 = older1; older1 = previous;
    assert(a != Action::Yawn);              // full energy: no yawning
    previous = a;
    lastAt = now;
    ++picks;
    ++counts[unsigned(a)];
  }
  assert(picks >= 2u * 3600u / 200u && picks <= 2u * 3600u / 30u + 1u);
  // Variety: every allowed action shows up.
  for (Action a : {Action::Scan, Action::PeekLeft, Action::PeekRight, Action::GiggleShy, Action::DoubleBlink, Action::Wink, Action::Nod, Action::Confident, Action::Sly}) assert(counts[unsigned(a)] > 0);
}

void eligibility() {
  Picker p(7);
  p.reset(0);
  Context c = ready(100000);
  c.awake = false;
  assert(p.update(c) == Action::None);
  c.awake = true;
  c.presence = false;  // nobody detected: accents wait an extra AbsentExtraMs
  p.reset(c.now);
  for (uint32_t t = c.now; t < c.now + Picker::MinGapMs + Picker::AbsentExtraMs - 250u; t += 250u) { Context q = c; q.now = t; q.lastInteractionAt = t - 60000u; assert(p.update(q) == Action::None); }
  c.presence = true;
  c.busy = true;
  assert(p.update(c) == Action::None);
  c.busy = false;
  c.lastInteractionAt = c.now - 24000u;  // too soon after touching
  assert(p.update(c) == Action::None);
  // Once eligible again the gap restarts: nothing for at least MinGapMs.
  uint32_t first = 0;
  for (uint32_t t = c.now; t < c.now + 300000u; t += 250u) {
    Context ok = ready(t);
    if (p.update(ok) != Action::None) { first = t; break; }
  }
  assert(first >= c.now + Picker::MinGapMs);
}

void lowEnergyYawns() {
  Picker p(11);
  p.reset(0);
  unsigned yawns = 0;
  Action previous = Action::None;
  for (uint32_t t = 60000; t < 3600000u; t += 250u) {
    Context c = ready(t);
    c.energy = .2f;
    const Action a = p.update(c);
    if (a == Action::None) continue;
    assert(a != previous);
    previous = a;
    if (a == Action::Yawn) ++yawns;
  }
  assert(yawns > 0);
}
void boredEyeRolls() {
  Picker calm(5), bored(5);
  calm.reset(0); bored.reset(0);
  unsigned a = 0, b = 0;
  for (uint32_t t = 60000; t < 4u * 3600u * 1000u; t += 250u) {
    Context c = ready(t);
    if (calm.update(c) == Action::EyeRoll) ++a;
    c.boredom = .9f;
    if (bored.update(c) == Action::EyeRoll) ++b;
  }
  assert(b > a && b > 0);  // PD-6.3: boredom makes eye rolls likely
}
}  // namespace

int main() {
  boredEyeRolls();
  gapsAndNoRepeats(0, 1);
  gapsAndNoRepeats(0xFFFFFFFFu - 3600000u, 42);  // millis wraps mid-run
  eligibility();
  lowEnergyYawns();
  assert(std::string(roboidle::actionName(Action::GiggleShy)) == "giggle_shy");
  std::cout << "PASS: idle layer B gaps 30-120 s, no back-to-back repeats, variety, eligibility (awake/presence/busy/25 s) and low-energy yawns\n";
  return 0;
}
