# Audit lisensi dan nama dagang (PD-4.19)

Tanggal 2026-10-13. Sumber: library yang benar-benar terkompilasi di `.verification/dual-board/{robot,gateway}/build/libraries`, `library.properties` tiap library, dan `mobile/android/*.gradle.kts`. Ini bukan nasihat hukum.

## Firmware (S3 robot + C3 gateway)

| Komponen | Versi | Lisensi | Kewajiban saat dijual | Risiko |
|---|---|---|---|---|
| arduino-esp32 core | 3.3.11 | LGPL-2.1 | Sertakan teks lisensi; karena tertaut statis, sediakan cara relink (objek aplikasi atau sumber) bila diminta | **Sedang**: siapkan paket objek/relink per rilis |
| ESP-IDF (di dalam core: FreeRTOS, lwIP, mbedTLS, NVS, Wi-Fi/BT blob) | sesuai core 3.3.11 | Apache-2.0 / MIT / BSD; blob radio Espressif | Sertakan NOTICE dan teks lisensi | Rendah |
| ESP_SR (WakeNet/MultiNet) + model di partisi `model` | 3.3.11 | Lisensi Espressif (hanya untuk chip Espressif) | Hanya dipakai di ESP32-S3; verifikasi teks lisensi di repo espressif/esp-sr sebelum rilis | Rendah-sedang: model wake word kustom (4.4) punya ketentuan layanan sendiri |
| NimBLE-Arduino (C3) | 2.5.1 | Apache-2.0 | Sertakan LICENSE + NOTICE | Rendah |
| ArduinoJson | 7.4.3 | MIT | Sertakan pemberitahuan hak cipta | Rendah |
| Adafruit GFX Library | 1.12.6 | BSD | Sertakan pemberitahuan | Rendah |
| Adafruit SSD1306 | 2.5.17 | BSD | Sertakan pemberitahuan | Rendah |
| Adafruit BusIO | 1.17.4 | MIT | Sertakan pemberitahuan | Rendah |
| Adafruit AHTX0 | 2.0.6 | BSD | Sertakan pemberitahuan | Rendah |
| Adafruit BMP280 | 3.0.0 | MIT | Sertakan pemberitahuan | Rendah |
| Adafruit Unified Sensor | 1.1.15 | Apache-2.0 | Sertakan LICENSE | Rendah |
| LivingEyes | 0.15.0-dev (`f2aa01d`) | **Tidak ada file lisensi** (author "LivingEyes contributors") | Pemilik wajib memastikan hak penuh atas kode ini (milik sendiri, atau izin tertulis dari kontributor) dan menetapkan lisensinya | **Tinggi** sampai status kepemilikan jelas |

## Aplikasi Android

| Komponen | Versi | Lisensi | Kewajiban |
|---|---|---|---|
| Kotlin, Compose BOM, AndroidX (activity-compose 1.10.1, material3, foundation, ui) | Kotlin 2.1.20, AGP 8.9.2 | Apache-2.0 | Layar "Lisensi sumber terbuka" di app (masuk PD-4.2/4.8) |

## Tindakan sebelum jual
1. Pastikan dan tulis status kepemilikan LivingEyes (tindakan pemilik).
2. Tambah berkas `THIRD_PARTY_NOTICES` di paket firmware dan layar lisensi di app (PD-4.8).
3. Siapkan paket relink LGPL untuk arduino-esp32 per rilis.
4. Jangan pakai aset, nama, atau suara Living.ai/EMO (PRODUCT_DIRECTION bagian 12J).

## Nama dagang "AILI" (pencarian PDKI 2026-10-13)
Pencarian merek "AILI" di pdki-indonesia.dgip.go.id menemukan 7 data:
- **Terdaftar:** kelas 35 (ritel online, toko cat; JID2025104097), kelas 2 (cat, "AILI + LUKISAN"), kelas 7 (mesin; dua data milik Jiangxi Aili New Material Technology).
- **Ditolak:** kelas 8 dan dua di kelas 25.
- **Kelas 9 (elektronik/perangkat lunak) dan 28 (mainan): tidak ada data "AILI".**

Penilaian awal: peluang mendaftar "AILI" di kelas 9 dan 28 terlihat terbuka, tetapi kelas 35 sudah dipakai pihak lain (ritel online). Hindari mendaftar atau beriklan sebagai layanan ritel bernama "AILI". Sebelum produksi: konsultasikan dengan konsultan KI terdaftar (pencarian kemiripan bunyi/visual, mis. "AILY", "AYLI"), lalu ajukan di kelas 9 dan 28 (pertimbangkan 42 untuk aplikasi). PDKI menyatakan hasil pencariannya bukan dasar keputusan final.
