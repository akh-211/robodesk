#include "../CompanionScheduler.h"
#include <cassert>
#include <cstdint>

static companion::SchedulerContext eligible() {
  companion::SchedulerContext context;
  context.enabled = true;
  context.probablePresence = true;
  context.clockValid = true;
  context.sensorsFresh = true;
  context.localAudioAllowed = true;
  context.idleMs = 60000;
  return context;
}

int main() {
  companion::SchedulerContext context = eligible();
  companion::CompanionScheduler first(42), second(42);
  const auto a = first.update(1000, context);
  const auto b = second.update(1000, context);
  assert(a.event == companion::SchedulerEvent::Started);
  assert(b.event == a.event && b.activity == a.activity && b.durationMs == a.durationMs);
  assert(a.durationMs >= companion::CompanionScheduler::MinimumActivityMs);
  assert(a.durationMs <= companion::CompanionScheduler::MaximumActivityMs);
  assert(companion::activityDefinition(a.activity) != nullptr);
  assert(first.update(1100, context).event == companion::SchedulerEvent::None);
  assert(first.active());

  context.busy = true;
  const auto interrupted = first.update(1200, context);
  assert(interrupted.event == companion::SchedulerEvent::Interrupted);
  assert(interrupted.reason == companion::ActivityBlock::Busy && interrupted.activity == a.activity);
  assert(!first.active());
  assert(first.nextEligibleIn(1200) >= companion::CompanionScheduler::MinimumGapMs);
  assert(first.nextEligibleIn(1200) <= companion::CompanionScheduler::MaximumGapMs);
  context.busy = false;
  assert(first.update(1201, context).reason == companion::ActivityBlock::Cooldown);

  companion::CompanionScheduler explicitStart(7);
  context.microphonePrivate = true;
  auto rejected = explicitStart.update(2000, context, nullptr, 0,
      uint8_t(companion::CompanionActivityId::CuriousLook));
  assert(rejected.event == companion::SchedulerEvent::Rejected);
  assert(rejected.reason == companion::ActivityBlock::MicrophonePrivate);
  context.microphonePrivate = false;
  context.localAudioAllowed = false;
  rejected = explicitStart.update(2000, context, nullptr, 0,
      uint8_t(companion::CompanionActivityId::RhythmPlay));
  assert(rejected.event == companion::SchedulerEvent::Rejected);
  assert(rejected.reason == companion::ActivityBlock::QuietHours);
  context.localAudioAllowed = true;
  assert(explicitStart.update(2000, context, nullptr, 0, 255).event == companion::SchedulerEvent::Rejected);
  assert(explicitStart.update(2000, context, nullptr, 0,
      uint8_t(companion::CompanionActivityId::CuriousLook)).event == companion::SchedulerEvent::Started);

  struct GateCase { bool enabled, presence, clock, sensors, safety; companion::ActivityBlock reason; };
  const GateCase gates[] = {
    {false,true,true,true,false,companion::ActivityBlock::Disabled},
    {true,false,true,true,false,companion::ActivityBlock::NoPresence},
    {true,true,false,true,false,companion::ActivityBlock::ClockInvalid},
    {true,true,true,false,false,companion::ActivityBlock::StaleSensor},
    {true,true,true,true,true,companion::ActivityBlock::Safety}
  };
  for (const auto& gate : gates) {
    companion::SchedulerContext blocked=eligible();
    blocked.enabled=gate.enabled;blocked.probablePresence=gate.presence;
    blocked.clockValid=gate.clock;blocked.sensorsFresh=gate.sensors;
    blocked.safetyRecovery=gate.safety;
    companion::CompanionScheduler gated(29);
    const auto result=gated.update(2500,blocked,nullptr,0,
        uint8_t(companion::CompanionActivityId::CuriousLook));
    assert(result.event==companion::SchedulerEvent::Rejected&&result.reason==gate.reason);
  }
  context=eligible();context.ownerPaused=true;
  companion::CompanionScheduler ownerPaused(31);
  const auto ownerRejected=ownerPaused.update(2600,context,nullptr,0,
      uint8_t(companion::CompanionActivityId::CuriousLook));
  assert(ownerRejected.event==companion::SchedulerEvent::Rejected&&
         ownerRejected.reason==companion::ActivityBlock::PausedByOwner);

  companion::CompanionScheduler pauseResume(17);
  context = eligible();
  const auto toPause = pauseResume.update(5000, context, nullptr, 0,
      uint8_t(companion::CompanionActivityId::ExpressionPractice));
  context.ownerPaused = true;
  const auto paused = pauseResume.update(6000, context);
  assert(paused.event == companion::SchedulerEvent::Paused);
  assert(paused.durationMs == toPause.durationMs - 1000u);
  assert(pauseResume.remainingMs(500000) == paused.durationMs);
  assert(pauseResume.update(500000, context).event == companion::SchedulerEvent::None);
  context.ownerPaused = false;
  const auto resumed = pauseResume.update(501000, context);
  assert(resumed.event == companion::SchedulerEvent::Resumed);
  assert(resumed.durationMs == paused.durationMs);
  assert(pauseResume.cancel(502000).event == companion::SchedulerEvent::Cancelled);

  companion::CompanionScheduler completion(9);
  const auto started = completion.update(3000, context, nullptr, 0,
      uint8_t(companion::CompanionActivityId::Rest));
  assert(started.event == companion::SchedulerEvent::Started);
  const auto ended = completion.update(3000 + started.durationMs, context);
  assert(ended.event == companion::SchedulerEvent::Completed);
  assert(ended.activity == started.activity && ended.reason == companion::ActivityBlock::None);
  assert(completion.update(ended.nextEligibleMs - 1u, context).reason == companion::ActivityBlock::Cooldown);
  assert(completion.update(ended.nextEligibleMs, context).event == companion::SchedulerEvent::Started);

  uint8_t recent[3] = {
    uint8_t(companion::CompanionActivityId::CuriousLook),
    uint8_t(companion::CompanionActivityId::ExpressionPractice),
    uint8_t(companion::CompanionActivityId::Daydream)
  };
  companion::CompanionScheduler avoidRecent(3);
  const auto avoided = avoidRecent.update(4000, context, recent, 3);
  assert(avoided.event == companion::SchedulerEvent::Started);
  assert(avoided.activity != recent[0] && avoided.activity != recent[1] && avoided.activity != recent[2]);

  companion::CompanionScheduler rollover(11);
  const uint32_t start = UINT32_MAX - 1000u;
  const auto wrapStart = rollover.update(start, context, nullptr, 0,
      uint8_t(companion::CompanionActivityId::Rest));
  assert(wrapStart.event == companion::SchedulerEvent::Started);
  assert(rollover.update(start + wrapStart.durationMs - 1u, context).event == companion::SchedulerEvent::None);
  const auto wrapEnd = rollover.update(start + wrapStart.durationMs, context);
  assert(wrapEnd.event == companion::SchedulerEvent::Completed);
  assert(rollover.nextEligibleIn(start + wrapStart.durationMs) >= companion::CompanionScheduler::MinimumGapMs);
  return 0;
}
