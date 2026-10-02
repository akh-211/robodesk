#include <cassert>
#include <cstring>
#include "CompanionActivity.h"
#include "CompanionActivityCatalog.h"

int main() {
  assert(companion::activityCatalogSize() == 7);
  for (uint8_t id = 1; id <= companion::activityCatalogSize(); ++id) {
    const companion::ActivityDefinition* definition = companion::activityDefinition(id);
    assert(definition && definition->lifeActivity >= 1 && definition->lifeActivity <= 9);
    assert(definition->minDurationMs >= 20000u && definition->maxDurationMs <= 90000u);
    assert(definition->minDurationMs <= definition->defaultDurationMs);
    assert(definition->defaultDurationMs <= definition->maxDurationMs);
    assert((definition->resources & companion::ActivityResourceDisplay) != 0);
  }
  assert(companion::activityDefinition(0) == nullptr && companion::activityDefinition(8) == nullptr);
  assert(std::strcmp(companion::companionActivityName(3), "rhythm_play") == 0);

  companion::BehaviorContext context;
  assert(context.allowsAutonomousActivity());
  context.busy = true;
  assert(!context.allowsAutonomousActivity());
  assert(context.activityBlock() == companion::ActivityBlock::Busy);
  context.busy = false;
  context.safetyRecovery = true;
  assert(context.activityBlock() == companion::ActivityBlock::Safety);
  context.safetyRecovery = false;
  context.ownerPaused = true;
  assert(context.activityBlock() == companion::ActivityBlock::PausedByOwner);

  companion::ActivityTracker tracker;
  companion::ActivityOutcome outcome;
  assert(tracker.state() == companion::ActivityState::Idle);
  tracker.sync(4, 100, true);
  assert(tracker.activity() == 4);
  assert(tracker.state() == companion::ActivityState::Running);
  assert(tracker.startedAt() == 100);

  tracker.sync(4, 200, false, companion::ActivityBlock::Busy);
  assert(tracker.state() == companion::ActivityState::Paused);
  assert(tracker.lastTransition() == companion::ActivityTransitionReason::Paused);
  assert(tracker.block() == companion::ActivityBlock::Busy);
  tracker.sync(4, 300, true);
  assert(tracker.state() == companion::ActivityState::Running);
  assert(tracker.lastTransition() == companion::ActivityTransitionReason::Resumed);
  assert(tracker.startedAt() == 100);

  tracker.sync(0, 400, true);
  assert(tracker.state() == companion::ActivityState::Completed);
  assert(tracker.latestOutcome(outcome));
  assert(outcome.durationMs == 200);
  tracker.sync(0, 401, true);
  assert(tracker.state() == companion::ActivityState::Idle);
  assert(std::strcmp(companion::currentActivityName(tracker.state(), tracker.activity()), "none") == 0);
  assert(tracker.outcomeCount() == 1);

  tracker.sync(6, 500, true);
  tracker.cancel(600);
  assert(tracker.state() == companion::ActivityState::Cancelled);
  tracker.sync(6, 700, true);
  assert(tracker.state() == companion::ActivityState::Running);
  assert(tracker.startedAt() == 700);

  companion::ActivityTracker observedIntent;
  observedIntent.syncIntent(6, 800, true);
  observedIntent.syncIntent(7, 1300, true);
  observedIntent.syncIntent(0, 1500, true);
  assert(observedIntent.state() == companion::ActivityState::Idle);
  assert(observedIntent.outcomeCount() == 0);
  observedIntent.syncIntent(6, 1600, true);
  observedIntent.syncIntent(6, 1700, false, companion::ActivityBlock::Busy);
  assert(observedIntent.state() == companion::ActivityState::Paused);
  observedIntent.syncIntent(0, 2000, false, companion::ActivityBlock::Busy);
  assert(observedIntent.state() == companion::ActivityState::Idle);
  assert(observedIntent.outcomeCount() == 0);

  assert(tracker.transitions() == 6);
  assert(std::strcmp(companion::activityStateName(tracker.state()), "running") == 0);
  assert(std::strcmp(companion::activityBlockName(companion::ActivityBlock::Disabled), "disabled") == 0);
  assert(std::strcmp(companion::lifeActivityName(7), "self_entertain") == 0);
  assert(std::strcmp(companion::lifeActivityName(255), "none") == 0);

  companion::ActivityTracker history;
  history.sync(5, 1000, true);
  history.sync(0, 1400, false, companion::ActivityBlock::Busy);
  assert(history.state() == companion::ActivityState::Interrupted);
  assert(std::strcmp(companion::activityStateName(history.state()), "interrupted") == 0);
  assert(history.latestOutcome(outcome));
  assert(outcome.activity == 5);
  assert(outcome.kind == companion::ActivityOutcomeKind::Interrupted);
  assert(outcome.reason == companion::ActivityBlock::Busy);
  assert(outcome.startedAt == 1000 && outcome.endedAt == 1400);
  assert(std::strcmp(companion::activityOutcomeName(outcome.kind), "interrupted") == 0);
  history.sync(5, 2000, true);
  assert(history.state() == companion::ActivityState::Running);
  assert(history.startedAt() == 2000);
  history.sync(0, 2300, true);
  assert(history.latestOutcome(outcome));
  assert(outcome.kind == companion::ActivityOutcomeKind::Completed);
  history.sync(0, 2301, true);
  assert(history.state() == companion::ActivityState::Idle);
  assert(std::strcmp(companion::currentActivityName(history.state(), history.activity()), "none") == 0);
  assert(history.latestOutcome(outcome) && outcome.kind == companion::ActivityOutcomeKind::Completed);
  assert(history.lastTransition() == companion::ActivityTransitionReason::Completed && history.lastTransitionAt() == 2300);
  companion::ActivityOutcome olderOutcome;
  assert(history.outcomeNewest(0, outcome) && outcome.kind == companion::ActivityOutcomeKind::Completed);
  assert(history.outcomeNewest(1, olderOutcome) && olderOutcome.kind == companion::ActivityOutcomeKind::Interrupted);
  assert(!history.outcomeNewest(2, olderOutcome));
  for (uint32_t i = 0; i < companion::ActivityTracker::OutcomeCapacity + 3; ++i) {
    history.sync(4, 3000 + i * 2, true);
    history.sync(0, 3001 + i * 2, true);
  }
  assert(history.outcomeCount() == companion::ActivityTracker::OutcomeCapacity);
  assert(history.latestOutcome(outcome) && outcome.kind == companion::ActivityOutcomeKind::Completed);
  assert(history.outcomeNewest(0, outcome) && outcome.activity == 4);
  history.clearHistory();
  assert(history.outcomeCount() == 0 && !history.latestOutcome(outcome));

  companion::ActivityTracker invalid;
  invalid.sync(255, 900, true);
  assert(invalid.state() == companion::ActivityState::Idle);

  companion::HeadTouchGestureDetector gestures;
  assert(gestures.update(true, 100) == companion::HeadTouchGesture::None);
  assert(gestures.update(false, 160) == companion::HeadTouchGesture::Tap);
  assert(gestures.update(true, 300) == companion::HeadTouchGesture::None);
  assert(gestures.update(false, 360) == companion::HeadTouchGesture::DoubleTap);
  assert(gestures.update(true, 1000) == companion::HeadTouchGesture::None);
  assert(gestures.update(true, 2200) == companion::HeadTouchGesture::Petting);
  assert(gestures.update(true, 2800) == companion::HeadTouchGesture::None);
  assert(gestures.update(false, 2900) == companion::HeadTouchGesture::PettingEnd);

  companion::HeadTouchGestureDetector wrapGestures;
  assert(wrapGestures.update(true, UINT32_MAX - 100) == companion::HeadTouchGesture::None);
  assert(wrapGestures.update(true, 1100) == companion::HeadTouchGesture::Petting);
  assert(wrapGestures.update(false, 1200) == companion::HeadTouchGesture::PettingEnd);

  companion::ActivityTracker wrap;
  const uint32_t beforeWrap = UINT32_MAX - 10;
  wrap.sync(3, beforeWrap, true);
  wrap.sync(0, 15, true);
  assert(wrap.state() == companion::ActivityState::Completed);

  companion::ActivityTracker promptTracker;
  promptTracker.sync(4, 100, true);
  auto prompt = promptTracker.promptContext(150);
  assert(prompt.state == companion::ActivityState::Running && prompt.currentActivity == 4);
  assert(prompt.currentAgeMs == 50 && !prompt.hasOutcome);
  promptTracker.sync(4, 200, false, companion::ActivityBlock::Busy);
  prompt = promptTracker.promptContext(250);
  assert(prompt.state == companion::ActivityState::Paused && prompt.currentActivity == 4);
  promptTracker.sync(0, 300, true);
  prompt = promptTracker.promptContext(320);
  assert(prompt.state == companion::ActivityState::Completed && prompt.currentActivity == 0);
  assert(prompt.hasOutcome && prompt.outcomeRecent && prompt.outcomeAgeMs == 20);
  assert(prompt.outcome.activity == 4 && prompt.outcome.kind == companion::ActivityOutcomeKind::Completed);
  prompt = promptTracker.promptContext(60301);
  assert(prompt.hasOutcome && !prompt.outcomeRecent && prompt.outcomeAgeMs == 60001);

  companion::ActivityTracker afterReboot;
  prompt = afterReboot.promptContext(70000);
  assert(prompt.state == companion::ActivityState::Idle && prompt.currentActivity == 0);
  assert(!prompt.hasOutcome && !prompt.outcomeRecent);
  return 0;
}
