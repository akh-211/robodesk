# RoboDesk Sonic Character

ESP32-S3 robot firmware with local dashboard, microphone/speaker audio, Gemini Live integration, and signed OTA firmware updates.

## First setup

1. Install Arduino-ESP32 3.3.11 and the libraries used by the sketch.
2. Copy `secrets.example.h` to `secrets.h` for build defaults, or leave the example values and provision Wi-Fi/Gemini settings from the robot's setup dashboard.
3. Build for ESP32-S3 with 16 MB flash, OPI PSRAM, and the project's `partitions/robodesk_ota_16mb.csv` layout. The initial partition migration requires USB; later signed application updates can use the local dashboard on trusted Wi-Fi.

The example dashboard PIN (`robodesk`) and setup-AP password (`robodesk123`) are public defaults. Change them during first setup and do not expose the HTTP dashboard to untrusted or public networks.

The local dashboard is available at `http://robodesk.local/` or the IP printed in the serial log. Gemini conversation needs an internet connection. The dashboard and signed local OTA upload work without internet when the client and robot share a trusted Wi-Fi network.

## Firmware releases

See [GITHUB_RELEASES.md](GITHUB_RELEASES.md) for preparing public, secret-free GitHub Release assets. Once firmware with the GitHub updater is installed, the dashboard can check and install signed updates over Wi-Fi; no Arduino IDE or USB cable is needed for later releases. `secrets.h`, signing keys, device-specific notes, and compiled firmware are excluded from the public staging tree.
