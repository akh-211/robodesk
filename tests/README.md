# Verifikasi audio dan offline

Host regression tests memakai kode `GeminiLiveDirect.h` asli; hanya Arduino/TLS/FreeRTOS yang disimulasikan. Tidak menguji jaringan nyata, penjadwalan multicore nyata, atau kualitas suara fisik.

PowerShell, dari folder sketch:

```powershell
New-Item -ItemType Directory -Force .verification | Out-Null
g++ -std=c++17 -Wall -Wextra -I tests/stubs -I . tests/gemini_offline_test.cpp -o .verification/gemini_offline_test.exe
if ($LASTEXITCODE -eq 0) { & .verification/gemini_offline_test.exe }
g++ -std=c++17 -Wall -Wextra -I tests/stubs -I . tests/mic_frame_test.cpp -o .verification/mic_frame_test.exe
if ($LASTEXITCODE -eq 0) { & .verification/mic_frame_test.exe }
```

Skenario: kepemilikan TLS selama task koneksi, pembatalan koneksi, retry backoff, kegagalan membuat task, snapshot konfigurasi, format/ukuran audio, write macet dan write parsial, heartbeat timeout/pong, speaker backpressure, serta timeout setup.

Uji assembler mic mencakup read parsial, reset pause/resume, potongan kadaluarsa, read yang terlalu lama, dan wraparound `millis()`.

Build rilis memakai Arduino-ESP32 3.3.11, target ESP32-S3, flash 16 MB, PSRAM OPI, dan tabel partisi proyek `partitions/robodesk_ota_16mb.csv` dengan dua slot OTA 3 MiB. Letakkan direktori build di folder sementara di luar sketch.

Hasil 26 September 2026: 11 skenario transport dan 5 skenario mic lulus dengan `-Wall -Wextra -Werror`. Migrasi USB v1 sebelumnya boot dengan `micTask=1`. Paket v2 sudah ditulis ke kedua slot pada 2026-09-26 dan esptool memverifikasi hash; log boot v2 belum tertangkap. Build v2 dengan perbaikan self-test audio dan quiesce koneksi Gemini berhasil: image 1.321.136 byte untuk slot 3.145.728 byte, RAM statis 176.132 byte. Simbol ELF mengonfirmasi `verifyRollbackLater` terpasang sebagai strong symbol. Signature release v2 berhasil diverifikasi. Pengujian browser OTA, rollback perangkat nyata, percakapan online, dan kualitas audio fisik belum dilakukan. Gemini `OFFLINE` karena jaringan tidak memiliki internet. Pemeriksaan mic pada boot memvalidasi aktivitas frame/sinyal, tetapi belum menggantikan uji kanal L/R serta kualitas transkrip secara fisik.

## Uji pada robot setelah upload

1. Internet normal: ucapkan beberapa kalimat dekat/jauh dari INMP441, periksa transkrip dan dengarkan respons panjang. Pantau `micDrop`, `txFail`, `starve`, `spkGapMaxUs`, dan `micGapMaxUs` pada log `LEV,STAT`. Counter kumulatif: bandingkan kenaikan antarbaris, bukan hanya nilai total. Tidak boleh terus naik saat jaringan dan perangkat sehat.
2. Lihat `LEV,AUDIO,MIC_SLOT`: slot harus cocok dengan kabel L/R. Jika slot kosong/noisy terpilih, set build flag `ROBODESK_MIC_SLOT=0` untuk L/R ke GND atau `=1` untuk L/R ke 3.3 V. Default `-1` mempertahankan deteksi otomatis. Periksa clipping dan transkrip sebelum mengubah gain.
3. Router tetap hidup, cabut koneksi WAN sebelum boot. Mata, sensor, dashboard lokal, dan suara karakter harus tetap berjalan selama percobaan koneksi. Log koneksi gagal harus memiliki jeda retry, tidak berulang setiap loop.
4. Putuskan WAN setelah ucapan selesai serta saat robot berbicara. Sesi tanpa respons diputus melalui response timeout (15 s setelah Thinking / audio terakhir dan output sudah habis), TX deadline, atau heartbeat (15 s tanpa RX + 10 s tanpa pong, timeout dijeda ketika speaker menahan RX). Setelah itu tidak boleh terjebak Thinking.
5. Putuskan Wi-Fi; sambungkan kembali Wi-Fi/WAN. Pastikan sesi setup baru selesai, mic kembali menerima input baru, dan ucapan lama tidak diputar/dikirim ulang.
6. Ulangi dengan TouchToTalk/WakeWord jika tersedia; pastikan pergantian capture tidak mencampur potongan PCM sebelum/sesudah pause.

Perubahan ini tidak menyediakan model AI offline. Tanpa internet percakapan Gemini tidak tersedia, tetapi layanan lokal robot harus tetap responsif. OTA aplikasi lokal sudah tersedia melalui dashboard; OTA otomatis berbasis manifest internet ditunda sampai Wi-Fi memiliki akses internet.
