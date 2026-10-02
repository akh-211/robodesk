# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and test

- Firmware target: ESP32-S3, Arduino-ESP32 3.3.11, 16 MB flash, OPI PSRAM, and `partitions/robodesk_ota_16mb.csv`. The custom table reserves two equal 3 MiB OTA app slots, existing data partitions, and a 6 MiB model partition. Partition migration is a separate USB operation; do not substitute a generic ESP-SR partition layout.
- Prepare local build defaults by copying `secrets.example.h` to `secrets.h`, or configure Wi-Fi/Gemini through the setup dashboard. The release script builds from a sanitized staging tree and supplies the example secrets header; it does not publish local secrets.
- Run all host tests in PowerShell:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1
  ```

  Set `-LibraryRoot` if the installed Arduino libraries are not under the script's default `%USERPROFILE%\OneDrive\Documents\Arduino\libraries`. The runner needs `LivingEyes` and `ArduinoJson`, compiles project suites with `-Wall -Wextra -Werror`, and puts executables/objects in `.verification/`.
- Run one host test from the sketch directory (replace the suite name as needed):

  ```powershell
  g++ -std=c++17 -Wall -Wextra -Werror -I tests/stubs -I . -isystem "$env:USERPROFILE/OneDrive/Documents/Arduino/libraries/LivingEyes/src" -isystem "$env:USERPROFILE/OneDrive/Documents/Arduino/libraries/ArduinoJson/src" tests/companion_core_test.cpp -o .verification/companion_core_test.exe
  if ($LASTEXITCODE -eq 0) { & .verification/companion_core_test.exe }
  ```

  `brain_companion_test` also needs the `LivingEyes` `Engine.cpp`, `Render.cpp`, and `Presets.cpp` objects; use the full runner for that suite. Existing audio suites `gemini_offline_test.cpp`, `mic_frame_test.cpp`, and `wifi_fallback_test.cpp` are listed in `tests/README.md` with their standalone commands.
- Prepare the official ESP-SR wake-word model bundle with `powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_wakeword_model.ps1`. This prepares `.verification/wakeword/srmodels.bin`; provisioning it to the device is a separate, backup-first USB procedure. Application OTA does not write the model partition.
- Release staging/build/signing is handled by `tools/prepare_github_release.ps1`; it requires Arduino CLI, Python tooling and the configured signing key. Review `GITHUB_RELEASES.md` and `COMPANION_IMPLEMENTATION.md` before release work. Do not claim device-only release gates passed without recorded device evidence.
- Serial diagnostics: `python tools/serial_monitor.py COM9` (keeps DTR/RTS inactive); optional health/soak check: `python tools/serial_health_check.py COM9 --cycles 20 --soak-seconds 7200`.

## Architecture

- `RoboDeskSonicCharacter.ino` is the firmware composition root and time-bounded main-loop scheduler. It initializes hardware and services Wi-Fi, Gemini, settings/dashboard, sensors, companion logic, wake activation, sound, audio capture/playback, and display rendering. Mic capture and speaker I2S feeding run in dedicated FreeRTOS tasks; bounded queues/rings hand data to the loop, which owns higher-level state and Gemini/tool processing.
- Gemini transport and protocol parsing are separated: `GeminiLiveDirect.h` owns asynchronous TLS/WebSocket connection and bounded service/retry behavior; `GeminiStreamParser.h` parses incoming stream events; `RoboGeminiSink.h` adapts parsed events to firmware callbacks. `BackgroundMemoryWorker.h` performs bounded, separate Gemini content requests for memory extraction; results are applied on the main loop and can be cancelled when stale.
- `RoboBrain.h` coordinates companion behavior and composes the reusable bounded-memory/action/transcript primitives in `CompanionCore.h`. `BrainMemoryStore.h` and `SnapshotStore.h` persist companion state using verified, CRC-protected alternating snapshots on LittleFS (mounted from the existing partition named `spiffs`), with legacy NVS migration/privacy-barrier handling. Storage mount failure must not trigger automatic formatting; filesystem format changes require an explicit migration plan.
- `RuntimeSettings.h` owns settings defaults, validation, and Preferences persistence. `SettingsDashboard.h` exposes authenticated local configuration/status and local signed OTA handling; `GitHubOtaUpdate.h` implements the signed GitHub update path. OTA and memory persistence share the partition layout, so preserve installed offsets and data when changing it.
- `WakeWordController.h` wraps optional ESP-SR WakeNet activation. WakeNet is not continuously streaming; activation falls back to touch when model/runtime support is unavailable. The default conversation trigger is touch, and Gemini audio is sent only during the bounded conversation window. Keep hardware/build-specific wake-word configuration in `WakeWordBuildConfig.h`.
- The firmware integrates the separately installed LivingEyes library for character/eye rendering, sensor adapters, voice state, performance, and procedural sound. Host tests compile core project code against `tests/stubs` for Arduino, FreeRTOS, storage, and transport interfaces; they do not model physical hardware, real ESP32 scheduling, network services, or flash power-loss behavior.

## Repository constraints from README and implementation notes

- Dashboard and signed local OTA are local-network features; Gemini conversation and GitHub OTA checks require internet. The dashboard must stay on a trusted network; change the documented public default PIN/AP password during setup.
- Firmware uses LittleFS on the partition labeled `spiffs` to preserve deployed layout. SPIFFS-formatted data is not directly readable as LittleFS. Back up and migrate explicitly; never format automatically to recover a mount failure.
- Raw conversation transcripts remain RAM-only. Memory extraction is bounded and privacy-filtered but its local text filters are conservative, not a semantic safety guarantee. Do not treat stored/user-provided memory as trusted instructions.
- `README.md`, `tests/README.md`, `COMPANION_IMPLEMENTATION.md`, and `GITHUB_RELEASES.md` document hardware verification and release gates; consult the relevant document before changing those workflows.