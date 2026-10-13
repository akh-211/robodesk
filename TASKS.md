# TASKS
<!-- Dibaca statusline. Dikelola Claude; jaga format: bagian NOW / NEXT / DONE, item `- [ ]` / `- [x]`. -->

## NOW
- [x] PD-1.8b Cue lib hmph/pusing/dengkur + pin ulang: lib f2aa01d: cue hmph/dizzy/snore + jitter tempo +-8 %; firmware: hmph sekali saat kesal/mulai ngambek, dizzy saat diguncang 3x, snore saat mimpi (ambient, mati di jam tenang/anggaran); check.py + host test + build both lulus 2026-10-13, pin 76eac6cc; belum uji perangkat
## NEXT
- [ ] PD-0.2 S3 crash heap corruption 2026-10-13 (hp_s3.log 388 s: "Bad tail" blok PSRAM, assert multi_heap_free, bt: rdConnect->RoboLinkTlsClient::startTls->mbedtls ecdsa; bersamaan app SettingsSet kind=9): penyebab penulisan di luar batas belum ditemukan; audit heap (roboHeapAudit tiap 2 s + setelah op setelan, log LEV,HEAP,CORRUPT) di-flash ke S3; perlu ulang simpan setelan dari app saat Gemini connect. Juga: OLED langsung ke mata setelah pairing approved (passkey dihapus, banner OK dibuang) belum diverifikasi visual. TERHALANG 2026-10-13: audit statis (PageStager, inbox Settings, ring TCP, Rpc message_, buffer dok DocMax+1, tak ada stack PSRAM) tak menemukan tulis di luar batas; butuh pemilik mengulang simpan setelan dari app saat Gemini connect dgn firmware audit + soak 24 jam
- [ ] PD-4.0 Memo 5 keputusan pemilik (pasar, akses AI, nama, layar, servo). TERHALANG 2026-10-13: memo siap di docs/companion/DECISION_MEMO_PD-4.0.md, menunggu jawaban pemilik (pasar, akses AI, nama, layar, servo)
- [ ] PD-1.9 Baterai rendah/kritis + histeresis. TERHALANG 2026-10-13: RoboBatteryPolicy.h + host test lulus; belum disambung ke firmware karena ambang 3,5/3,3 V butuh kalibrasi ADC vs multimeter di perangkat
- [ ] PD-1.10 Uji penerimaan Fase 1 di perangkat. TERHALANG 2026-10-13: butuh perangkat + penguji (checklist bagian 14, 2 jam tanpa Wi-Fi + 1 malam) dan PD-1.9
- [ ] PD-4.1 Mockup IA 5 tab (persetujuan pemilik). TERHALANG 2026-10-13: usulan di docs/mobile/mockup/android_app_mockup_5tab.html (acuan lama tidak diubah), menunggu persetujuan pemilik
- [ ] PD-4.2 Implementasi 5 tab + strings ID/EN. TERHALANG 2026-10-13: butuh PD-4.1 disetujui
- [ ] PD-4.3 Onboarding kelahiran + beri nama. TERHALANG 2026-10-13: butuh PD-4.2 + uji onboarding <=3 mnt di HP nyata
- [ ] PD-4.14 Privasi: indikator, persetujuan Gemini, hapus semua data. TERHALANG 2026-10-13: butuh PD-4.2
- [ ] PD-4.15 Pagar konten persona + mode anak. TERHALANG 2026-10-13: butuh keputusan PD-4.0 (pasar/usia)
- [ ] PD-4.16 Akses AI produksi sesuai 4.0. TERHALANG 2026-10-13: butuh keputusan PD-4.0 (akses AI)
- [ ] PD-4.17 Kanal OTA stable/beta + catatan rilis. TERHALANG 2026-10-13: butuh PD-4.12
- [ ] PD-4.12 Kunci OTA produksi + rotasi. TERHALANG 2026-10-13: PD-0.1 selesai; kunci produksi harus dibuat dan disimpan offline oleh pemilik (konfirmasi terpisah, baca GITHUB_RELEASES.md)
- [ ] PD-4.19 Audit lisensi + cek nama PDKI. TERHALANG 2026-10-13: butuh keputusan PD-4.0 (nama dagang)
- [ ] PD-4.13 Kebijakan keamanan publik + SBOM. TERHALANG 2026-10-13: butuh PD-4.19
- [ ] PD-4.5 Kredensial unik per unit + build retail. TERHALANG 2026-10-13: butuh PD-0.2 + izin provisioning
- [ ] PD-4.6 Secure Boot v2 + flash encryption (izin eksplisit). TERHALANG 2026-10-13: butuh PD-4.5 + izin eksplisit eFuse/secure boot
- [ ] PD-4.7 Firmware uji pabrik + factory_test.py. TERHALANG 2026-10-13: PD-0.1 selesai; butuh 3 unit fisik
- [ ] PD-4.8 Release APK. TERHALANG 2026-10-13: butuh PD-4.2 + kunci signing rilis milik pemilik
- [ ] PD-4.4 Wake word kustom. TERHALANG 2026-10-13: butuh keputusan nama (PD-4.0) + flash USB partisi model
- [ ] PD-4.9 Pin TX C3 kembali ke GPIO4. TERHALANG 2026-10-13: butuh perbaikan kabel TX C3 (fisik)
- [ ] PD-4.18 Uji bangku kelistrikan & termal. TERHALANG 2026-10-13: butuh perangkat + alat ukur
- [ ] PD-4.10 Paket dokumen & rencana sertifikasi. TERHALANG 2026-10-13: butuh keputusan pasar (PD-4.0)
- [ ] PD-4.20 Sertifikasi & produksi eksternal (pemilik/lab). TERHALANG 2026-10-13: pemilik/lab, butuh PD-4.10
- [ ] PD-4.11 Gerbang rilis. TERHALANG 2026-10-13: butuh semua task + bukti perangkat
- [ ] Butuh HP+robot: I9 uji perangkat, I10 bagian perangkat, A11 (screenshot vs mockup, tak ada emulator), A12/A13 penutupan, lepas dashboard
- [ ] Uji perangkat: pairing app tidak connect; C3 di-flash 2026-10-12 (log kini via USB): jendela pairing terbuka 2x tapi nol `LEV,BLE,CONN` → HP tak pernah sampai link-layer; app kini pindai-lalu-sambung + hapus bond basi HP saat onboarding; Samsung A16 (belum bond) tetap GATT 147 & nol CONN di C3; PC 10/10 OK. Kini: log diagnostik di layar pairing + autoConnect bergantian setelah 147 (APK), C3 coex BLE boost saat jendela pairing. nRF Connect juga gagal → bukan app. Wi-Fi off + coex boost juga gagal (diagnostik dikembalikan, C3 normal di-flash). Sketsa NimBLE polos di C3 (tanpa Wi-Fi, +9 & 0 dBm) juga gagal → Samsung A16↔chip C3, bukan kode. Firmware robot dikembalikan. createBond, HP restart tanpa app, resolusi alamat off, alamat acak baru: semua gagal. "Dulu bisa" = BLE masih di S3 (single-board s/d 5428770); C3 gateway+BLE di C3 sejak 44b25a0. Sketsa BLE HID keyboard di C3: muncul di Setelan Android tapi gagal dipasangkan, nol event di C3 → dugaan RX/antena papan C3. Firmware utama dikembalikan (wifi=1 ble=1). Langkah: tempel HP ke antena / papan C3 lain / HP lain
- [ ] OLED S3: banner pairing penuh (hitung mundur) sampai sesi berakhir/sukses + banner approved; saran sinkron reset pairing robot<->app; belum uji perangkat
- [ ] Sinkron app<->robot (2026-10-12): C3 flag beacon pairing-terbuka (terbukti di udara), app: tahan sambung otomatis saat jendela robot terbuka, tunggu jendela saat pairing, tak sambung bila belum dipasangkan, refresh cache GATT, tombol Pasangkan ulang/Mulai pairing; APK dibangun + host test lulus; BELUM uji perangkat (UI tak berpindah/GATT 0 setelah approve: dugaan cache GATT, butuh log diag layar pairing)
- [ ] Toggle app langsung berlaku (tanpa Simpan) + fitur chat app->robot (jawaban suara/teks OLED/keduanya, setelan terpisah untuk jawaban suara) via BLE->C3->UART->S3->Gemini Live; riwayat chat hanya sesi. Kode+host test (C++ & Kotlin) lulus, APK dibangun, S3+C3 di-flash 2026-10-12 (backup .verification/fw-backup-20261012/*_before_chat.bin; S3 READY wss=1); tinggal uji perangkat (chat, mode teks/suara, toggle, ukuran dokumen setelan <= 4096)

## DONE
- [x] PD-3.3 Ekspresi & elusan dari app: live command 15 Elus + Ekspresi (op 4) dari Beranda, batas laju di S3 1,5 s dan 12/mnt (RemoteRateLimit); kode + host test C++/Kotlin + build both + APK lulus 2026-10-13; belum uji perangkat
- [x] PD-3.4 Diary mood lokal: MoodDiary per jam di app (opt-in, AES-GCM Keystore, 7 hari, tanpa transkrip) + tombol hapus di tab Memori; test Kotlin + APK lulus 2026-10-13; belum uji perangkat
- [x] Commit + push perubahan PD di wip-lokal (izin 2026-10-13): 72b72c4 di origin/wip-lokal (tanpa secrets.h, .verification, m.py, .claude/settings.json)
- [x] PD-0.1 Gabungkan feature/emo-oled ke Engine/Character + check.py hijau + pin ulang (izin 2026-10-13): baseline 4615347 + merge emo-oled cde26a3 (Engine::setEyeStyle/setEmoOledPreset, preset per ekspresi, 5 aksen baru, Character::setEyeStyle; legacy tetap default dan hash visual sama) + check.py hijau (PYTHONUTF8=1 di konsol Windows) + pin ulang; host test + build both lulus 2026-10-13; gaya EmoOled belum dipakai firmware; belum uji perangkat
- [x] PD-1.14 Mikro hidup lib: napas, ketidaksempurnaan, bersin: lib f2aa01d: FaceLife napas 3-5 s, kedip telat dan satu kelopak telat, bersin/cegukan maks 1x/jam dan tidak saat sibuk (opt-in; firmware breath .6, imperfection .5, quirks on); lirikan meleset-lalu-dikoreksi belum (lib tak punya target tatapan yang bisa dibaca); check.py + host test + build both lulus 2026-10-13; belum bukti napas terlihat di 128x64
