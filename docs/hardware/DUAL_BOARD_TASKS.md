# Dual-board implementation tasks

## Software implementation

- [x] Opt-in C3 gateway / S3 robot profiles; retain standalone profiles.
- [x] UART pin map, COBS framing, CRC32, reliable sequence/ACK/retry, heartbeat and peer-session handling.
- [x] Bounded TLS tunnel with S3 Gemini parser/audio buffers; serialize Live/background TLS use.
- [x] Dashboard robot RPC and disconnected status; credentials stay out of ordinary behavior RPC.
- [x] C3 BLE ownership, notification forwarding and S3 allowlist enforcement.
- [x] USB-gated 60-second credential migration, durable C3 import acknowledgement and S3 secret-backup removal.
- [x] Recoverable two-board factory reset with S3 credential-backup tombstone and IR erase.
- [x] C3 time synchronization and S3 elapsed-time fallback.
- [x] Role-bound signed S3 image transfer, inactive-slot verification and ordered pair OTA.
- [x] S3 IR RMT capture/learning/NEC transmit/replay and dashboard controls.
- [x] Build/release preparation tooling and updated wiring documentation.
- [x] Verify all three Wi-Fi SSID/password slots after NVS writes; report the failed slot and remaining NVS entries without exposing credentials.

## Software verification

- [x] UART parser boundary/corruption/noise tests.
- [x] Standalone vs pair signature-context separation tests.
- [x] Behavior wire and full host regression suite pass (25 C++ executables plus source contracts).
- [x] C3 gateway and S3 robot compile with OTA slot margin.
- [x] Standalone C3/S3 regression builds, matched to final staged source hashes.
- [x] Independent review findings resolved and final review complete.
- [x] BLE transport compiles on installed C3/S3 NimBLE SDK configurations.

## Device verification — perform after implementation and software checks

Latest hardware audit (2026-10-06): an inventory outside the restricted sandbox detects COM16, verified by esptool as ESP32-C3 with 4 MB flash. A complete ROM-reader recovery backup is available; stub-reader transfers consistently stopped at 0xC6000 while the ROM reader completed. Earlier S3 BLE-only startup triggered brownout and full S3 firmware was restored. BLE remains on C3. Runtime diagnostics are being prepared; pairing, combined load and signed pair OTA remain unverified.

The signed pair OTA code path is implemented but has not passed device verification. The release builder compiles the C3-owned BLE profile and rejects diagnostic firmware before signing; NimBLE is linked only into the gateway image. A successful local compile does not confirm a physical update.

- [ ] Both board identities, flash/PSRAM and partition layouts verified over USB.
- [ ] UART cross-wiring and separate regulated power branches assembled.
- [ ] First matched-profile flash/bootstrap; capture both board startup/reset reasons. Connect C3 USB for diagnostics; use signed pair OTA only if the running S3 supports the OTA RPC, otherwise connect S3 USB once for bootstrap.
- [ ] Radio-disabled S3 stable with sensors/audio; C3 dashboard accessible alone.
- [ ] Credential migration, reboot/reconnect, privacy and BLE app allowlist exercised.
- [ ] Audio + dashboard + BLE load and C3 heap reserve measured.
- [ ] Signed pair OTA through C3 succeeds; wrong-role/corrupt image and interrupted update/rollback tested.
- [ ] IP5310 low-battery/load tests and at least two-hour combined monitoring pass.

No hardware result is assumed from a host test or a successful build. See [wiring and bring-up](dual-board-wiring.md).

Software evidence and exact image/slot sizes: [verification report](DUAL_BOARD_VERIFICATION.md).
