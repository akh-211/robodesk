# Verifikasi audio dan offline

Host regression tests memakai kode `GeminiLiveDirect.h` asli; hanya Arduino/TLS/FreeRTOS yang disimulasikan. Tidak menguji jaringan nyata, penjadwalan multicore nyata, atau kualitas suara fisik.

PowerShell, dari folder sketch:

```powershell
New-Item -ItemType Directory -Force .verification | Out-Null
g++ -std=c++17 -Wall -Wextra -I tests/stubs -I . tests/gemini_offline_test.cpp -o .verification/gemini_offline_test.exe
if ($LASTEXITCODE -eq 0) { & .verification/gemini_offline_test.exe }
g++ -std=c++17 -Wall -Wextra -I tests/stubs -I . tests/mic_frame_test.cpp -o .verification/mic_frame_test.exe
if ($LASTEXITCODE -eq 0) { & .verification/mic_frame_test.exe }
g++ -std=c++17 -Wall -Wextra -Werror -I tests/stubs -I . tests/wifi_fallback_test.cpp -o .verification/wifi_fallback_test.exe
if ($LASTEXITCODE -eq 0) { & .verification/wifi_fallback_test.exe }
```

Skenario: kepemilikan TLS selama task koneksi, pembatalan koneksi, retry backoff, kegagalan membuat task, snapshot konfigurasi, format/ukuran audio, write macet dan write parsial, heartbeat timeout/pong, speaker backpressure, serta timeout setup.

Uji assembler mic mencakup read parsial, reset pause/resume, potongan kadaluarsa, read yang terlalu lama, dan wraparound `millis()`.

Tes Wi-Fi memeriksa urutan prioritas termasuk slot kosong, wraparound fallback, simpan/muat NVS, serta kompatibilitas pengaturan lama yang hanya memiliki SSID utama.

Build rilis memakai Arduino-ESP32 3.3.11, target ESP32-S3, flash 16 MB, PSRAM OPI, dan tabel partisi proyek `partitions/robodesk_ota_16mb.csv` dengan dua slot OTA 3 MiB. Letakkan direktori build di folder sementara di luar sketch.

Hasil 26 September 2026: 11 skenario transport dan 5 skenario mic lulus dengan `-Wall -Wextra -Werror`. Migrasi USB v1 sebelumnya boot dengan `micTask=1`. Paket v2 sudah ditulis ke kedua slot pada 2026-09-26 dan esptool memverifikasi hash; log boot v2 belum tertangkap. Build v2 dengan perbaikan self-test audio dan quiesce koneksi Gemini berhasil: image 1.321.136 byte untuk slot 3.145.728 byte, RAM statis 176.132 byte. Simbol ELF mengonfirmasi `verifyRollbackLater` terpasang sebagai strong symbol. Signature release v2 berhasil diverifikasi. Pengujian browser OTA, rollback perangkat nyata, percakapan online, dan kualitas audio fisik belum dilakukan. Gemini `OFFLINE` karena jaringan tidak memiliki internet. Pemeriksaan mic pada boot memvalidasi aktivitas frame/sinyal, tetapi belum menggantikan uji kanal L/R serta kualitas transkrip secara fisik.

## Uji pada robot setelah upload

1. Internet normal: ucapkan beberapa kalimat dekat/jauh dari INMP441, periksa transkrip dan dengarkan respons panjang. Pantau `micDrop`, `txFail`, `starve`, `spkGapMaxUs`, dan `micGapMaxUs` pada log `LEV,STAT`. Counter kumulatif: bandingkan kenaikan antarbaris, bukan hanya nilai total. Tidak boleh terus naik saat jaringan dan perangkat sehat.
2. Lihat `LEV,AUDIO,MIC_SLOT`: slot harus cocok dengan kabel L/R. Jika slot kosong/noisy terpilih, set build flag `ROBODESK_MIC_SLOT=0` untuk L/R ke GND atau `=1` untuk L/R ke 3.3 V. Default `-1` mempertahankan deteksi otomatis. Periksa clipping dan transkrip sebelum mengubah gain.
3. Router tetap hidup, cabut koneksi WAN sebelum boot. Mata, sensor, dashboard lokal, dan suara karakter harus tetap berjalan selama percobaan koneksi. Log koneksi gagal harus memiliki jeda retry, tidak berulang setiap loop.
4. Putuskan WAN setelah ucapan selesai serta saat robot berbicara. Sesi tanpa respons diputus melalui response timeout (15 s setelah Thinking / audio terakhir dan output sudah habis), TX deadline, atau heartbeat (15 s tanpa RX + 10 s tanpa pong, timeout dijeda ketika speaker menahan RX). Setelah itu tidak boleh terjebak Thinking.
5. Putuskan Wi-Fi; sambungkan kembali Wi-Fi/WAN. Pastikan sesi setup baru selesai, mic kembali menerima input baru, dan ucapan lama tidak diputar/dikirim ulang.
6. Ulangi dengan TouchToTalk/WakeWord jika tersedia; pastikan pergantian capture tidak mencampur potongan PCM sebelum/sesudah pause.

Tanpa internet, percakapan Gemini tidak tersedia. Saat jendela percakapan dibuka lewat wake word atau sentuhan, MultiNet English dapat menjalankan 12 ekspresi lokal, memeriksa status, serta pause/resume initiative selama bundle `wn9_hiesp` dan `mn7_en` sudah diprovision pada partisi model. Host test memeriksa command mapping dan transisi controller; uji pengenalan, mic ownership, dan respons fisik tetap harus dilakukan pada ESP32-S3. OTA signed lokal tersedia melalui dashboard. OTA GitHub kini dapat memeriksa manifest dan mengunduh image bertanda tangan melalui HTTPS pada task terpisah; uji redirect dan pemasangan aktual tetap perlu dijalankan pada perangkat yang terhubung ke internet.

## Suite pendamping

`powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1` menjalankan lima belas suite termasuk regresi lama. Default LivingEyes adalah checkout terpisah `%USERPROFILE%\OneDrive\Documents\LivingEyes-merge\LivingEyes`, **bukan** salinan lama di `Arduino/libraries`. Runner memeriksa hash `library.properties` serta semua file `src/` terhadap `tools/livingeyes-pin.json` sebelum mengompilasi. Set `ROBODESK_LIVINGEYES_ROOT` atau gunakan `-LivingEyesRoot` (prioritas lebih tinggi) untuk memilih checkout; `-LibraryRoot` kini hanya untuk ArduinoJson. Checkout merger yang disetujui harus cocok dengan `tools/livingeyes-pin.json`; checkout dengan hash berbeda akan ditolak sampai source delta ditinjau dan pin diperbarui secara eksplisit. Contoh PowerShell dari folder sketch:

```powershell
$eyes = Join-Path $env:USERPROFILE 'OneDrive/Documents/LivingEyes-merge/LivingEyes'
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1 -LivingEyesRoot $eyes -VerificationRoot .verification/merged
# Hanya untuk membandingkan baseline lama saat migrasi, meski tidak cocok dengan pin akhir:
$baseline = Join-Path $env:USERPROFILE 'OneDrive/Documents/Arduino/libraries/LivingEyes'
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1 -LivingEyesRoot $baseline -AllowUnpinnedLivingEyes -VerificationRoot .verification/baseline
```

`-AllowUnpinnedLivingEyes` memerlukan `-LivingEyesRoot` eksplisit, mencetak hash serta peringatan, dan hanya berlaku untuk tes host; **tidak ada** bypass pin pada build rilis. Test proyek dikompilasi dengan `-Wall -Wextra -Werror`; source renderer LivingEyes terpilih dikompilasi terpisah karena warning upstream yang tidak terkait perubahan ini. Gunakan `-VerificationRoot` untuk menghindari menimpa artefak `.verification` yang ada.

Suite pendamping mencakup core, snapshot, brain, parser Gemini, settings, keamanan origin dashboard, kebijakan redirect HTTPS GitHub OTA, dan interaksi lokal. `companion_interaction_test` memvalidasi pembacaan AHT/BMP gagal atau stale, validitas rentang, freshness wraparound, tren lingkungan dengan baseline/cooldown, dan wraparound waktu. Tes brain juga mencakup antrean reminder penuh/gagal, reminder berteks sama, pelacakan outcome invitation agregat, serta clear memory yang mempertahankan reminder. Host tests tidak mensimulasikan HTTP/browser, WakeNet/model, sensor atau audio fisik, flash power-loss, dan layanan online. Cakupan dan gate perangkat ada di [COMPANION_IMPLEMENTATION.md](../COMPANION_IMPLEMENTATION.md).
