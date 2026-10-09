# Dual-board software verification

Date: 2026-10-04. SDK: Arduino-ESP32 3.3.11, ArduinoJson 7.4.3, pinned LivingEyes 0.15.0-dev.

## Completed checks

- Full host suite: 25 C++ executables and dashboard/boot/source contracts passed. Log: `.verification/dual-board-host/run.log`.
- Python signature-context tests: standalone S3/C3 and paired robot/gateway signatures reject cross-target verification.
- Three source guards cover local USB migration window, reset preparation before settings erasure, and gateway rollback service while reset is pending. These guards do not simulate NVS or physical power loss.
- Final C3 gateway and S3 robot builds passed, with at least 64 KiB spare in each OTA slot.
- Final standalone C3/S3 regression builds passed. Staged header/sketch hashes match the final workspace source for all four profiles (credential substitution excluded).
- S3 robot ELF symbol inspection found no linked `esp_wifi_init`, `esp_wifi_start`, `esp_bt_controller_init` or `esp_bt_controller_enable` definitions.
- PowerShell build/release scripts parsed successfully; `git diff --check` passed.
- BLE transport compiled for the C3 gateway and an S3 probe. ELF callback signatures confirm NimBLE on both installed SDK targets. The retained Bluedroid fallback was not built with a separate Bluedroid SDK configuration.
- Independent review completed. Route dispatch, RPC cache peer session, BLE backend guards, migration gating, and factory reset/rollback ordering findings were addressed.

| Paired profile | Binary bytes | OTA slot bytes | Spare bytes | Static RAM bytes |
|---|---:|---:|---:|---:|
| C3 gateway | 1,782,768 | 1,966,080 | 183,312 | 56,944 |
| S3 robot | 2,043,280 | 3,145,728 | 1,102,448 | 114,848 |
| Standalone C3 | 1,741,040 | 1,966,080 | 225,040 | 88,840 |
| Standalone S3 | 2,740,240 | 3,145,728 | 405,488 | 129,404 |

Static RAM totals are compiler output. They do not measure free heap after Wi-Fi, BLE, TLS, dashboard, audio or PSRAM allocation. Runtime reserve thresholds still require device measurements.

## Hardware remains pending

### C3 BLE bench measurement, 2026-10-06

Diagnostic app0 flash and subsequent production app0 restoration both passed esptool write-hash verification; NVS and partition metadata were preserved. Diagnostic USB logs captured BLE advertising enabled and an active UART link for 45 seconds without recorded brownout/panic. Heap fell from 74,008 bytes before BLE queues to 20,856 after advertising, then to 6,224 with AP and STA active; the minimum was 280 bytes and largest block 3,060 bytes. This fails the combined-load memory requirement. Phone pairing, ANCS, dashboard/TLS/OTA load and 15-minute stability are still unverified. Evidence is in `.verification/c3-ble-runtime-20261006/diagnostic-startup.json`, `flash-diagnostic.log` and `flash-production.log`. Production retains its startup reserve guard.

An inventory outside the restricted sandbox (2026-10-06) found COM16, and esptool verified ESP32-C3 with 4 MB flash. Full ROM-reader flash backup succeeded; the stub reader consistently failed at 0xC6000. Current C3 app0 and partition layout were verified from that backup. Earlier BLE-only startup on S3 triggered brownout and its full firmware was restored. C3 runtime BLE, combined load, pairing/ANCS, power rails, migration/reset interruptions, IR and signed pair OTA still require verification. Follow [the task checklist](DUAL_BOARD_TASKS.md) and [the wiring guide](dual-board-wiring.md).

Migration uses plaintext UART within a 60-second window opened from the S3 USB console. Keep physical board-link access trusted. Radio separation does not prove that the S3 regulator, wiring or shared 5 V source can sustain load.
