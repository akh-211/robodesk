# RoboDesk Project Status Summary

**Snapshot date:** 2026-10-06  
**Workspace:** `C:\Users\imipi\OneDrive\Documents\RoboDeskSonicCharacterV2`

This file is a concise map of the current project state. The detailed task documents linked below remain the source of acceptance criteria and test evidence. Do not infer device readiness from a passing host test or compile.

## Product and board architecture

RoboDesk is a two-board companion robot:

| Board | Main responsibility |
| --- | --- |
| ESP32-C3 gateway | Wi-Fi and BLE radios, dashboard/API, phone bridge, TLS/network work, time sync and OTA coordination |
| ESP32-S3 robot | OLED/expressions, microphone and speaker, sensors, behavior/activities, IR and robot-side persistence |

The boards exchange bounded, checksummed, retried messages over UART. The current documented dual-board pins are S3 TX17 → C3 RX5, C3 TX4 → S3 RX18, plus common GND. Full power, I2C, audio, IR and power-bank wiring details are in [`docs/hardware/dual-board-wiring.md`](docs/hardware/dual-board-wiring.md).

BLE remains assigned to the C3. BLE-only startup on the S3 previously caused brownout. This is the project architecture decision; it does not mean BLE is inherently unsupported by every S3 design.

## Feature status

### Robot companion

- Seven existing activities plus pixel doodle, watch room, touch play, rhythm improv, focus company and calm breathing.
- Calm proactive behavior with presence/quiet-hour/privacy/busy-state gates, cooldowns, scheduler controls, pause/resume/cancel and bounded outcome/preference history.
- OLED status overlays for phone navigation and notifications, activity and resource status, expressions, touch responses and companion memory.
- Dashboard controls/status for activities, navigation, phone connection, privacy, readout allowlist and memory/resource health.

### Phone features

- Android bridge: Google Maps notification parsing for turn/distance/ETA, phone notification allowlist and bounded snippets, authenticated BLE operations and reconnect handling.
- iPhone bridge: encrypted bonded ANCS notifications with GATT discovery, identity checks, fragment reassembly and add/update/remove handling. iPhone navigation is unavailable by design.
- Local privacy controls keep phone snippets transient, gate optional Gemini readout, keep them out of durable memory/transcripts, and prevent microphone capture during readout.
- HawkFi monitoring is deferred; there is no implemented HawkFi integration to qualify.

Detailed feature and device gates: [`docs/companion/PHONE_CHARACTER_TASKS.md`](docs/companion/PHONE_CHARACTER_TASKS.md) and [`docs/companion/NEXT_STEPS.md`](docs/companion/NEXT_STEPS.md).

### Dual-board platform

Software implementation covers standalone and paired build roles, UART framing/retry/session health, C3 network gateway, dashboard RPC, credential migration and erase handshakes, time sync/fallback, IR support and role-bound pair OTA. The complete implementation checklist and unchecked hardware tests are in [`docs/hardware/DUAL_BOARD_TASKS.md`](docs/hardware/DUAL_BOARD_TASKS.md).

## BLE memory and safety state

- Production C3 BLE startup requires at least 140 KiB free internal heap and an 80 KiB largest contiguous block. The limits preserve headroom for the observed Wi-Fi/AP/BLE and deferred RPC allocations.
- The latest recorded C3 production run kept BLE disabled because the reserve was not met. The successful diagnostic advertising run does not prove production BLE operation.
- NimBLE uses a pinned source-host build. Current production profile uses one active connection, MTU 247, two CCCDs and bounded GATT/pool sizes. Android security, HMAC/session authentication, trusted-peer allowlisting and iPhone ANCS handling must remain intact.
- `tools/build_dual_board.ps1` now has a diagnostic profile selector: `baseline` and `msys1-6`. The latter reduces only MSYS1 block count 8 → 6; it is restricted to the C3 gateway diagnostic build, has a separate artifact directory and must not be signed or treated as production.
- The final guarded `msys1-6` image compiled in its own diagnostic directory at 1,863,952 bytes / 66,712 global bytes. `build.options.json` includes both diagnostic/profile macros, and the staged NimBLE config shows MSYS1=6. This compile result has no proven runtime heap benefit. Do not change the production value until device measurements and Android/iPhone BLE stress pass.
- ANCS operation tokens are bound to immutable connection/session generations. Retry/reset invalidates the old generation; tokens stay allocated until NimBLE host shutdown is confirmed. RPC reassembly buffer allocation is deferred until first inbound message.

Evidence and open BLE gates: [`docs/hardware/BLE_MEMORY_FIX_PLAN.md`](docs/hardware/BLE_MEMORY_FIX_PLAN.md).

## Latest software verification

- Paired production build after numeric diagnostic-guard and board-selection fixes passed: C3 binary 1,863,728 bytes, sketch 1,863,575 bytes, globals 66,704 bytes; S3 binary 2,090,752 bytes, sketch 2,090,600 bytes, globals 118,960 bytes.
- BLE-focused Python tests passed: 14 tests, including profile selection, diagnostic-only rejection, security/build contract checks and ANCS session behavior.
- Full host regression suite passed after the numeric guard and profile-selection code changes: 25 C++ suites plus the BLE, dual-board, Android parser and privacy/auth contracts (exit 0).
- A diagnostic C3 candidate with MSYS1=6 compiled after final guard hardening at 1,863,952 bytes / 66,712 global bytes. This establishes that candidate source compiled, not that BLE starts or fits at runtime.
- The paired production build, targeted BLE tests and full host suite after guard hardening passed.

## Device and external-service status

At this snapshot, `Get-PnpDevice -Class Ports` returned no serial ports in the current workspace session. No boards were flashed during this continuation. Phone availability is not confirmed. Therefore the following remain unverified:

- C3/S3 physical identity, wiring, power and first matched-pair bootstrap.
- Production BLE startup, Android pairing and notification flow, iPhone ANCS pairing/notifications, MTU and reconnect behavior.
- Combined Wi-Fi/AP, BLE, dashboard polling, UART, audio and TLS/OTA heap reserve, including largest-block trends and long soak stability.
- Real Google Maps notification formats, OLED overlays and expiry/arrival behavior.
- Signed pair OTA, wrong-role/corrupt image handling, interruption rollback and data preservation.
- IP5310 load/low-battery behavior, thermal behavior and two-hour combined operation.
- Physical microphone/audio, PIR/touch and activity behavior.

No GitHub release was published in this continuation. The former local working tree is committed on branch `claude/quirky-allen-bpvdo7` (`44b25a0`). Do not reset, clean, stage, flash or publish as part of routine documentation work.

## Recommended sequence

1. Preserve the current production BLE guards and pool values; the full host suite, paired production build, diagnostic candidate build and independent review now pass.
2. When both boards and a compatible phone are available, perform backup-first C3/S3 identity and wiring checks, then matched-pair flash and UART bring-up.
3. Measure the C3 `baseline` profile, then the isolated `msys1-6` candidate with the same workload; record heap, largest block and host stack high-water marks.
4. Run Android security/notification and iPhone ANCS flows, then combined Wi-Fi/BLE/dashboard/UART/audio/TLS/OTA, signed rollback, power and soak tests.
5. Change production reserves/pool sizes only from those measurements; record device logs and conditions before declaring the companion release-ready.

## Key project files

- [`AGENT_HANDOFF.md`](AGENT_HANDOFF.md) — concise continuation handoff; older records below its current-state section are historical.
- [`COMPANION_IMPLEMENTATION.md`](COMPANION_IMPLEMENTATION.md) — implementation and release reconciliation.
- [`docs/companion/PROGRESS.md`](docs/companion/PROGRESS.md) — companion implementation progress and prior evidence.
- [`docs/companion/PHONE_CHARACTER_TASKS.md`](docs/companion/PHONE_CHARACTER_TASKS.md) — phone, navigation, companion and device acceptance matrix.
- [`docs/hardware/DUAL_BOARD_TASKS.md`](docs/hardware/DUAL_BOARD_TASKS.md) — dual-board implementation and hardware checks.
- [`docs/hardware/BLE_MEMORY_FIX_PLAN.md`](docs/hardware/BLE_MEMORY_FIX_PLAN.md) — BLE memory profile and pending measurements.
- [`tests/run_host_tests.ps1`](tests/run_host_tests.ps1) — host regression runner.
- [`tools/build_dual_board.ps1`](tools/build_dual_board.ps1) — reproducible C3/S3 build roles and BLE diagnostic profiles.
