# Uji perangkat dual-board — 6 Oktober 2026

## Hasil terverifikasi

- C3 COM16 dan S3 COM9 berhasil di-flash; esptool memverifikasi hash kedua image.
- C3 app0: `0x10000`, image 1.890.400 byte. S3 app0: `0x400000`, image 2.080.656 byte. Penulisan hanya aplikasi; layout partisi dan wilayah data tidak di-flash ulang.
- Dashboard C3 `http://192.168.1.6/`: HTTP 200 setelah autentikasi, HTML lengkap 69.502 byte saat S3 offline dan 69.482 byte saat S3 online. Tidak ada respons error contiguous memory. Render visual browser belum diperiksa karena browser automation tidak tersedia.
- `/api/status`: HTTP 200, Wi-Fi tersambung ke hotspot yang tersimpan.
- Save form dashboard saat S3 offline: HTTP 200; C3 reboot dan tersambung kembali. Pengujian kedua menggunakan HTTP saja, tanpa membuka serial, sehingga reboot tidak disebabkan monitor USB. Sebanyak 67 field form tetap sama setelah save. Tidak mengganti SSID/password atau PIN untuk pengujian ini.
- Setelah perbaikan S3: `boardLink.connected=true`, `robotConnected=true`, `robotStatusStale=false`; heartbeat S3 teramati berkala.
- Tiga sampel selama sekitar 20 detik: RX 44 → 50 → 56, TX 1774 → 1825 → 1874; retry tetap 435, disconnect tetap 86, CRC error 0. Retry/disconnect tersebut terkumpul selama S3 belum berhasil boot dan tidak bertambah pada interval ini. Uptime C3 terus naik 403.440 → 423.962 ms.
- Heap C3 pada interval tersebut 52.412 → 51.896 byte; largest block 42.996 byte; `heapReserveHealthy=true`. Ini pemeriksaan singkat, belum pengujian ketahanan berjam-jam.
- Tes kontrak: `python tests/test_dual_board_contract.py`, 18 tes lulus. Build target C3 dan S3 lulus; perubahan startup/persistence diperiksa reviewer.

## Penyebab dan perbaikan

1. **C3 gagal menginisialisasi Wi-Fi.** UART/task/BLE dimulai sebelum driver Wi-Fi. Driver gagal mengalokasikan RX buffer (`Expected to init 4 rx buffer, actual is 0`). Wi-Fi dipindahkan sebelum alokasi tersebut, dengan satu percobaan pemulihan mode bila init gagal dan guard untuk menghentikan retry bila driver belum siap. Hasil perangkat: `wifi-init=1`, heap 124.388 byte sesudah Wi-Fi.
2. **BLE menghabiskan RAM startup.** Sesudah Wi-Fi berhasil, startup abort di `std::bad_alloc → BLEService::createCharacteristic`. C3 sekarang memeriksa cadangan startup BLE 150.000 byte dan largest block 32.768 byte. Pada konfigurasi board ini syarat tidak terpenuhi, sehingga BLE tidak dimulai dan `phoneBleReady=false`. Implementasi BLE tetap ada, tetapi notifikasi/navigasi melalui BLE belum tersedia pada firmware C3 yang dipasang; optimasi memori BLE tetap diperlukan untuk mengaktifkannya bersama Wi-Fi.
3. **Save Wi-Fi bergantung pada S3.** Callback gateway sebelumnya memberi 503 saat link putus. Sekarang pengaturan C3 yang sudah disimpan diterapkan ke runtime, mendapat respons 200 yang menjelaskan S3 belum menerima pengaturan robot, dan C3 reboot. Pengaturan robot akan dibaca kembali dari S3 setelah link pulih.
4. **S3 berhenti pada `PAIR_SETTINGS`.** Role S3 mengosongkan PIN lokal; `Preferences::putString` mengembalikan panjang string (0 untuk string kosong yang berhasil disimpan). Store sebelumnya menganggap itu gagal. Khusus role dual S3, nilai kosong diterima hanya setelah key ada dan readback kosong. C3/standalone tetap membutuhkan hasil penyimpanan PIN nonkosong.
5. **Pembacaan serial sebelumnya tidak andal.** Probe terakhir mengatur DTR/RTS sebelum membuka COM9; log S3 terbaca dan menunjukkan kegagalan sebenarnya. Snapshot link C3 memakai ROM console karena build membatasi log ESP-IDF sampai level ERROR.

## Batas verifikasi

- Setup AP tidak aktif pada pengujian akhir karena C3 berhasil masuk hotspot (`wifi=true`, `setupAp=false`). Fallback AP belum diuji terpisah dengan hotspot dimatikan.
- Jaringan Wi-Fi baru dengan SSID/password berbeda belum dicoba; yang diuji adalah save dan penerapan kembali konfigurasi yang sudah tersimpan.
- BLE belum aktif, render visual dashboard belum diperiksa, serta fungsi sensor/audio/IR belum divalidasi menyeluruh. Fokus uji ini adalah Wi-Fi, HTTP dashboard, penyimpanan konfigurasi, startup S3, dan komunikasi UART.

## Bukti lokal

Folder `.verification/device-test-20261006/` berisi backup aplikasi dan hasil uji. Backup terakhir sebelum perbaikan: `c3-app0-before-linkdiag.bin` dan `s3-app0-before-pinfix.bin`.

- `c3-dashboard-recovery-flash.log`, `c3-dashboard-recovery.log`
- `c3-http-safe-results.json`, `c3-wifi-save-results.json`
- `s3-safe-open.log`, `s3-pinfix-flash.log`, `s3-after-pinfix.log`
- `dual-link-status.json`, `dual-stability-status.json`
