# Phase 03 — Independent activities

## LIFE-01 — Implement bounded local activity catalog

- Priority: P1; dependencies: CORE-02, CHAR-01.
- Add Curious Look, Expression Practice, Rhythm Play, Daydream, visual Stretch/Reset, Rest, and Quiet Company. Activities must use current display/sound and avoid unsupported perception or movement claims.
- Done when every catalog item has bounded duration, resource requirements, and cancellation behavior.

## LIFE-02 — Schedule macro activities

- Priority: P1; dependencies: LIFE-01, CORE-03.
- Select one activity using needs, mood, setting, recent history, and context. Initial duration is 20–90 seconds, followed by a 2–5 minute choice gap. Let LivingEyes retain control of idle micro-behavior.
- Done when deterministic tests prove no duplicate/rapid-loop activities and constraints prevent ineligible selections.

## LIFE-03 — Handle interruption and rest

- Priority: P1; dependencies: LIFE-02.
- Pause/resume only when meaningful; otherwise cancel and finish safely. Reboot returns to idle; it never resumes an expired activity. Respect quiet, focus, privacy, and active conversation.
- Done when all interruptions settle to one known state without lingering audio or stale activity claims.

## LIFE-04 — Report actual activity outcome

- Priority: P2; dependencies: LIFE-02, CORE-04.
- Track a bounded ring of 16 activity outcomes and provide a truthful short local description for an actual running/recent activity.
- Done when conversation context and status distinguish selected, running, interrupted, and completed work.
