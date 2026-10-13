# TASKS
<!-- Dibaca statusline. Dikelola Claude; jaga format: bagian NOW / NEXT / DONE, item `- [ ]` / `- [x]`. -->

## NOW
- [x] PD-4.13 Kebijakan keamanan publik + SBOM: SECURITY.md (kontak pelaporan + masa dukungan diisi pemilik) + tools/sbom.py CycloneDX otomatis di akhir build_dual_board.ps1 + test_sbom; SBOM di .verification/sbom
## NEXT
- [ ] Pilih gaya mata default: Klasik vs Kontur (EmoOled). TERHALANG 2026-10-13: perbandingan piksel 128x64 di docs/companion/eye_style_compare.html, menunggu pilihan pemilik
- [ ] PD-0.2 S3 crash heap corruption 2026-10-13 (hp_s3.log 388 s: "Bad tail" blok PSRAM, assert multi_heap_free, bt: rdConnect->RoboLinkTlsClient::startTls->mbedtls ecdsa; bersamaan app SettingsSet kind=9): penyebab penulisan di luar batas belum ditemukan; audit heap (roboHeapAudit tiap 2 s + setelah op setelan, log LEV,HEAP,CORRUPT) di-flash ke S3; perlu ulang simpan setelan dari app saat Gemini connect. Juga: OLED langsung ke mata setelah pairing approved (passkey dihapus, banner OK dibuang) belum diverifikasi visual. TERHALANG 2026-10-13: firmware audit heap di-flash 23:08, log soak berjalan di .verification/soak-20261013; menunggu pemilik menyimpan setelan dari app 5-10x saat Gemini connect, lalu 24 jam menyala
- [ ] PD-1.9 Baterai rendah/kritis + histeresis. TERHALANG 2026-10-13: RoboBatteryPolicy.h + host test lulus; pending pemilik (belum ada baterai) untuk kalibrasi ADC vs multimeter
- [ ] PD-1.10 Uji penerimaan Fase 1 di perangkat. TERHALANG 2026-10-13: pending atas permintaan pemilik (uji 2 jam tanpa Wi-Fi + 1 malam)
- [ ] PD-4.1 Mockup IA 5 tab (persetujuan pemilik). TERHALANG 2026-10-13: revisi 2 (lebih modern, ikon garis, cakupan fitur lengkap) di docs/mobile/mockup/android_app_mockup_5tab.html; menunggu persetujuan pemilik
- [ ] PD-4.2 Implementasi 5 tab + strings ID/EN. TERHALANG 2026-10-13: butuh PD-4.1 disetujui
- [ ] PD-4.3 Onboarding kelahiran + beri nama. TERHALANG 2026-10-13: butuh PD-4.2 + uji onboarding <=3 mnt di HP nyata
- [ ] PD-4.14 Privasi: indikator, persetujuan Gemini, hapus semua data. TERHALANG 2026-10-13: butuh PD-4.2
- [ ] PD-4.17 Kanal OTA stable/beta + catatan rilis. TERHALANG 2026-10-13: PD-4.12 selesai, tetapi kanal beta butuh perubahan alur OTA C3 (flash 95 %, sisa 81 KB), tag rilis beta (publikasi per rilis, izin pemilik) dan uji OTA perangkat; kerjakan bersama persiapan rilis
- [ ] PD-4.5 Kredensial unik per unit + build retail. TERHALANG 2026-10-13: butuh PD-0.2 + izin provisioning
- [ ] PD-4.6 Secure Boot v2 + flash encryption (izin eksplisit). TERHALANG 2026-10-13: pemilik minta jangan dijalankan sampai uji keseluruhan OK
- [ ] PD-4.7 Firmware uji pabrik + factory_test.py. TERHALANG 2026-10-13: ditunda pemilik (3 unit)
- [ ] PD-4.8 Release APK. TERHALANG 2026-10-13: butuh PD-4.2 + kunci signing rilis milik pemilik
- [ ] PD-4.4 Wake word kustom. TERHALANG 2026-10-13: nama AILI dipilih; butuh pesanan model wake word kustom ke Espressif (biaya/akun pemilik) + flash USB partisi model
- [ ] PD-4.9 Pin TX C3 kembali ke GPIO4. TERHALANG 2026-10-13: menunggu kabel TX C3 diperbaiki (pemilik akan memberi kabar)
- [ ] PD-4.18 Uji bangku kelistrikan & termal. TERHALANG 2026-10-13: ditunda pemilik (alat uji bangku)
- [ ] PD-4.10 Paket dokumen & rencana sertifikasi. TERHALANG 2026-10-13: draf siap di docs/product/quick-start.md (ID/EN) + docs/product/safety-privacy-certification.md (keselamatan, kebijakan privasi, rencana uji Komdigi); menunggu tinjauan pemilik
- [ ] PD-4.20 Sertifikasi & produksi eksternal (pemilik/lab). TERHALANG 2026-10-13: pemilik/lab, butuh PD-4.10
- [ ] PD-4.11 Gerbang rilis. TERHALANG 2026-10-13: butuh semua task + bukti perangkat
- [ ] Butuh HP+robot: I9 uji perangkat, I10 bagian perangkat, A11 (screenshot vs mockup, tak ada emulator), A12/A13 penutupan, lepas dashboard
- [ ] Uji perangkat: pairing app tidak connect; C3 di-flash 2026-10-12 (log kini via USB): jendela pairing terbuka 2x tapi nol `LEV,BLE,CONN` → HP tak pernah sampai link-layer; app kini pindai-lalu-sambung + hapus bond basi HP saat onboarding; Samsung A16 (belum bond) tetap GATT 147 & nol CONN di C3; PC 10/10 OK. Kini: log diagnostik di layar pairing + autoConnect bergantian setelah 147 (APK), C3 coex BLE boost saat jendela pairing. nRF Connect juga gagal → bukan app. Wi-Fi off + coex boost juga gagal (diagnostik dikembalikan, C3 normal di-flash). Sketsa NimBLE polos di C3 (tanpa Wi-Fi, +9 & 0 dBm) juga gagal → Samsung A16↔chip C3, bukan kode. Firmware robot dikembalikan. createBond, HP restart tanpa app, resolusi alamat off, alamat acak baru: semua gagal. "Dulu bisa" = BLE masih di S3 (single-board s/d 5428770); C3 gateway+BLE di C3 sejak 44b25a0. Sketsa BLE HID keyboard di C3: muncul di Setelan Android tapi gagal dipasangkan, nol event di C3 → dugaan RX/antena papan C3. Firmware utama dikembalikan (wifi=1 ble=1). Langkah: tempel HP ke antena / papan C3 lain / HP lain
- [ ] OLED S3: banner pairing penuh (hitung mundur) sampai sesi berakhir/sukses + banner approved; saran sinkron reset pairing robot<->app; belum uji perangkat
- [ ] Sinkron app<->robot (2026-10-12): C3 flag beacon pairing-terbuka (terbukti di udara), app: tahan sambung otomatis saat jendela robot terbuka, tunggu jendela saat pairing, tak sambung bila belum dipasangkan, refresh cache GATT, tombol Pasangkan ulang/Mulai pairing; APK dibangun + host test lulus; BELUM uji perangkat (UI tak berpindah/GATT 0 setelah approve: dugaan cache GATT, butuh log diag layar pairing)
- [ ] Toggle app langsung berlaku (tanpa Simpan) + fitur chat app->robot (jawaban suara/teks OLED/keduanya, setelan terpisah untuk jawaban suara) via BLE->C3->UART->S3->Gemini Live; riwayat chat hanya sesi. Kode+host test (C++ & Kotlin) lulus, APK dibangun, S3+C3 di-flash 2026-10-12 (backup .verification/fw-backup-20261012/*_before_chat.bin; S3 READY wss=1); tinggal uji perangkat (chat, mode teks/suara, toggle, ukuran dokumen setelan <= 4096)

## DONE
- [x] PD-4.0 Memo 5 keputusan pemilik (pasar, akses AI, nama, layar, servo): jawaban pemilik dicatat di PRODUCT_DIRECTION bagian 0: pasar 14+, kunci Gemini milik pengguna, nama AILI, OLED 0,96", servo v2
- [x] PD-4.12 Kunci OTA produksi + rotasi: kunci produksi P-256 di C:/Users/imipi/RoboDesk-OTA-PRODUCTION-OFFLINE (PKCS8 terenkripsi + frasa sandi terpisah, di luar repo/OneDrive); tools/ota_signing.py init-production-key, sign --key-file, verify --public-header + test; prosedur & rotasi docs/companion/OTA_PRODUCTION_KEY.md; uji 1 image .verification/pd-4.12 lulus; firmware masih kunci dev; TUGAS PEMILIK: salin ke 2 media offline lalu hapus dari PC
- [x] PD-4.15 Pagar konten persona + mode anak: pagar konten umum 14+ di prompt persona + host test; 20 prompt uji di docs/companion/CONTENT_GUARD_TEST_PROMPTS.md; belum diuji dengan Gemini di perangkat
- [x] PD-4.16 Akses AI produksi sesuai 4.0: UX kunci milik pengguna: GeminiKeyHelp (bersihkan tempel, cek AIza/39 karakter), langkah singkat + tombol buka AI Studio di Koneksi dan onboarding; test Kotlin + APK lulus; belum uji perangkat
- [x] PD-4.19 Audit lisensi + cek nama PDKI: docs/companion/LICENSE_AUDIT.md: lisensi firmware/app (risiko: LivingEyes tanpa lisensi, LGPL core perlu paket relink); PDKI AILI: kelas 35/2/7 dipakai pihak lain, kelas 9/28 kosong; sarankan konsultan KI
