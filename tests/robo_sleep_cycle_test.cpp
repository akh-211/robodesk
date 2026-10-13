#include "../RoboSleepCycle.h"

#include <cassert>
#include <iostream>
#include <string>

using robosleep::Event;
using robosleep::Input;
using robosleep::SleepCycle;
using robosleep::State;

namespace {
constexpr uint32_t S = 1000, M = 60 * S;

struct Rig {
  SleepCycle cycle;
  Input in;
  explicit Rig(uint32_t start) { in.now = start; in.lastInteractionAt = start; in.presence = true; cycle.reset(start); }
  State step(uint32_t dt, Event e = Event::None) { in.now += dt; in.event = e; const State s = cycle.update(in); in.event = Event::None; return s; }
  // Advance in 1 s ticks until the state differs or the budget runs out.
  State runFor(uint32_t ms) { for (uint32_t t = 0; t < ms; t += S) step(S); return cycle.state(); }
  void interact() { in.lastInteractionAt = in.now; }
};

void fullCycle(uint32_t start) {
  Rig r(start);
  r.in.energy = .2f;
  assert(r.runFor(59 * S) == State::Awake);  // boot snooze
  assert(r.runFor(2 * S) == State::Drowsy && r.cycle.previous() == State::Awake);
  assert(r.runFor(118 * S) == State::Drowsy);
  assert(r.runFor(2 * S) == State::LightSleep);
  assert(r.cycle.asleep());
  assert(r.runFor(29 * M) == State::LightSleep);
  assert(r.runFor(2 * M) == State::DeepSleep);
  r.interact();
  assert(r.step(S, Event::Touch) == State::Waking && r.cycle.changed());
  assert(!r.cycle.redozePending());  // not quiet hours, not an alarm
  assert(r.step(2 * S) == State::Waking && !r.cycle.changed());
  assert(r.step(S) == State::Awake);
}

void quietHoursNeedSyncedClock() {
  Rig r(0);
  r.in.quietHours = true;
  r.in.clockSynced = false;
  assert(r.runFor(10 * M) == State::Awake);  // presence true, energy full: only the quiet rule could fire
  r.in.clockSynced = true;
  assert(r.step(S) == State::Drowsy);
  // Light sleep deepens after the minimum during quiet hours.
  assert(r.runFor(2 * M + S) == State::LightSleep);
  assert(r.runFor(58 * S) == State::LightSleep);
  assert(r.runFor(2 * S) == State::DeepSleep);
}

void lonelyRoomGoesToSleep() {
  Rig r(0);
  r.in.presence = false;
  assert(r.runFor(9 * M + 58 * S) == State::Awake);
  assert(r.runFor(3 * S) == State::Drowsy);
  // Presence alone (someone walks in) cancels drowsiness outside quiet hours.
  r.in.presence = true;
  assert(r.step(S, Event::Pir) == State::Awake);
  // ...and does not flap straight back even though nobody interacted for >10 min.
  r.in.presence = false;
  assert(r.runFor(58 * S) == State::Awake);
  assert(r.runFor(3 * S) == State::Drowsy);
}

void holdPreventsSleep() {
  Rig r(0);
  r.in.energy = .1f;
  r.in.hold = true;
  assert(r.runFor(30 * M) == State::Awake);
  r.in.hold = false;
  assert(r.step(S) == State::Drowsy);
  r.in.hold = true;  // navigation starts while drowsy
  assert(r.step(S) == State::Awake);
  r.in.hold = false;
  assert(r.runFor(62 * S) == State::Drowsy);
  assert(r.runFor(121 * S) == State::LightSleep);
  r.in.hold = true;  // alarm/reminder/navigation while asleep wakes up
  assert(r.step(S) == State::Waking);
}

void touchCancelsDrowsinessForAWhile() {
  Rig r(0);
  r.in.energy = .1f;
  r.runFor(61 * S);
  assert(r.cycle.state() == State::Drowsy);
  r.interact();
  assert(r.step(S, Event::Touch) == State::Awake);
  assert(r.runFor(58 * S) == State::Awake);
  assert(r.runFor(3 * S) == State::Drowsy);
}

void pirRespectsQuietHours() {
  Rig r(0);
  r.in.energy = .1f;
  r.runFor(61 * S + 121 * S);
  assert(r.cycle.state() == State::LightSleep);
  assert(r.step(S, Event::Pir) == State::Waking);  // daytime: PIR wakes
  r.runFor(4 * S);
  assert(r.cycle.state() == State::Awake);
  r.in.clockSynced = r.in.quietHours = true;
  r.runFor(61 * S);
  assert(r.cycle.state() == State::Drowsy);
  assert(r.step(S, Event::Pir) == State::Drowsy);  // quiet hours: PIR ignored
  r.runFor(121 * S);
  assert(r.cycle.state() == State::LightSleep);
  assert(r.step(S, Event::Pir) == State::LightSleep);
  assert(r.step(S, Event::WakeWord) == State::Waking);  // wake word still works
}

void midnightWakeFallsAsleepAgain() {
  Rig r(0);
  r.in.clockSynced = r.in.quietHours = true;
  r.runFor(61 * S + 121 * S + 61 * S);
  assert(r.cycle.state() == State::DeepSleep);
  r.interact();
  assert(r.step(S, Event::Touch) == State::Waking && r.cycle.redozePending());
  assert(r.runFor(3 * S) == State::Awake);
  assert(r.runFor(19 * S) == State::Awake);
  assert(r.runFor(2 * S) == State::LightSleep);
  // A second wake with interaction at +15 s extends the stay by another 20 s.
  assert(r.step(S, Event::Touch) == State::Waking);
  r.runFor(3 * S);
  assert(r.cycle.state() == State::Awake);
  r.runFor(15 * S);
  r.interact();
  assert(r.runFor(19 * S) == State::Awake);
  assert(r.runFor(2 * S) == State::LightSleep);
}

void alarmWakesThenRedozes() {
  Rig r(0);
  r.in.energy = .1f;
  r.runFor(61 * S + 121 * S);
  assert(r.cycle.state() == State::LightSleep);
  r.in.hold = true;  // the alarm is ringing
  assert(r.step(S, Event::Alarm) == State::Waking && r.cycle.redozePending());
  assert(r.runFor(60 * S) == State::Awake);  // stays up while the alarm holds
  r.in.hold = false;
  assert(r.runFor(2 * S) == State::LightSleep);  // left alone long enough already
}

void nightRedozeClearsAtDawn() {
  Rig r(0);
  r.in.clockSynced = r.in.quietHours = true;
  r.runFor(61 * S + 121 * S);
  assert(r.cycle.state() == State::LightSleep);
  r.interact();
  r.step(S, Event::Touch);
  r.runFor(3 * S);
  assert(r.cycle.state() == State::Awake && r.cycle.redozePending());
  r.in.quietHours = false;  // quiet window ended
  r.step(S);
  assert(!r.cycle.redozePending());
  assert(r.runFor(10 * M) == State::Awake);
}

void remoteSleepAndWake() {
  Rig r(0);
  r.in.energy = .9f;  // awake and fresh
  assert(r.step(S, Event::RemoteSleep) == State::LightSleep && r.cycle.previous() == State::Awake);
  assert(r.step(S, Event::Pir) == State::LightSleep);  // someone is there: a remote sleep ignores PIR
  assert(r.step(S, Event::RemoteSleep) == State::LightSleep && !r.cycle.changed());
  r.in.clockSynced = r.in.quietHours = true;
  assert(r.step(S, Event::RemoteWake) == State::Waking && !r.cycle.redozePending());  // owner asked: no night redoze
  r.runFor(4 * S);
  assert(r.cycle.state() == State::Awake);
  r.in.hold = true;
  assert(r.step(S, Event::RemoteSleep) == State::Awake);  // a running focus session/alert keeps it awake
  r.in.hold = false;
  r.in.clockSynced = r.in.quietHours = false;
  assert(r.step(S, Event::RemoteSleep) == State::LightSleep);
  assert(r.step(S, Event::Touch) == State::Waking);
  r.runFor(4 * S);
  r.in.energy = .1f;
  r.runFor(61 * S + 121 * S);
  assert(r.cycle.state() == State::LightSleep);
  assert(r.step(S, Event::Pir) == State::Waking);  // ordinary sleep: PIR wakes again
}

void names() {
  assert(std::string(robosleep::stateName(State::Awake)) == "awake");
  assert(std::string(robosleep::stateName(State::Drowsy)) == "drowsy");
  assert(std::string(robosleep::stateName(State::LightSleep)) == "lightsleep");
  assert(std::string(robosleep::stateName(State::DeepSleep)) == "deepsleep");
  assert(std::string(robosleep::stateName(State::Waking)) == "waking");
}
}  // namespace

int main() {
  fullCycle(0);
  fullCycle(0xFFFFFFFFu - 90 * S);  // millis wraps inside the drowsy/light phases
  quietHoursNeedSyncedClock();
  lonelyRoomGoesToSleep();
  holdPreventsSleep();
  touchCancelsDrowsinessForAWhile();
  pirRespectsQuietHours();
  midnightWakeFallsAsleepAgain();
  alarmWakesThenRedozes();
  nightRedozeClearsAtDawn();
  remoteSleepAndWake();
  names();
  std::cout << "PASS: sleep cycle transitions, synced-clock quiet hours, holds, snooze, PIR, night redoze, millis wrap, remote sleep/wake\n";
  return 0;
}
