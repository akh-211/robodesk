# Phase 01 — Behavior foundation

## CORE-01 — Build one behavior context

- Priority: P1; dependencies: BASE-01.
- Gather fresh sensor state, probable presence, audio/conversation busy state, local time validity, quiet/focus/privacy modes, and user stimulus into one bounded snapshot.
- Preserve source freshness; stale or unavailable readings must not look like current observations.
- Done when scheduler and diagnostics use the same snapshot and host tests cover missing, stale, and wraparound timing.

## CORE-02 — Add activity lifecycle and LivingEyes adapter

- Priority: P1; dependencies: CORE-01.
- Define selected/running/paused/completed/cancelled states, activity ID, monotonic start/deadline, and transition reason. Provide start, pause, resume, cancel, and completion APIs.
- Send expression requests through supported LivingEyes APIs; do not implement another visual director. Add a LivingEyes API only when the pinned version cannot express a needed intent.
- Done when transition tests prove exactly one terminal result and restart safely after reboot.

## CORE-03 — Unify priorities and interruptions

- Priority: P1; dependencies: CORE-02.
- Apply order: safety and microphone privacy; user touch/command; active conversation; reminder/notification; autonomous activity; baseline idle. Reuse current action and audio queues.
- Pause or cancel low priority behavior on interruption and prevent notification/activity audio from cutting off Gemini.
- Done when competing requests resolve deterministically and privacy takes effect immediately.

## CORE-04 — Explain behavior in diagnostics

- Priority: P1; dependencies: CORE-01, CORE-02.
- Expose bounded activity state, next eligible time, active constraint, and last transition through existing status diagnostics.
- Done when host tests validate status schema and the reason matches the selected scheduler decision.
