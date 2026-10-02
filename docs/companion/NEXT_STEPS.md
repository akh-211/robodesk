# Companion Implementation â€” Detailed Execution Checklist

This checklist turns the roadmap into an ordered implementation and qualification sequence. Update `PROGRESS.md` after each gate; mark an item `DONE` only with its listed evidence. Source changes, host tests, a target build, and robot qualification are separate gates.

## Current handoff

- Source baseline is v9 (`709958e`). The companion foundation currently reports LivingEyes activity, keeps 16 terminal outcomes, uses local invitation cues, maps head double-tap to `RepeatedPet`, and shows activity in the dashboard.
- The final 23-suite host run passed, including PIR and preference persistence. `git diff --check` passed.
- The final ESP32-S3 compile passed at 2,732,415 bytes of the 3,145,728-byte app slot, 2,732,560-byte binary, and 168,452 bytes RAM. COM9 enumerates as USB Serial Port (VID 0x1A86 / PID 0x55D3), and the serial health probe sees stable heartbeats. Dashboard/OTA qualification remains pending.
- No release has been published and no robot was flashed yet. LIFE-01/02/03/04 source implementation is complete with bounded macro sessions, and LivingEyes idle micro-behavior is preserved; short intents remain excluded from macro outcome history. Device qualification is the remaining gate.
- LivingEyes remains the only visual behavior director. New work may schedule high-level intent but must not create another idle animation loop.

## Work order and gates

| Order | Work package | Roadmap tasks | Prerequisites | Exit gate |
| --- | --- | --- | --- | --- |
| 0 | Re-establish reproducible source and target build | BASE-01, QUAL-01 | Current source tree and pinned LivingEyes | All 23 host suites and ESP32-S3 target build pass; image below 3,145,728 bytes |
| 1 | Finish behavior priority and dashboard contract | CORE-03, CORE-04, UX-01, UX-02 | Gate 0 | Deterministic priority tests; authenticated dashboard controls report saved/current state |
| 2 | Complete local activity catalog and scheduler | LIFE-01â€“04 | Gate 1 | Bounded activities, deterministic no-repeat scheduler, interruption and outcome tests pass |
| 3 | Complete initiative, acceptance, and touch play | SOC-01â€“04, CHAR-03 | Gate 2 | Accept/reject/no-response limits pass; game starts only after acceptance and cancels safely |
| 4 | Add opt-in preference and routine learning | MEM-01â€“04 | Gate 3 | Versioned persistence, reset/opt-out, migration and failed-write tests pass |
| 5 | Robot qualification and release decision | BASE-02/03, QUAL-02 | Gates 0â€“4 | Physical checklist and OTA recovery evidence complete; then prepare a release candidate |

Do not skip ahead to a firmware release if a required earlier gate is still open. Device qualification can only be completed with the robot present; software and host work should continue while that evidence is unavailable.

## Gate 0 â€” Re-establish the build

### 0.1 Confirm source and dependency baseline

1. Record `git status --short` and the current commit in `PROGRESS.md`.
2. Confirm `tools/livingeyes-pin.json` matches the LivingEyes checkout used by `tests/run_host_tests.ps1`.
3. Run `powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1`.
4. Run `git diff --check`.
5. If either check fails, stop and fix that failure before proceeding to the ESP32 build.

**Pass evidence:** exact command, exit code, LivingEyes fingerprint, and suite count (23) recorded in `PROGRESS.md`.

### 0.2 Run a secret-free ESP32-S3 compile

1. Use a local environment where Arduino CLI can read its installed ESP32 platform and tool packages. The prior attempt failed before compiling because the sandbox denied access to `%LOCALAPPDATA%\Arduino15\packages` and its temp directory. Do not treat that attempt as a firmware failure or a successful build.
2. Stage the current sketch in a temporary directory, excluding `.git`, private secrets, previous builds, and generated firmware. Copy `secrets.example.h` into the staged sketch as `secrets.h`; copy `partitions/robodesk_ota_16mb.csv` as `partitions.csv`.
3. Compile the staged sketch using the pinned LivingEyes library parent and these target options:

   ```text
   FQBN: esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom
   upload.maximum_size: 3145728
   ```

4. Verify the generated partitions contain `app0` at `0x400000`, `app1` at `0x700000`, each with size `0x300000`.
5. Verify the compiled image is smaller than or equal to 3,145,728 bytes. Record the image size, board options, compiler/core versions, library path, and fingerprint.
6. Re-run the LivingEyes fingerprint after compilation. Stop if it changed during the build.

**Pass evidence:** successful compiler exit, exact image byte count, correct partition table, and matching before/after dependency fingerprints. This gate creates no signed release and does not flash the robot.

## Gate 1 â€” Priority rules and dashboard controls

### 1.1 Lock and test the priority order

Implement one explicit priority decision table in shared host-testable logic:

1. Safety recovery and microphone privacy protections.
2. Active user touch/voice interaction and conversation.
3. Urgent reminders and existing safety/comfort alerts.
4. Owner pause, focus, and quiet-mode restrictions.
5. Accepted invitation or running autonomous activity.
6. Baseline LivingEyes idle behavior.

For every pair likely to overlap, define whether the lower item is deferred, paused, cancelled, or ignored. In particular, do not silence existing urgent alerts because a companion activity is running. Do not let an autonomous activity open the microphone or start a cloud request.

**Tests:** table-driven pairwise precedence; activity interrupted by conversation; quiet/focus defers initiative; safety cancels activity/audio; privacy remains effective across restart. Use `millis()` wrap-safe elapsed-time comparisons.

### 1.2 Finish authenticated dashboard behavior

1. Keep read-only status on `/api/status`; all mutations use the existing authenticated same-origin POST guard.
2. Add controls only for implemented features: initiative pause/resume, activity pause/cancel, local sound permission, invitation preference, learning opt-in/reset, and favorite/skip when those data exist.
3. Validate action and value against explicit allowlists. Invalid requests must return a clear rejection and leave settings unchanged.
4. On page load and after each action, render the saved state from robot status; do not make browser-local state authoritative.
5. Keep activity history bounded to 16 entries and exclude transcript/audio data.

**Tests:** authorized valid mutation; missing auth; wrong origin; unknown action/value; persistence failure; refresh consistency; status JSON remains valid at buffer capacity.

## Gate 2 â€” Local activity catalog and scheduler

### 2.1 Catalog (LIFE-01)

Define each activity with an ID, truthful user-facing description, minimum/maximum duration, required resources, sound policy, and interruption policy. Initial candidates:

- Curious Look â€” visual attention only; no object-recognition claim.
- Expression Practice â€” short expressions through existing LivingEyes/action APIs.
- Rhythm Play â€” brief local sound only when allowed and not over speech.
- Daydream â€” explicitly imaginative expression/text; never represented as an observed event.
- Stretch/Reset â€” visual sequence only; no claim that the robot physically moved.
- Rest and Quiet Company â€” low-stimulation states that leave the renderer in control.

Start from the existing LivingEyes `LifeIntent` where it already supplies a meaningful activity. Add a catalog entry only where an existing supported performance or sound can execute it. Do not create a second animation scheduler.

**Tests:** every ID has finite duration and a cancel path; unsupported resource requirements are rejected; speaker playback is not interrupted by ambient rhythm cues.

### 2.2 Deterministic selection (LIFE-02)

1. Define a pure selector that consumes a snapshot of behavior context, preference scores (if available), mood/needs, recent outcomes, quiet/focus state, presence confidence, and monotonic time.
2. Select at most one macro activity. Respect 20â€“90 second duration and 2â€“5 minute minimum choice gap.
3. Avoid immediate repeats; weight choices deterministically from an injected seed or stable tie-breaker so host tests reproduce decisions.
4. Stale sensor data, invalid wall clock, no presence, DND/focus, busy conversation, safety recovery, and owner-disabled initiative must not make an otherwise ineligible choice eligible.
5. Let LivingEyes continue its own idle micro-behavior between macro activities.

**Tests:** same snapshot produces same selection; no duplicate running activities; cooldown and duration boundaries; invalid time and stale sensor cases; wraparound; each blocker; bounded recent-history ring.

### 2.3 Interruption and actual outcome (LIFE-03/04)

1. For each activity, choose pause/resume only if the underlying performance supports it; otherwise cancel and finish safely.
2. On reboot, discard any prior running state and start idle. Never restore an expired activity as running.
3. On cancellation, clear queued local sounds/actions owned by that activity.
4. Record selected, started, paused, resumed, interrupted, completed, and cancelled outcomes truthfully in the fixed 16-entry history.
5. Add a short local description to dashboard and AI turn context only while that state is current/recent. AI may describe it but cannot start an arbitrary action.

**Tests:** every transition and interruption source; no lingering audio; context after reboot, interruption, completion and stale outcome never claims â€œrunningâ€.

## Gate 3 â€” Polite initiative and touch play

### 3.1 Invitation state machine (SOC-01)

1. Specify states: eligible, cue offered, accepted, declined, expired, and suppressed. Preserve the existing maximum four invitations per day and 30-minute interval; a rejection enforces at least a one-hour cooldown.
2. Require probable presence plus idle/eligible context. PIR is not identity. Quiet/focus, busy conversation, privacy-sensitive interaction, no sound permission, safety recovery, and owner pause suppress the cue.
3. The invitation itself stays local: brief visual cue and optional local sound. It must not open the microphone or call Gemini.
4. Make accept/reject explicit and locally observable. No response expires without starting an activity. Reboot must not replay a pending cue or reset daily limits.
5. Invalid wall clock must fail closed for schedule-based invitations until time is trustworthy; monotonic cooldowns remain wrap-safe.

**Tests:** accept, decline, expiry, reboot, invalid clock, quota, cooldown, repeat PIR, DND/focus, sound disabled, and no cloud/microphone side effect.

### 3.2 Touch game (SOC-02)

1. Start only after explicit acceptance; do not treat an ambiguous touch as acceptance if it is also a privacy or conversation control.
2. Use a short local pattern/timing sequence with no new hardware or cloud dependency.
3. Define clear start, success, retry, cancel, and timeout states. Cancel immediately on privacy hold, safety recovery, conversation start, owner pause, or system restart.
4. Store only aggregate completion/favorite/skip signals if learning is enabled; do not store detailed touch timing history.

**Tests:** correct/incorrect sequence, timeout, cancel gesture, each interruption, disabled sound, restart, and no microphone activation.

### 3.3 Presence and other features (SOC-03/04)

1. Debounce PIR and rate-limit a return greeting; dismiss locally. Do not infer identity or face recognition.
2. Coordinate activity/invitation selection with Pomodoro, reminders, comfort alerts, briefing, and notification playback.
3. Defer companion activity during focus/quiet mode and preserve urgent existing alert priority.

**Tests:** sustained/noisy PIR; greeting cooldown; timer/reminder during activity; DND and focus; urgent alert delivery and activity cleanup.

## Gate 4 â€” Optional preference and routine learning

### 4.1 Preference model (MEM-01/02)

1. Add opt-in state with a clear dashboard explanation and a one-action reset.
2. Learn only from explicit favorite/skip or clear acceptance/completion. Do not infer preference from PIR alone or from an ignored invitation.
3. Bound each activity score to a documented integer range and use it as a small scheduling weight, never as a hard requirement.
4. If routine learning is enabled, aggregate counts into eight coarse time-of-day buckets. Store no detailed timestamps, raw presence stream, audio, or transcript.

**Tests:** score saturation, opt-out, reset isolation (reminders/memory preserved), bucket rollover/timezone change, no learning from PIR-only events, and deterministic selector behavior.

### 4.2 Persistence (MEM-03)

1. Use a separate versioned preference payload; do not silently change an existing snapshot wire format.
2. Validate version, length, enum IDs, score range, and checksum before loading. Invalid data falls back to defaults without formatting or deleting unrelated settings.
3. Batch writes to no more than once per 15 minutes except an explicit owner change/reset. A failed write must not report success or change in-memory state inconsistently.
4. Implement safe migration and keep an older valid payload until the new one is confirmed.

**Tests:** default, round trip, old version migration, unknown version, truncated/corrupt payload, write failure, repeated updates within 15 minutes, reset, and reminder preservation.

### 4.3 Truthful turn context (MEM-04)

Add current activity, activity state, and a bounded recent outcome to existing context only when supported by the tracker. Make stale/interrupted/idle status explicit. Keep cloud use tied to a user-initiated turn; never invoke AI to drive unattended behavior.

**Tests:** offline response, completed activity, interrupted activity, reboot, stale status, and user question when no activity is running.

## Gate 5 â€” Robot qualification and release decision

Record the device model/board, current firmware version, source commit, power/network conditions, start/end times, and observed result for each item. If the robot is unavailable, leave the item pending; a build is not a substitute.

### 5.1 Functional checks

- [ ] Dashboard `/api/status` parses repeatedly; no partial JSON or stale activity state.
- [ ] Double-tap and long pet remain distinct; side-touch microphone privacy remains effective.
- [ ] Invitation follows quotas/cooldowns; acceptance starts exactly one activity; no response starts none.
- [ ] Game cancellation works for privacy, conversation, safety recovery, quiet/focus and timeout.
- [ ] User conversation and urgent reminders/alerts take priority without stale activity or sound.
- [ ] Preference opt-in, display, reset, reboot persistence, and reminder isolation behave as specified.

### 5.2 Soak and latency checks

- [ ] Measure touch-to-first-render latency under conversation load; report sample count and p95, target at or below 100 ms.
- [ ] Run two hours offline and record activity selections, durations, gaps, blockers, resets, and unexpected audio.
- [ ] Reboot with microphone privacy enabled; verify privacy remains enabled before any microphone window can start.
- [ ] Run the planned 24-hour soak after the shorter run passes; inspect memory, logs, audio, and status errors.

### 5.3 OTA checks and release gate

- [ ] Verify signed manifest/image before install and confirm redirect policy.
- [ ] Install candidate; confirm successful boot, version, settings/reminder/memory preservation.
- [ ] Exercise the documented recovery path with power maintained; record result and restore state.
- [ ] Confirm compiled image fits the 3,145,728-byte app slot and LivingEyes pin is unchanged.
- [ ] Update `PROGRESS.md` with evidence. Only then prepare a release candidate; publishing and flashing require an explicit release/deployment task.

## Evidence template

Copy one block per test into `PROGRESS.md`:

```text
Task/gate:
Date/time and timezone:
Source commit:
Device/board and current firmware:
Network/power/other conditions:
Command or interaction sequence:
Expected result:
Observed result:
Logs/artifact path:
Pass/fail and follow-up:
```


## Current remaining work

Software acceptance gates are complete: 23 pinned host suites and the ESP32-S3 build pass. Finish the final physical/browser qualification with a backed-up device identity check, verify the dashboard activity start/pause/resume/cancel and privacy/DND gates, then decide on a signed release after the device reports healthy boot and OTA status. Do not call OTA qualification complete based on USB enumeration alone.


### Device gate result (2026-10-02)

COM9 remains readable and the serial health probe passed two six-second samples without reboot or response timeout. The authenticated dashboard briefly returned HTTP 200, then `robodesk.local` stopped resolving and a multicast DNS query received no answer. No firmware was uploaded: OTA reachability, post-update boot, activity controls, and rollback must be verified together while the robot dashboard remains reachable. Resume with a stable client-accessible robot IP/hostname, then install the signed image through the authenticated local dashboard and run the device checklist below.
