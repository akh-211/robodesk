# Companion Roadmap Progress

Status values: `TODO`, `IN_PROGRESS`, `BLOCKED`, `DONE`. A task is DONE only when its listed verification passes. Hardware-only evidence must identify the device and test conditions.

## Latest hardware update (2026-10-03)

- The RAM-optimization image was built for ESP32-S3 / Arduino-ESP32 3.3.11 with the pinned LivingEyes checkout, 16 MB flash, OPI PSRAM, and the custom OTA layout. Generic Arduino upload targeted `0x10000`; this is not an application slot for this partition map. The active slot was identified from OTA metadata and the image was correctly written and hash-verified at `app0=0x400000`. No new GitHub release was published.
- USB serial showed READY, Wi-Fi connected, and no reset loop during approximately three minutes of observation. Runtime sample: internal free/minimum/largest block `98,504/57,160/42,996` bytes; PSRAM free `8,175,104` bytes; speaker and pre-roll rings both PSRAM-backed.
- Built-in `audio_test` sent a Gemini request and processed `177,840` speaker frames. Final audio counters showed zero underruns, starvation, and drops; buffer drained and state returned READY. Serial counters do not prove audible speaker output or microphone pickup.
- Browser/dashboard qualification, hands-on mic/speaker/touch/PIR checks, two-hour offline soak, 24-hour soak, and signed OTA/rollback remain pending. `robodesk.local` did not resolve during the latest attempt.
- The dashboard health panel and speaker-drop telemetry are now implemented in source. Host regression and ESP32-S3 build pass; sketch uses 2,738,171 bytes; generated binary is 2,738,320 bytes, within the 3 MiB OTA slot; static global RAM is 129,324 / 327,680 bytes, leaving 198,356 bytes. Renderer fixtures cover healthy and incomplete telemetry. Startup arms OTA rollback before audio buffer allocation, then starts its readiness window after the audio tasks and services start. The new image has not been flashed: both USB reads of active app0 were corrupted (first at 20%, retry at 10%), so preserve the currently installed firmware until a stable transfer path is available.
- Follow-up device run: the new image was backed up-first and written/hash-verified at app0 `0x400000`. Post-flash UART health probe passed (three windows: heartbeats present, no boot/reset messages/timeouts); live snapshot showed internal free/minimum/largest `98,640/58,356/42,996` bytes, PSRAM free `8,175,104` bytes, both audio rings on PSRAM. `audio_test` processed `152,641` speaker frames with zero underrun/starvation/drop, returned READY. Robot GitHub check reached DNS/TCP/HTTPS, verified a valid v10 manifest signature, and skipped install because installed version is already v10.
- Persistent `LEV,BRAIN,SAVE_FAIL` repeated on the ~60 s retry interval. The prior generic upload from `0x10000` overlapped the first `116,880` bytes of `spiffs` at `0x290000`; the preserved full SPIFFS backup (1,441,792 bytes, SHA-256 `CA769397774F5EDB8BFA88C2567651FC99F36109385649070580CF8FC6708971`) fails LittleFS listing with `Corrupted dir pair at {0x0, 0x1}`. App0 (3 MiB) and partition/NVS/OTA metadata (32 KiB) backups are also saved under `%LOCALAPPDATA%\Temp\RoboDeskEnhancement-20261003`. An empty 1,441,792-byte LittleFS image was prepared and validated locally but has not been written. Formatting would erase files/local brain memory in SPIFFS; wait for explicit owner approval.
- Dashboard is reachable at robot IP `192.168.1.6` but `/api/status` returns 401 without Basic Auth. The user logged in locally, but no browser surface is available to this session, so visual rendering remains unverified. The dashboard is temporarily unavailable during USB bootloader reads; after the last read the serial health probe confirmed the robot was running again.
- Owner approved SPIFFS recovery. The validated empty LittleFS image was written only to `0x290000..0x3EFFFF` (1,441,792 bytes); esptool flash hash passed. NVS, partition table, coredump, app slots, and model partition were not changed. After reset, three serial health windows passed. A further 90-second filtered monitor showed heartbeats and no `BRAIN,SAVE_FAIL`, but no `BRAIN,SAVE` event either. Owner reports `companion.storageHealthy=true`; filesystem mount is now healthy, while actual save/reload of a memory record remains to be proven.
- Owner reports the dashboard heap/audio panel displays “Healthy: heap reserve and audio counters are within the monitored thresholds.” This confirms the live panel is visible and currently green; it does not include the separate `companion.storageHealthy` persistence signal.

## Current baseline

- Firmware source baseline: RoboDesk v9, commit `709958e`.
- Host regression suite and ESP32-S3 firmware build passed before this roadmap started.
- Release assets v9 are published. This does not qualify the new roadmap behavior on-device.
- LivingEyes is the current visual director; RoboDesk owns conversation, sensor integration, reminders, and feature orchestration.
- Detailed next work order, implementation steps, test gates, device checklist, and evidence template: `NEXT_STEPS.md`.

## Active tasks

- `BASE-02` - physical baseline is pending; no hardware test was performed for v9 publication.
- `BASE-03` - physical OTA and rollback qualification is pending.

- `CORE-04` - `/api/status` reports current activity and latest outcome; idle now reports no current activity while retaining the latest outcome. Dashboard reads the payload, has an OLED-style expression preview wired to the existing expression allowlist, live character state, mockup-matched navigation/theme, and explicit game acceptance. Host contract checks pass; visual browser/device verification remains pending.
- `CHAR-03` - head double tap maps to `RepeatedPet`; LivingEyes retains long pet detection and side hold retains microphone privacy. Host gesture tests pass; latency/overlap still need device verification.
- `SOC-01` - proactive cue uses a local curious expression/SFX within existing invitation limits and does not send Gemini text or open the microphone. Host gate tests pass; device cue and cooldown behavior remain pending.
- `LIFE-04` - the tracker and dashboard support a bounded 16-outcome ring. Production records macro outcomes through the session scheduler and excludes short LivingEyes intent expiry; host implementation is complete and device validation remains pending.
- `SOC-02` - a local tap-hold-tap game starts only after explicit dashboard acceptance while head touch is released; host tests cover success, failure, timeout, interruption, and timer wrap. Robot-side behavior and device cancellation tests remain pending.
- `SOC-03` - PIR has stable-presence debounce, a 30-minute cooldown committed only after audio acceptance, return detection after two minutes away, and gates for owner opt-outs, focus, privacy, busy conversation, OTA, conversation quiet window, and quiet hours. Host tests pass; PIR sensitivity and audible cue remain pending on-device.
- `MEM-01/02/03/04` - Opt-in preferences have bounded favorite/skip scores and coarse time buckets in separate versioned CRC storage; three signals in a bucket permit only a low-probability, low-strength bias toward net-favorite activities in that bucket. Dashboard controls and factory reset cover the learner. AI turn context uses the ActivityTracker snapshot, distinguishes running/paused/idle, and exposes outcomes as recent only through 60 seconds; stale outcomes are labeled stale with activity details redacted. Long sensor context cannot truncate the activity section. Twenty-three host suites and the latest ESP32-S3 compile pass; device qualification remains pending.

## Implementation update (2026-10-02)

- LIFE-01 now has a seven-entry production catalog tied to executable behaviors and the LivingEyes director integration point.
- Activity diagnostics expose the current observed intent, last lifecycle transition and age; the dashboard can render/clear a bounded newest-first history. The firmware now observes expiring/replaced LivingEyes intents without misreporting them as completed macro activities. Macro history records completed scheduler sessions; short LivingEyes intent expiry remains excluded. `CORE-04` / `UX-02` browser and device verification remains pending.
- Added CharacterMind host coverage for state bounds, mood overlay expiry, recovery, and wraparound. Fixed the firmware's sulking-expression gate to use the correctly tested mood-overlay lifetime. The pinned LivingEyes implementation's own `sulking()` helper has inverted active-window logic; the dependency is outside this workspace and was not modified.
- `LIFE-01/02/03/04` are implemented in source: a seven-entry executable catalog, bounded no-repeat scheduler, pause/resume/cancel lifecycle, dashboard controls, outcome history, and a LivingEyes macro-session hook that preserves its baseline idle behavior. Host tests and target build pass. Device/browser/OTA qualification remains pending.
- Previous baseline: host regression passed 21 suites using LivingEyes fingerprint `afe21dd2700beaf9f119ec0fcba76ae70f803e49a915adac111d41eaa9fcf3c9`. Target compile passed: sketch size 2,726,099 / 3,145,728 bytes, generated binary 2,726,240 bytes, RAM 168,348 / 327,680 bytes. The 16 MB partition table retains app0 at `0x400000` and app1 at `0x700000`, each `0x300000`. All browser/robot/OTA checks are intentionally deferred to the end phase.

## Completed tasks

- Task documents for phases 00-06 were created. They define acceptance criteria but do not count as implementation completion.
- `BASE-01` - source and release status are reconciled in `COMPANION_IMPLEMENTATION.md`; v9 publication does not count as device qualification.
- `CORE-01` - `BehaviorContext` snapshots busy/pause/enable, privacy, probable presence, clock, sensor freshness, and fall recovery; host gate tests and target compile passed.
- `CORE-02` - lifecycle tracker is integrated with LivingEyes `LifeIntent`; host lifecycle tests and target compile passed.
- `CORE-03` - the existing ordered action queue was verified for safety/user/conversation/reminder/sensor/proactive/ambient precedence, conversation suppression, cancellation, and the existing privacy barrier. Host tests pass; physical preemption latency remains pending.
- Activity tracker host tests cover start, pause/resume, interruption, completion, cancellation, latest outcome, terminal reset, 16-entry ring bounds, unknown intent, transition count, and unsigned timer rollover.

## Historical blockers and evidence

- Latest unflashed ESP32-S3 compile: Arduino-ESP32 3.3.11, pinned LivingEyes checkout, 16 MB flash/OPI PSRAM/custom partition. Sketch: 2,726,099 of 3,145,728 bytes; generated binary: 2,726,240 bytes; global RAM: 168,348 of 327,680 bytes. Twenty-one host suites pass. `arduino-cli board list` currently reports COM9 as USB Serial Port / Unknown; board identity and robot/browser/OTA qualification remain pending.
- Device-only verification remains pending until each phase schedules the relevant physical test. Do not infer a pass from host tests or compilation.

## Current implementation gate (2026-10-02)

- Companion scheduler/catalog, activity lifecycle controls, repeat/cooldown policy, dashboard controls/status, and the LivingEyes `LifeDirector` macro-session hook are implemented. LivingEyes fingerprint is pinned at `75dd27680fd89a7b5de702bb2ddcabb60b63ff36334b2eb50b5797fc7bc2b123`.
- Pinned host runner completed successfully: 23 suites, exit code 0. ESP32-S3 target compile completed with Arduino-ESP32 3.3.11, custom 16 MB/OPI PSRAM partition layout; final image is 2,732,560 bytes (within the 3,145,728-byte OTA slot).
- COM9 enumerates as USB Serial Port, VID `0x1A86`, PID `0x55D3`, serial `5B91003763`; COM9 returned continuous heartbeat logs in the final serial health probe. Dashboard behavior and OTA/device qualification have not yet been verified. No firmware was flashed or release published in this gate.

- Follow-up review fixes: removed direct learned `LifeDirector` schedules (preference bias now stays behind scheduler gates), blocked Rhythm during Gemini follow-up quiet time, made macro-start failures reject dashboard requests, and scoped Rhythm queue cancellation by activity source. Regression tests cover all scheduler blockers and preserve unrelated ambient cues.
- Final rebuild after review fixes: sketch 2,732,415 / 3,145,728 bytes; binary 2,732,560 bytes; RAM 168,452 / 327,680 bytes. Partition offsets verified against the documented two-slot layout.
- Final device check: COM9 USB serial identity is VID `0x1A86`, PID `0x55D3`, serial `5B91003763`; 2 serial-health cycles passed (heartbeats present, no reboot messages/timeouts). Read-only dashboard status once returned HTTP 200 with the documented default local PIN, but subsequent `robodesk.local` name resolution and mDNS queries failed. No firmware was flashed or release published because authenticated OTA could not be kept reachable for boot/rollback qualification.

## Dual-board device check (2026-10-06)

- C3/S3 app0 flashes and hashes verified. C3 Wi-Fi-first startup fixes RX-buffer allocation failure; a BLE heap guard prevents the observed allocation abort. BLE remains inactive on this board and needs memory optimization before phone bridge qualification.
- C3 dashboard and status HTTP 200 verified at `192.168.1.6`. Offline-S3 settings save returned 200, rebooted C3, and restored its Wi-Fi connection; HTTP-only verification preserved all 67 form fields.
- Fixed S3 `PAIR_SETTINGS`: accept the intentionally empty role PIN only with durable NVS readback. S3 now sends heartbeats; UART handshake and fresh remote status are verified. In the final 20-second sample RX advanced, with no new retry/disconnect/CRC error or C3 reboot.
- Both target builds and 18 source-contract tests pass. See [device test report](../hardware/dual-board-device-test-2026-10-06.md) for measurements and remaining AP/browser/BLE qualification limits. No release was published in this check.
