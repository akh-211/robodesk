# RoboDesk Sonic Character

ESP32-S3 robot firmware with local dashboard, microphone/speaker audio, Gemini Live integration, and signed OTA firmware updates.

## First setup

1. Install Arduino-ESP32 3.3.11 and the libraries used by the sketch.
2. Copy `secrets.example.h` to `secrets.h` for build defaults, or leave the example values and provision Wi-Fi/Gemini settings from the robot's setup dashboard.
3. Build for ESP32-S3 with 16 MB flash, OPI PSRAM, and the project's `partitions/robodesk_ota_16mb.csv` layout. The initial partition migration requires USB; later signed application updates can use the local dashboard on trusted Wi-Fi when the installed firmware supports the update path.

The example dashboard PIN (`robodesk`) and setup-AP password (`robodesk123`) are public defaults. Change them during first setup and do not expose the HTTP dashboard to untrusted or public networks.

The local dashboard is available at `http://robodesk.local/` or the IP printed in the serial log. Gemini conversation needs an internet connection. The dashboard and signed local OTA upload work without internet when the client and robot share a trusted Wi-Fi network.

## ESP32-S3 + ESP32-C3 gateway

The opt-in dual-board build keeps Wi-Fi/BLE/dashboard on the C3 and robot peripherals/audio/companion logic on the S3. Optional Home Assistant, Open-Meteo weather, and read-only iCalendar context can be configured from **Integrasi luar** in the C3 dashboard. See [P3 integration setup and limits](docs/integrations/P3_EXTERNAL_INTEGRATIONS.md) and the [dual-board wiring guide](docs/hardware/dual-board-wiring.md).

## Firmware releases

See [GITHUB_RELEASES.md](GITHUB_RELEASES.md) for preparing public, secret-free GitHub Release assets. Dashboard **GitHub** signed OTA requires the robot to be connected to station/router Wi-Fi with internet access and synchronized time for HTTPS; the setup AP or a local-only network is insufficient. If WakeNet is armed, select Touch-to-talk in the dashboard, save, and allow the robot to reboot before installing. A device still running the old updater (including the published v5 image) has a broken redirect path: do not rely on its GitHub Check/Install buttons to bootstrap a fix. Use a backup-first USB application update, or a local signed dashboard upload if the installed firmware supports it, to install a compatible signed version **greater than v5** with the corrected updater. Only subsequent releases can be checked/installed through that corrected GitHub path, subject to device verification. Local signed dashboard upload on trusted Wi-Fi is separate and does not require internet. The release script stages a build with example credentials and excludes `secrets.h`, signing keys, and previously compiled firmware; review the public asset set before publication.

For USB logs on the ESP32-S3, use `python tools/serial_monitor.py COM9` instead of Arduino IDE's built-in Serial Monitor. The helper leaves DTR/RTS inactive; some USB-UART reset circuits connect those lines to ESP32 EN/GPIO0, and toggling them when the monitor closes can hold the chip in reset.

## Pendamping dengan memori lokal

Implementasi memori 96 fakta/32 pengalaman, delapan pengingat, aktivasi panggilan atau sentuhan, worker ekstraksi terbatas, snapshot terverifikasi, dan dashboard dijelaskan di [COMPANION_IMPLEMENTATION.md](COMPANION_IMPLEMENTATION.md). Roadmap companion aktif, arsitektur aktivitas, dan task implementasi berurutan ada di [docs/companion/ROADMAP.md](docs/companion/ROADMAP.md); status serta bukti kerja ada di [docs/companion/PROGRESS.md](docs/companion/PROGRESS.md). Raw transcript hanya di RAM; default baru memakai WakeNet "Hi ESP" dengan fallback sentuhan. Pengaturan input lama yang selalu menyimak dialihkan ke jendela sentuhan. Audio tidak dikirim di luar jendela percakapan.

Dua TTP223 dibaca terpisah: sensor kepala menggunakan GPIO7 untuk belaian/reaksi afeksi; sensor samping memakai GPIO16 secara default untuk membuka jendela percakapan. Hubungkan pin SIG sensor samping ke GPIO yang dikonfigurasi, atau ubah macro build `ROBODESK_PIN_TOUCH_SIDE` jika kabel memakai GPIO lain. Endpoint dashboard `/api/status` menyertakan nomor pin yang sedang dipakai. Tidak ada mode permainan; sentuhan kepala tetap menjadi input afeksi dan percakapan biasa.

Pembacaan AHT20/BMP280 yang valid dan segar menghasilkan konteks sensor, status diagnostik, dan reaksi lingkungan lokal ringan setelah baseline dan cooldown. BMP280 dipakai untuk tekanan dan tren perubahan, bukan prakiraan cuaca; PIR tetap hanya sensor gerakan/kehadiran, bukan identifikasi. Dashboard menyediakan `Local aggregate interaction diagnostics` (default nonaktif); jika diaktifkan, hanya counter lokal yang dihitung, tidak ada transkrip atau data diagnostik yang dikirim ke cloud.

Saat Gemini tidak tersedia, jendela percakapan setelah wake word atau sentuhan samping memakai MultiNet lokal jika model `wn9_hiesp` dan `mn7_en` dari bundle ESP-SR tersedia pada partisi model. Ucapkan `Show happy`, `Look curious`, `Look shy`, `Show love`, `Look sad`, `Act surprised`, `Start thinking`, `Show agreement`, `Show disagreement`, `Wink`, `Laugh`, `Look sleepy`, `Check status`, `Pause initiative`, atau `Resume initiative`. Aksi ekspresi dan kontrol berjalan lokal; ekspresi/perintah diterima diberi bunyi konfirmasi, sedangkan status ditampilkan singkat di OLED. Saat Gemini online, ucapan tetap ditangani Gemini. Fungsi ini tetap perlu dikualifikasi pada robot fisik.

Model WakeNet memerlukan provisioning pada partisi model yang sudah dicadangkan; OTA aplikasi tidak mengisinya. Firmware memakai LittleFS pada partisi bernama `spiffs`; kegagalan mount tidak memicu format otomatis. Pengujian perangkat serta rilis signed OTA masih mengikuti gate pada dokumen implementasi.
