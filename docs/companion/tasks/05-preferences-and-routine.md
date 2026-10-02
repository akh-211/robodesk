# Phase 05 — Preferences and routine

## MEM-01 — Learn activity preference

- Priority: P2; dependencies: LIFE-04, SOC-02.
- Update small preference scores only from explicit favorite/skip controls or clear user acceptance/completion signals; do not infer preferences from PIR alone.
- Done when preferences are bounded, explainable in dashboard, disabled on request, and resettable without removing reminders.

## MEM-02 — Learn coarse routine timing

- Priority: P2; dependencies: MEM-01.
- Optionally aggregate interaction counts into eight daily time buckets. Do not store audio or detailed presence history.
- Done when data minimization, opt-out, reset, rollover, and maximum storage are tested.

## MEM-03 — Version preference persistence

- Priority: P1; dependencies: MEM-01.
- Add a versioned bounded preference payload with validation and safe fallback. Batch writes; persist no more frequently than once per 15 minutes except explicit user changes.
- Done when old snapshots load, new snapshots round-trip, invalid data is rejected, and failed writes never report success.

## MEM-04 — Add truthful AI activity context

- Priority: P2; dependencies: LIFE-04.
- Add current/recent activity and its state to existing robot context/tool response. AI may describe the state but cannot start arbitrary hardware actions or claim unobserved events.
- Done when offline, interrupted, and stale-state prompts cannot fabricate a running activity.
