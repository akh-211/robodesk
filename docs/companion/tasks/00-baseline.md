# Phase 00 — Baseline and stability

Each task starts `TODO`. Record commands, commit, device, conditions, and observed results in `PROGRESS.md`.

## BASE-01 — Reconcile v9 documentation

- Priority: P0; dependencies: none.
- Compare companion docs, release notes, current source, and v9 tests. Mark implemented, unverified, and unsupported behavior separately; correct stale statements about release status and qualification.
- Verify names, endpoints, settings, storage limits, OTA slot size, and actual voice/phone capabilities against source.
- Done when a reader can tell which claims are source-verified and which require device evidence; no document claims unperformed hardware tests passed.

## BASE-02 — Record device behavior baseline

- Priority: P0; dependencies: BASE-01.
- Record boot, serial diagnostics, touch, OLED render, speaker/mic, PIR, IMU, AHT20, BMP280, Wi-Fi loss/recovery, quiet mode, and microphone privacy behavior.
- Measure touch-to-first-render latency with conversation load; record sensor freshness and reset reasons.
- Done when the baseline includes repeatable steps and identifies each failure or unsupported measurement. Do not change hardware or flash a device without explicit task authorization.

## BASE-03 — Qualify update and persistence recovery

- Priority: P0; dependencies: BASE-01.
- Review signed OTA image verification, partition bounds, boot selection, rollback/recovery, and snapshot migration behavior. Execute device cases when a test device is available.
- Done when the report distinguishes code/build evidence from actual boot, data-preservation, and recovery evidence.
