# RoboDesk Companion Roadmap

## Goal

Develop RoboDesk into a cheerful, curious desk companion that reacts promptly, chooses meaningful activities while idle, initiates interaction politely, and learns a small set of owner preferences. Keep the current character identity stable. Build on the installed OLED, speaker, microphone, touch inputs, PIR, IMU, and environmental sensors.

Autonomous activities and invitations run locally. Online AI is used in response to user interaction, not to invent or drive idle activity. The companion must describe only an activity that is actually selected or running. Do not claim physical movement or object recognition without supporting hardware.

## Architecture

- LivingEyes remains the sole visual behavior director and renderer. RoboDesk may select high-level activity intent, then submit it using supported LivingEyes APIs and existing action/audio queues.
- A bounded companion activity lifecycle records selection, running, pause, completion, cancellation, and reason. Monotonic time drives durations and cooldowns.
- Safety and microphone privacy outrank user interaction, conversation, reminders/notifications, autonomous activities, and baseline idle behavior.
- The scheduler does not duplicate LivingEyes idle micro-behaviors. First activities last 20–90 seconds, with a 2–5 minute gap between selections.
- Virtual energy is a character value and must never be represented as a physical battery measurement. PIR means probable presence only.
- Learn activity preference and broad time-of-day preference only. Learning is optional, inspectable, resettable, bounded, and contains no stored audio.
- Keep persistent writes bounded. Snapshot format changes require safe versioning and migration. Storage failure must not trigger automatic format.

## Delivery order

| Phase | Focus | Depends on |
| --- | --- | --- |
| 00 | Reconcile the v9 baseline and qualification gates | — |
| 01 | Behavior context, activity lifecycle, priority and diagnostics | 00 |
| 02 | Stable personality, mood causes and expressive touch | 01 |
| 03 | Local self-directed activity catalog and scheduler | 01, 02 |
| 04 | Polite invitations, touch games and companion integration | 02, 03 |
| 05 | Bounded preference learning and truthful conversation context | 03, 04 |
| 06 | Dashboard controls, soak testing, and release qualification | 01–05 |

## Activity catalog for phase 03

Start with local activities that need no additional hardware: Curious Look (visual attention without object-recognition claims), Expression Practice, Rhythm Play (short local sounds), Daydream (clearly imaginative text and expression), Stretch/Reset (visual sequence only), Rest, Quiet Company, and Touch Game after the user accepts an invitation. Reuse existing sound, action, and LivingEyes performance queues; never play over Gemini speech.

## Acceptance and rollout

- Host tests cover deterministic selection, lifecycle transitions, priority/interruption, cooldowns, quiet hours, invalid time, `millis()` wrap, stale sensors, and persistence failure/migration.
- Device checks include touch-to-first-render p95 at or below 100 ms under conversation load; a two-hour offline activity run; microphone privacy across reboot; and later a 24-hour soak.
- OTA qualification verifies signature, new firmware boot, data preservation, and recovery behavior. A successful build alone is not device qualification.
- Keep the OTA image below the current 3,145,728-byte app slot. Add no dependencies without a demonstrated need.
- Do not publish a new firmware release until the relevant device qualification evidence is recorded.

## Task index

- [Detailed next-step execution checklist](NEXT_STEPS.md)
- [00 — Baseline and stability](tasks/00-baseline.md)
- [01 — Behavior foundation](tasks/01-behavior-foundation.md)
- [02 — Character and touch](tasks/02-character-and-touch.md)
- [03 — Independent activities](tasks/03-independent-activities.md)
- [04 — Initiative and play](tasks/04-initiative-and-play.md)
- [05 — Preferences and routine](tasks/05-preferences-and-routine.md)
- [06 — Dashboard and qualification](tasks/06-dashboard-qualification.md)
- [Progress log](PROGRESS.md)

## Decisions and defaults

- Character: cheerful and curious, with a consistent core personality.
- Initiative: adaptive and polite. Keep existing limits of at most four invitations per day and at least 30 minutes between them; refusal adds at least one hour of cooldown.
- AI: local-first; cloud use follows user interaction and existing privacy/quota controls.
- Language: Indonesian conversational style follows existing persona settings; offline command recognition remains English first.
- Hardware: software-first on the current device. Camera, wheels, servos, new sensors, and iPhone ANCS are separate future work.
- Learning: enabled only where the owner chooses; a reset removes learned preferences without deleting reminders.
