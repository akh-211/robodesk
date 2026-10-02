#include <livingeyes/LifeDirector.h>
#include <cassert>
#include <cstdint>

int main() {
  livingeyes::LifeDirector director;
  livingeyes::LifeContext context;
  livingeyes::PersonalityProfile profile;
  director.reset(1000);
  assert(!director.startMacroSession(livingeyes::LifeActivity::None, 1000, 30000));
  assert(!director.startMacroSession(livingeyes::LifeActivity::Observe, 1000, 19999));
  assert(!director.startMacroSession(livingeyes::LifeActivity::Observe, 1000, 90001));
  assert(director.startMacroSession(livingeyes::LifeActivity::Observe, 1000, 45000));
  auto intent = director.evaluate(context, profile, 1000);
  assert(intent.macroSession && intent.activity == livingeyes::LifeActivity::Observe);
  assert(intent.validUntil == 46000 && intent.sessionStartedAt == 1000 && intent.sessionDurationMs == 45000);
  assert(director.macroSessionActive(45999));
  assert(!director.macroSessionActive(46000));

  assert(director.pauseMacroSession(11000));
  assert(!director.macroSessionActive(11000));
  intent = director.evaluate(context, profile, 11000, true);
  assert(!intent.macroSession);
  assert(!director.pauseMacroSession(11001));
  assert(director.resumeMacroSession(20000));
  intent = director.evaluate(context, profile, 20000);
  assert(intent.macroSession && intent.sessionStartedAt == 20000);
  assert(intent.sessionDurationMs == 35000 && intent.validUntil == 55000);
  assert(director.cancelMacroSession());
  assert(!director.macroSessionActive(21000));
  intent = director.evaluate(context, profile, 21000, true);
  assert(!intent.macroSession);

  const uint32_t start = UINT32_MAX - 10000u;
  assert(director.startMacroSession(livingeyes::LifeActivity::Play, start, 30000));
  intent = director.evaluate(context, profile, start);
  assert(intent.macroSession && intent.validUntil == start + 30000u);
  assert(director.macroSessionActive(start + 29999u));
  assert(!director.macroSessionActive(start + 30000u));
  return 0;
}
