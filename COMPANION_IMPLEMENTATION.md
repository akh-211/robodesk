# Pendamping RoboDesk: implementasi dan verifikasi

Firmware memakai Gemini Live langsung melalui Wi-Fi, dengan default `gemini-3.8-live`. Memori dan perilaku lokal tidak melatih ulang model. Build menggunakan ESP32-S3, Arduino-ESP32 3.3.11, flash 16 MB, dan PSRAM OPI. Kredensial/NVS pengaturan dan offset partisi tetap dipertahankan.

## Perilaku yang tersedia

- Sentuhan samping atau WakeNet membuka jendela percakapan 15 detik; aktivitas dan akhir respons memperpanjangnya. Audio tidak dikirim ke Gemini di luar jendela tersebut. Isyarat proaktif lokal tidak membuka jendela mic atau menghubungi Gemini. Nilai mode lama `AlwaysListening` tetap bisa dibaca, kemudian dialihkan ke sentuhan saat boot atau penyimpanan dashboard.
- Logger memakai antrean terbatas dan task terpisah. Log boleh dibuang; audio/rendering tidak menunggu serial. Heartbeat `LEV,HEARTBEAT,uptime=...` tersedia setiap lima detik. Ini tidak dapat memperbaiki USB-UART yang secara fisik menahan EN dalam reset.
- Timeout respons pertama 12 detik mengembalikan jalur lokal, memberi notifikasi layar/bunyi, dan mempertahankan retry koneksi dengan backoff. Resumption handle kedaluwarsa setelah dua jam; handle tidak resumable/error dihapus. GoAway menunggu idle atau batas waktu, dan cancellation membuang tool yang belum dieksekusi.
- `RoboBrain` memilih tindakan melalui antrean prioritas: pemulihan, pengguna, pengingat, sensor, proaktif, ambience. Percakapan menunda tindakan yang lebih rendah. Intensitas ekspresi diterapkan pada mata, termasuk saat robot berbicara; ambience tidak mengambil alih suara Gemini. Ketuk kepala dua kali memberi interaksi `RepeatedPet`; sentuhan samping tetap untuk percakapan dan privasi mic.
- Inisiatif seimbang memerlukan kehadiran, waktu valid, idle, dan di luar jam tenang. Ajakan dikirim sebagai ekspresi dan bunyi lokal; tidak menghubungi Gemini dan tidak membuka mikrofon. Batasnya satu ajakan per 30 menit serta empat per hari. “Jangan ganggu”, “nanti saja”, atau “diam dulu” memperpanjang cooldown menjadi satu jam. Pengaturan lama tetap berlaku; default baru mengaktifkan isyarat lokal. Jam tenang default 22.00–07.00.

## Memori dan privasi

`CompanionCore.h` menampung 96 fakta, 32 pengalaman, dan delapan pengingat. Delapan giliran transkrip dibatasi 1.024 byte per pembicara termasuk terminator, hanya di RAM; audio tidak direkam. Giliran selesai, terinterupsi, dan terpotong dibedakan. Potongan ASR digabungkan; teks pembicara AI tidak menjadi bukti pengguna.

Worker terpisah memakai `generateContent`, default `gemini-3.5-flash-lite`, setelah empat giliran lengkap atau dua menit idle. Jarak antarpermintaan minimal lima menit; kuota maksimum 12 per hari disimpan sebelum permintaan dikirim. Waktu belum valid menonaktifkan permintaan latar belakang. Request/result berukuran terbatas, TLS terverifikasi, timeout terbatas, dan hasil diterapkan hanya di task loop. Percakapan, OTA, perubahan/hapus memori, atau penonaktifan otomatis membatalkan hasil yang tidak lagi relevan.

Ekstraksi memakai pernyataan pengguna saja. Fakta memerlukan kutipan persis, nilai yang benar-benar ada dalam kutipan, kategori valid, dan pernyataan pribadi yang jelas. Giliran terpotong/terinterupsi, dugaan, jawaban AI, serta indikasi kredensial, kesehatan, identitas, keuangan, atau instruksi berbahaya ditolak oleh filter lokal. Filter ini konservatif berbasis teks dan bukan jaminan klasifikasi semantik untuk semua bahasa/ungkapan. Ringkasan tahap ini berupa **kutipan pengalaman terpilih**, sehingga tidak menambahkan klaim hasil parafrase AI.

Permintaan eksplisit dan entri dashboard dilindungi dari eviction. Saat semua slot dilindungi, tool melaporkan gagal. Koreksi eksplisit (“koreksi” / “sebenarnya”) bisa mengganti fakta terlindungi. Koreksi/hapus juga menghapus konteks pengalaman lama dan membatalkan hasil worker sebelumnya. Semua memori dalam prompt ditandai data pengguna yang tidak boleh diikuti sebagai instruksi.

Snapshot `/brain-a.bin` dan `/brain-b.bin` berada pada filesystem LittleFS di partisi bernama `spiffs`; header memuat versi, generasi, ukuran, dan CRC payload/header. Penulisan slot tidak aktif diverifikasi sebelum dianggap berhasil. Migrasi NVS `robobrain` hanya dilakukan bila tidak ada snapshot valid dan tidak ada privacy barrier; NVS lama dipertahankan sampai snapshot terverifikasi. Privacy barrier di NVS menolak snapshot lama setelah penghapusan/koreksi; salinan pemulihan dan sumber legacy dibersihkan sebelum tool mengaku selesai.

**Tidak ada format otomatis.** Mount gagal tampil sebagai storage tidak sehat; memori legacy dapat dibaca jika belum ada barrier, tetapi penyimpanan baru tidak diakui berhasil. Firmware ini memakai LittleFS pada partisi bernama `spiffs` untuk mempertahankan filesystem yang sudah ada pada perangkat. Volume berformat SPIFFS tidak dapat langsung dibaca sebagai LittleFS dan memerlukan backup serta migrasi eksplisit. Jangan menghapus/menulis seluruh flash untuk mengatasi mount gagal. Format snapshot v1 memuat state dengan ukuran layout yang diperiksa; perubahan layout/versi library pada rilis berikutnya harus menyediakan migrasi format, bukan mengabaikan snapshot lama.

## Tools dan dashboard

Tools lama dipertahankan, ditambah `get_robot_state`, `create_reminder`, `list_reminders`, dan `cancel_reminder`. Status tool membedakan `accepted`, `completed`, ditolak, storage gagal, kapasitas penuh, atau waktu belum valid. Dengan Gemini online, perintah suara natural dapat dipakai, misalnya “berapa suhu sekarang?”, “berapa kelembapannya?”, “robot sedang miring atau bergerak?”, “ingatkan saya minum air 20 menit lagi”, “ingatkan saya setiap hari jam 08:00”, “apa saja reminder saya?”, atau “batalkan reminder nomor tersebut”. Untuk tanggal/waktu ambigu, companion harus meminta klarifikasi; sensor yang stale harus disebutkan sebagai tidak tersedia, bukan ditebak. Tidak ada tool atau mode permainan; sentuhan kepala dan samping tetap dipakai untuk afeksi, membuka jendela percakapan, serta interaksi normal.

Pengingat sekali memakai epoch UTC; rutinitas harian memakai menit lokal. NTP/default UTC+7 memberi waktu Jakarta, dan offset tetap dapat diubah. Waktu lokal tetap bergerak ketika Wi-Fi terputus setelah sinkronisasi. Timer memakai `millis()` dan dibuang setelah cold boot. Alert reminder baru ditandai fired setelah berhasil masuk antrean dan snapshot berhasil ditulis; jika enqueue atau persist gagal, reminder dipulihkan untuk retry. Power loss setelah snapshot berhasil tetapi sebelum alert diputar masih dapat melewatkan satu cue, sehingga tidak ada jaminan exactly-once terhadap kehilangan daya. AHT20 hanya memperbarui nilai dan timestamp setelah `getEvent` berhasil, finite, dan berada dalam rentang fisik. BMP280 menyimpan tekanan hPa yang finite dan berada dalam rentang tervalidasi; nilainya dipakai untuk konteks, diagnostik, dan tren perubahan, bukan prakiraan cuaca. Tren lingkungan lokal memakai 5 sampel baseline, perubahan berulang, freshness, dan cooldown 15 menit; reaksinya visual ringan. Sensor tetap membawa flag validitas dan usia sampel terpisah, termasuk tekanan dan IMU.

Dashboard yang sudah terautentikasi menyediakan daftar/koreksi/hapus memori, pengalaman terbaru, pengaturan memori otomatis/model/kuota, dan pengingat. POST yang mengubah state memerlukan Basic Auth dan origin HTTP yang sama. Tombol “Clear AI memory” mempertahankan pengingat; factory reset menghapus semua state. Endpoint baca: `/api/memory`, `/api/reminders`, `/api/status`. Toggle opsional `Local aggregate interaction diagnostics` (default nonaktif) menampilkan hitungan sensor, usia/validitas pembacaan, cooldown lingkungan, follow-up invitation, dan reminder tertunda/gagal, serta jumlah sampel dan batas atas bucket p95 latensi sentuhan sampai render mata pertama. Counter hanya lokal, reset saat reboot/opt-out, tidak menyimpan transkrip, dan tidak dikirim ke cloud.

Companion Home menambahkan tindakan cepat yang tetap melewati jalur arbitrasi utama: `Open conversation`, `Pause initiative`, `Resume initiative`, dan ekspresi allowlist. Tindakan percakapan dan ekspresi dashboard memiliki cooldown runtime singkat agar klik berulang tidak memenuhi antrean. Pause initiative hanya berada di RAM, menerima durasi terbatas (15 menit, satu jam, atau sampai dilanjutkan), wrap-safe terhadap `millis()`, dan tidak menghalangi sentuhan, WakeNet, percakapan yang diminta pengguna, reminder, atau pemulihan. Status ringkas menampilkan mode input, kesiapan WakeNet, audio/conversation state, wake-window, sinkronisasi jam, quiet/DND, kesehatan storage, jumlah reminder aktif/tertunda, serta freshness sensor; status tidak memuat kredensial, transcript, atau teks memori tak terbatas. Pengalaman dapat dihapus terpisah dari fakta dan reminder. Form reminder menyediakan timer, one-time date/time yang dikonversi ke epoch UTC, dan daily routine dalam `HH:MM`; validasi server tetap menjadi otoritas terakhir.

## WakeNet dan partisi model

Engine ESP-SR diaktifkan untuk core ESP32-S3 yang mendukung model flash. Model `wn9_hiesp` dan dependensi `mn7_en` diperiksa sebelum engine dijalankan. Model tidak tersedia/gagal dimuat mengalihkan input ke sentuhan; tidak pernah mengaktifkan streaming terus-menerus.

Persiapkan bundle resmi SDK tanpa memodifikasi perangkat:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_wakeword_model.ps1
```

Hasilnya `.verification/wakeword/srmodels.bin` dan manifest ukuran/SHA256. Model SDK 3.3.11 yang diperiksa berukuran 3.340.296 byte, muat pada partisi `model` 6 MiB di `0xa00000`. Provisioning model merupakan langkah USB terpisah: verifikasi tabel partisi terpasang dan backup dahulu, lalu tulis **hanya** bundle tersebut ke offset model. OTA aplikasi tidak mengisi partisi model dan tidak mengubah tabel partisi. Jangan mengganti partisi aplikasi dengan layout contoh ESP-SR.

## Verifikasi dan gate rilis

Jalankan semua suite host:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1
```

Parameter `-LivingEyesRoot` memilih checkout LivingEyes; `-LibraryRoot` hanya menyesuaikan lokasi instalasi ArduinoJson; `-VerificationRoot .verification/<run>` memilih output build terisolasi. Enam belas suite menggunakan inti aplikasi dan library asli dengan stub transport/storage. Cakupan mencakup bukti pengguna, parser, batas transkrip, migrasi, partial write/CRC/privacy barrier, reminder queue penuh/gagal/duplikat, clear memory tanpa menghapus reminder, tool/settings, guard same-origin, validitas AHT/BMP, mode suara offline, freshness/cooldown tren lingkungan, lifecycle aktivitas dan gestur sentuh, audio, dan Wi-Fi. Suite host tidak mensimulasikan browser HTTP, penjadwalan ESP32, sensor/audio fisik, atau flash fisik.

Verifikasi renderer legacy pada 2026-09-29: sumber Engine/Presets/Types dikembalikan ke snapshot LivingEyes pre-merger, sementara director, arbitration, idle behavior, dan konteks mood/cause/comfort tetap dipertahankan; fingerprint checkout `afe21dd2700beaf9f119ec0fcba76ae70f803e49a915adac111d41eaa9fcf3c9` cocok dengan pin, dan sepuluh suite host RoboDesk lulus setelah rollback renderer. CMake/CTest belum dapat dijalankan karena `cmake` tidak tersedia di lingkungan ini. Target ESP32-S3 secret-free build terbaru dengan Arduino-ESP32 3.3.11, PSRAM OPI, Flash 16 MB, `PartitionScheme=custom`, dan tabel app0/app1/model yang diverifikasi berhasil dibuat; ukuran image `2.667.840` byte dari batas slot `3.145.728` byte. Image tersebut ditulis langsung ke slot custom `app0` pada `0x400000` menggunakan esptool dan verifikasi hash berhasil; model partition tidak ditulis. Detail path, command, bukti serial, dan gate yang masih tertunda dicatat di `AGENT_HANDOFF.md`.

Untuk pengujian UART pada firmware ini setelah instalasi USB:

```powershell
python tools/serial_health_check.py COM9 --cycles 20 --soak-seconds 7200
```

Tool tidak mem-flash dan menjaga DTR/RTS nonaktif. Setelah 20 siklus, port ditutup selama dua jam, lalu uptime firmware dibandingkan sebelum/sesudah untuk mendeteksi reset. Hasil JSON berada di `.verification/serial-health.json`; isi percakapan/kredensial tidak disimpan. Pengukuran EN/DTR/RTS fisik serta respons mata/sentuhan tetap memerlukan observasi perangkat.

Rilis signed OTA menggunakan tooling yang ada. RoboDesk firmware v9 telah dipublikasikan dengan image, signature, dan manifest yang cocok; commit sumber rilis adalah `709958e`. Ini membuktikan build dan validasi signature pada host, bukan keberhasilan update pada robot. Gate fisik yang masih perlu bukti per perangkat/firmware mencakup boot marker `LEV,FACE,...`, API online, kualitas mic/speaker/echo dan underrun, power cut flash, wake word/model, siklus USB, soak dua jam, serta OTA/rollback/preservasi data. Task health soak sebelumnya dihentikan otomatis karena tekanan memori host dan tidak menghasilkan bukti lulus. Jangan menganggap gate tersebut lulus hanya dari build host atau publikasi release.

Referensi API yang digunakan: [pengelolaan sesi Live](https://ai.google.dev/gemini-api/docs/live-api/session-management), [tools Live](https://ai.google.dev/gemini-api/docs/live-api/tools), [model ringkasan](https://ai.google.dev/gemini-api/docs/models/gemini-3.5-flash-lite), [structured outputs](https://ai.google.dev/gemini-api/docs/structured-output), dan [ESP-SR](https://github.com/espressif/esp-sr).
