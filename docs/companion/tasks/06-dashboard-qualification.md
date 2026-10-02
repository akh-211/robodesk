# Phase 06 — Dashboard and qualification

## UX-01 — Add companion dashboard controls

- Priority: P1; dependencies: CORE-04, SOC-01, MEM-01.
- Show “what is the robot doing?”, start/stop/pause activity, invitation intensity, local sound permission, focus/quiet/pause initiative, favorites, and learning reset.
- Mutations reuse existing authenticated, same-origin POST checks; never expose a state-changing unauthenticated route.
- Done when dashboard works on mobile/desktop, every control reflects saved state, and invalid requests leave state unchanged.

## UX-02 — Add concise activity history and diagnostics

- Priority: P2; dependencies: LIFE-04, UX-01.
- Show the bounded latest outcomes and why the companion is waiting. Avoid transcripts and raw audio.
- Done when histories stay within the fixed limit and clear/reset semantics are documented.

## QUAL-01 — Run host and firmware gates

- Priority: P1; dependencies: 01–05.
- Run focused host tests, full regression suite, diff checks, and ESP32-S3 build. Confirm firmware remains below the 3,145,728-byte app slot.
- Done when commands/results and image size are recorded in `PROGRESS.md`.

## QUAL-02 — Run device soak and OTA checks

- Priority: P1; dependencies: QUAL-01.
- Measure p95 touch-to-first-render at or below 100 ms under conversation load; run two-hour offline activity test, privacy reboot test, and later a 24-hour soak. Verify signed OTA boot, preserved settings/memory, and failure recovery.
- Done only with device identity, firmware version, test conditions, logs/observations, and explicit pass/fail evidence. Do not publish a new release while a required gate remains unverified.
