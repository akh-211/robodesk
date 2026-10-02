# Phase 02 — Character and touch

## CHAR-01 — Define stable character state

- Priority: P1; dependencies: CORE-01.
- Represent virtual energy, curiosity, social need, boredom, mood, and mood cause with bounded values and slow drift. Do not represent virtual state as physical telemetry.
- Done when state transitions are deterministic under an injected clock and invalid sensor/time context has safe defaults.

## CHAR-02 — Add mood inertia and recovery

- Priority: P1; dependencies: CHAR-01.
- Derive mood from needs and real events; cap duration/intensity and recover gradually. User-facing actions must not imply distress, guilt, jealousy, or coercive bonding.
- Done when repeated stimuli do not create unbounded mood changes and recovery works after interruptions and reboot.

## CHAR-03 — Interpret touch gestures

- Priority: P1; dependencies: CORE-01.
- Extend debounced GPIO events to single tap, double tap, hold, and petting sequence while preserving long-hold microphone privacy behavior.
- Done when gesture boundaries, bounce, overlapping inputs, and privacy precedence have host tests and device latency evidence.

## CHAR-04 — Map character events to expression/audio

- Priority: P2; dependencies: CHAR-01, CHAR-02, CHAR-03.
- Map mood cause and gesture to existing performance tags, expressions, and short local sounds. No direct blocking playback.
- Done when sound can be muted, quiet hours are respected, and conversation audio is never interrupted.
