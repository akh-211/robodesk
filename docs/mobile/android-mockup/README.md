# RoboDesk Bridge — Android UI review 01

**Mockup saja. Tidak mengubah APK atau firmware.**

## Buka untuk review

- Buka [index.html](index.html) dengan browser: klik dua kali file, atau Open File pada Chrome/Edge. Tidak perlu server atau koneksi internet.
- Pilih layar melalui sidebar/dropdown, atau gunakan tombol pada mockup. Tab **Semua layar** menampilkan seluruh 22 layar.
- **Gelap / Terang** mengganti tema. **Cetak / PDF** menampilkan seluruh layar melalui dialog print browser.
- [overview.png](overview.png) adalah lembar visual 12 layar utama. Untuk detail perilaku, gunakan HTML dan [rencana enhancement](../APK_ENHANCEMENT_PLAN.md) sebagai acuan.

## Cakupan 22 layar

| Kelompok | Layar |
|---|---|
| Mulai dan trust | Selamat datang, penjelasan izin, pilih robot, verifikasi |
| Bridge | Beranda, aplikasi pilihan, detail aplikasi, metadata pengiriman, navigasi, privasi |
| Companion | Karakter, aktivitas, detail aktivitas |
| Dukungan | Baterai/background, diagnostik, pengaturan, tentang |
| Pemulihan | Offline, verifikasi gagal, forget, kosong, navigasi unknown/stale |

Warna mengikuti `RoboDesk Dashboard.html`: navy, cyan, violet dan status mint.
Teks awal Indonesia, lima tab utama, wajah OLED dan kartu dengan target sentuh
yang nyaman. HTML memiliki tema terang, focus state, label tombol dan reduced motion.
Ukuran preview desktop 390 × 800; layar yang panjang dapat discroll di dalam frame.

## Simulasi dan keterbatasan

- Semua identitas, angka, isi preview, kode pairing dan koneksi merupakan contoh sintetis.
- Tombol hanya berpindah layar, mengubah pilihan contoh, atau menampilkan pesan simulasi.
- Tidak memanggil Bluetooth, Notification Access, permission Android, jaringan, clipboard, atau perangkat robot.
- Tidak menyimpan credential maupun konfigurasi ke storage browser. Refresh mengembalikan contoh awal.
- Izin dan passkey sesungguhnya nanti memakai dialog sistem Android; bukan toggle HTML atau kode demo di sini.
- Semua fitur keamanan/native controls di mockup adalah usulan, bukan klaim telah terpasang pada APK 1.1.0.
- Overview dibuat dengan built-in `image_gen`, lalu diperiksa dan dikoreksi untuk TTL, penjelasan cloud dan versi. Prompt tersimpan di [IMAGE_PROMPT.txt](IMAGE_PROMPT.txt).
- Browser terintegrasi tidak tersedia saat pembuatan; HTML belum melalui pemeriksaan render/interaksi browser. Tidak ada tes/build APK dijalankan untuk pekerjaan desain ini.

## Urutan rencana untuk ditinjau

1. Pilihan robot, pairing dengan MITM protection, verifikasi identitas dan mutual authentication.
2. Default data minimal, redaksi, lock/pause/revoke dan penyimpanan credential.
3. Release signing dan backup/migration yang aman.
4. UI lengkap, reconnect/ACK, baterai dan diagnostik.
5. Kontrol companion terbatas melalui kanal terautentikasi.
6. Pemeriksaan software dan review keamanan; seluruh pengujian HP/board ditempatkan terakhir.

Rencana keamanan dan fitur untuk review ada di
[ANDROID_APP_SECURITY_FEATURE_ROADMAP.md](../ANDROID_APP_SECURITY_FEATURE_ROADMAP.md).
Status implementasi, detail interface lama, dan gate penerimaan ada di
[APK_ENHANCEMENT_PLAN.md](../APK_ENHANCEMENT_PLAN.md).
