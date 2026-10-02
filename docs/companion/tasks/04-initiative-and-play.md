# Phase 04 — Initiative and play

## SOC-01 — Add polite local invitations

- Priority: P1; dependencies: LIFE-02, CORE-03.
- Offer a brief visual/local sound invitation only when presence, idle, quiet/focus, privacy, and cooldown conditions allow. Keep existing limits of at most four daily and 30 minutes between; rejection adds at least one hour.
- Done when accept, reject, no response, restart, and invalid-clock cases preserve limits without unsolicited cloud calls.

## SOC-02 — Add touch game

- Priority: P2; dependencies: CHAR-03, SOC-01.
- Implemented host-tested tap-hold-tap state machine; the authenticated dashboard control is the explicit acceptance action, and the user can cancel there. Use the existing head touch input only; the game never opens the mic or invokes cloud AI.
- Remaining device gate: prove privacy hold, conversation, focus/Pomodoro, OTA, safety recovery, and timeout all cancel cleanly without residual audio or microphone activation.

## SOC-03 — Presence greeting and return ritual

- Priority: P2; dependencies: CORE-01, SOC-01.
- Use PIR only as probable presence, debounce repeats, and avoid claiming identity or face recognition. Make return greetings rate limited and locally dismissible.
- Done when sustained/noisy PIR cannot trigger repeated greetings.

## SOC-04 — Coordinate reminders and modes

- Priority: P2; dependencies: CORE-03, SOC-01.
- Integrate with Pomodoro, comfort alerts, briefing, and reminders using current feature scheduling and priority queues.
- Done when companion behavior is deferred in focus/quiet mode and urgent events retain their current behavior.
