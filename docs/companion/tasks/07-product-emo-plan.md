# Fase 07: Rencana implementasi produk ala EMO

Turunan dari [PRODUCT_DIRECTION.md](../PRODUCT_DIRECTION.md). Satu task = satu sesi. Task yang dikerjakan ditulis ke `TASKS.md` NOW dengan ID-nya. Centang `[x]` di sini hanya bila ada bukti; untuk butir "Bukti perangkat", buktinya harus dari perangkat.

## Aturan kerja untuk semua task

**Library mata yang dipakai build:** `Documents\LivingEyes-merge\LivingEyes` (bukan `Arduino\libraries\LivingEyes`). Build dan host test memeriksa sidik jarinya terhadap `tools/livingeyes-pin.json`. Setiap edit di library wajib:
1. Jalankan `python tools/check.py` di library (`SANITIZE=1` bila menyentuh memori).
2. Periksa output visual sebelum memperbarui `tests/visual.sha256`.
3. Pin ulang: `python tools/livingeyes_fingerprint.py <root>`, lalu tulis hash baru ke `tools/livingeyes-pin.json`.
4. Patuhi `AGENTS.md` library: API publik tetap, tanpa heap di hot loop, renderer tanpa driver/Wi-Fi.

**Host test:**
- Daftarkan test baru di `tests/run_host_tests.ps1:126`.
- Skrip itu juga mencocokkan string sumber persis di `.ino` dan `RoboBrain.h` (baris 46-86). Jangan ubah baris yang dicocokkan kecuali polanya ikut diperbarui.

**Setelan baru menyentuh 4 tempat:**
1. `RuntimeSettings.h`: field, default, load, save.
2. `RoboBehaviorWire.h`: export dan import.
3. `RobotSettingsModel.kt`: key dan `range`.
4. Opsional: `SettingsDashboard.h`.

Kind protokol baru tidak perlu.

**Flash:**
- Backup dulu ke `.verification/`.
- app0 di `0x400000`.
- OTA tidak menulis partisi `model`, jadi model wake word baru butuh flash USB.

**Bukti perangkat:**
- Log serial lewat `python tools/serial_monitor.py COMx`, simpan di `.verification/<tanggal>-<id>/`.
- Tanpa perangkat, task ditutup sebagai "kode + host test; belum uji perangkat".

## Urutan kerja (sama dengan urutan di `TASKS.md` NEXT)

**Fase 0 dan memo keputusan:**
0.2 → 0.1 → 0.3 → 4.0. Memo 4.0 disiapkan lebih awal karena keputusan pemilik butuh waktu.

**Fase 1:**
1. 1.1 → 1.2 → 1.11 → 1.12 → 1.3
2. 1.4 → 1.5 → 1.6 → 1.13 → 1.7 → 1.14
3. 1.8a → 1.8b → 1.9
4. 1.10 (uji penerimaan, butuh perangkat)

**Fase 2:**
2.1 → 2.2 → 2.6 → 2.3 → 2.4 → 2.5

**Fase 3:**
3.1 → 3.2 → 3.3 → 3.4. Butuh kode Fase 1 selesai; tidak perlu menunggu bukti perangkat 1.10.

**Fase 4:**
4.1 → 4.2 → 4.3 → 4.14 → 4.15 → 4.16 → 4.17 → 4.12 → 4.19 → 4.13 → 4.5 → 4.6 → 4.7 → 4.8 → 4.4 → 4.9 → 4.18 → 4.10 → 4.20 → 4.11

**Lompat ke task berikutnya yang tidak terhalang bila:**
- task butuh perangkat yang tidak tersedia;
- task butuh keputusan pemilik (4.0) yang belum ada;
- task butuh izin yang belum diberikan: commit, eFuse, migrasi partisi.

---

## Fase 0: Fondasi

### PD-0.1: Baseline versi LivingEyes
- Butuh: tidak ada. **Perlu izin user untuk commit.**
- Masalah: `LivingEyes-merge/LivingEyes` adalah repo git di branch `feature/emo-oled` tanpa satu commit pun, jadi edit library tidak bisa dibatalkan.
- Isi:
  1. Pastikan sidik jari cocok dengan pin `75dd2768…`.
  2. Commit seluruh isi sebagai baseline.
  3. Jalankan `python tools/check.py` dan catat hasilnya.
- Selesai bila: commit baseline ada, fingerprint == pin, `check.py` hijau.

### PD-0.2: Crash heap corruption S3
- Sudah ada di `TASKS.md` NEXT (hp_s3.log, `Bad tail` PSRAM saat TLS + SettingsSet kind=9).
- Selesai bila: akar masalah ditemukan dan diperbaiki, dan soak 24 jam dengan simpan setelan berulang lulus tanpa reset. **Bukti perangkat.**

### PD-0.3: Log perilaku untuk pembuktian
- Butuh: tidak ada.
- Tambah event `LEV,BEH,...` (lewat `RoboLog.h`) di 4 titik:
  - Reaksi: id, varian, dan waktu sejak edge sentuh sampai `RoboDeskOledSurface::present()` (`.ino:238`).
  - Transisi tidur.
  - Bunyi spontan.
  - Aksi idle.
- Skrip baru `tools/behavior_log_report.py` meringkas: p95 latensi sentuh → present, jumlah ulangan beruntun, bunyi spontan per jam, dan durasi tiap status tidur.
- Selesai bila: format event punya host test (atau test Python), dan laporan berjalan pada log contoh.

## Fase 1: Hidup (offline)

### PD-1.1: Kebijakan siklus energi (logika murni)
- Butuh: tidak ada.
- File baru `RoboSleepCycle.h`:
  - Status: Awake, Drowsy, LightSleep, DeepSleep, Waking.
  - Masukan: now, energi, jam tenang, kehadiran, interaksi terakhir, penahan (navigasi, sesi fokus, alarm/pengingat aktif), dan event (sentuh, wake word, PIR).
  - Aturan sesuai PRODUCT_DIRECTION bagian 5.
- Test `tests/robo_sleep_cycle_test.cpp`:
  - Semua transisi.
  - Jam belum sinkron: jangan tidur karena jam tenang.
  - millis wrap.
  - Penahan mencegah tidur.
  - Dibangunkan tengah malam lalu tidur lagi setelah 20 s.
- Selesai bila: host test lulus dan transisi deterministik dengan jam injeksi.

### PD-1.2: Sambungkan siklus tidur ke firmware
- Butuh: 1.1.
- Di `serviceBrain` (`.ino:1429`):
  - Drowsy: `Clip::Sleepy` dan ekspresi Tired; blok start aktivitas di konteks scheduler (`.ino:1470`).
  - LightSleep: `robot.setActivity(Activity::Sleeping)` (Scene Sleep; energi pulih di lib).
  - Bangun: `robot.wake(now)`, lalu `Clip::Wake`, lalu salam sesuai jam.
  - Wake word, sentuh, dan alarm tetap jalan saat tidur.
- Selesai bila: host test lulus, build `both` lulus, dan string yang dicocokkan `run_host_tests.ps1` tetap utuh.
- **Bukti perangkat:**
  - Ruang kosong 15 mnt berakhir tidur.
  - Sentuh membangunkan dengan transisi.
  - Jam tenang tidak dibangunkan PIR.

### PD-1.3: Proteksi OLED
- Butuh: 1.2.
- Fungsi murni `oledProtectionPlan(state, menit)` menentukan kontras (SSD1306 `0x81`), layar mati/hidup (`0xAE`/`0xAF`), dan offset geser 1 sampai 2 px tiap beberapa menit lewat `FaceLayoutProfile` (eyeYOffset).
- Terapkan di `RoboDeskOledSurface::present()`. Kirim ulang frame penuh setelah layar hidup, karena `oledDelta` hanya mengirim selisih.
- Overlay pairing, notifikasi, dan passkey wajib menyalakan layar.
- Selesai bila: host test untuk plan lulus.
- **Bukti perangkat:** redup di malam hari, padam saat DeepSleep, tidak ada artefak setelah layar menyala.

### PD-1.4: Audit dan lengkapi idle lapis B + anti-ulang
- Butuh: 0.3.
- Langkah 1: rekam 10 mnt log `LEV,BEH` untuk melihat apa yang sudah dilakukan `enableAutonomousBehavior` + `BehaviorSelector` (`.ino:1459/2199`).
- Langkah 2: hanya tambah yang kurang:
  - Aksi kecil tiap 15 sampai 40 s: Scan, PeekL/R, Giggle lalu Shy, menguap bila energi rendah, DoubleBlink.
  - Hanya saat Awake, ada orang, tidak sibuk, dan ≥25 s setelah interaksi.
- Sambungkan `BehaviorNoveltyTracker` (lib, belum dipakai). Panggil `robot.setSeed(esp_random())` dan `setPerformanceVariation`.
- Selesai bila: host test pemilih aksi lulus (tanpa ulangan beruntun, jeda dihormati).
- **Bukti perangkat:** laporan 30 mnt tanpa ulangan beruntun.

### PD-1.5: Variasi reaksi sentuh + latensi
- Butuh: 1.4.
- Tap, double tap, elus, dan sentuh setelah ≥2 jam sendiri masing-masing punya 3 sampai 5 varian (clip, intensitas, nada SFX).
- Tap ≥5x dalam 3 s memicu kesal. Bila berlanjut, ngambek lewat `MindCause::Sulking`: maks 3 mnt, pulih sendiri.
- Hold privasi 1,4 s dan TouchGame (`.ino:1796-1811`) tetap didahulukan.
- Selesai bila: host test untuk detektor tap cepat, batas ngambek, dan prioritas privasi lulus.
- **Bukti perangkat:** p95 sentuh → present ≤100 ms (50 sampel), 20 tap tanpa ulangan.

### PD-1.6: Reaksi IMU lengkap
- Butuh: 1.4.
- Pemetaan event dari `motionAdapter`:
  - Miring: pasangan mata bergeser ke arah miring.
  - Goyang: `Expression::Nervous` (bentuk spiral sudah ada) sebagai "pusing", plus `SonicCue::Shake`.
  - Goyang ≥3x dalam 30 s: kesal.
  - Jatuh: Shock lalu Sad, pulih lebih cepat bila dielus.
  - Ditaruh: lega.
- Selesai bila: host test penghitung goyang berulang lulus.
- **Bukti perangkat:** tiap event menghasilkan reaksi berbeda dan kembali netral. Bila Nervous tidak terbaca sebagai pusing, buat preset Dizzy di lib sebagai task terpisah.

### PD-1.7: Kesal/ngambek terbaca
- Butuh: 1.5.
- Wajah ngambek: Suspicious intensitas rendah, mata menghadap samping, sesekali melirik balik.
- Pulih bertahap lewat Shy, lalu Happy.
- Pagar etika: tidak ada teks atau suara yang membuat pemilik merasa bersalah. Mute, alarm, dan pengingat tetap jalan.
- Selesai bila: host test lulus. **Bukti perangkat:** uji keterbacaan pada 3 orang.

### PD-1.8a: Anggaran bunyi spontan
- Butuh: 0.3.
- Batas ≤6 bunyi spontan per jam tanpa interaksi, dan nol di jam tenang. Bunyi yang dipicu pengguna tetap boleh, dengan quietGain.
- Lokasi: chirp ambient (`.ino:2039`), salam, dan cue aktivitas (`.ino:868`, `1496-1499`).
- Selesai bila: host test penghitung per jam lulus. **Bukti perangkat:** laporan 2 jam.

### PD-1.8b: Cue suara baru (lib)
- Butuh: 0.1.
- Tambah cue "hmph" (ngambek), "pusing", dan "dengkur/mimpi" (sangat pelan) di `SonicCharacter.h`, plus jitter tempo.
- Ikuti alur edit lib (check.py, pin ulang).
- Selesai bila: `check.py` hijau, pin baru tercatat, host test firmware lulus.

### PD-1.9: Baterai rendah/kritis
- Butuh: 1.2.
- Kebijakan murni dengan histeresis (sag speaker tidak boleh memicu):
  - Rendah: sekali per siklus pengosongan, tampak lelah + overlay "cas aku".
  - Kritis: simpan state, paksa DeepSleep, layar padam, audio dan Gemini berhenti.
- Mengubah komentar "advisory only" di `RoboBatteryMonitor.h:6` adalah perubahan kebijakan. Sebelumnya, ukur akurasi ADC terhadap multimeter.
- Selesai bila: host test histeresis lulus. **Bukti perangkat:** kurva tegangan saat speaker aktif.

### PD-1.11: Arbitrasi prioritas + interupsi mulus
- Butuh: 1.2.
- Cocokkan `companion::Priority` (`CompanionCore.h:101-111`) dan dispatch aksi (`.ino:1526-1533`) dengan urutan PRODUCT_DIRECTION bagian 3:
  1. Keselamatan IMU
  2. Pemilik
  3. Emosi kuat
  4. Kebutuhan mendesak
  5. Rutinitas
  6. Aktivitas
  7. Mikro
- Clip yang tersela ditutup dengan ease-out ≤150 ms, tidak dipotong kasar.
- Selesai bila: host test urutan prioritas dan interupsi lulus.

### PD-1.12: Peta emosi tunggal
- Butuh: tidak ada.
- `RoboBrain.h` `express()` (:207-208) dan schema tool `express_character` (:82) mencakup seluruh tabel PRODUCT_DIRECTION bagian 4: tambah kesal, pusing, bersemangat, tenang, mengantuk, beserta intensitas 1 sampai 3.
- Nama mood CharacterMind dan "sulking" dipetakan lewat tabel yang sama.
- Selesai bila: `brain_companion_test` dan `gemini_companion_parser_test` diperluas dan lulus.

### PD-1.13: Reaksi suara keras
- Butuh: 1.4.
- Detektor lonjakan energi dari pipeline mic/VAD. Nonaktif saat bercakap, saat mic mute, dan saat musik terdeteksi.
- Reaksi: kaget lalu waspada sebentar, dengan cooldown.
- Selesai bila: host test detektor dengan sinyal sintetis lulus.
- **Bukti perangkat:** tepuk tangan memicu, percakapan biasa tidak.

### PD-1.14: Mikro hidup (lib)
- Butuh: 0.1, 1.4.
- Napas mata: skala/posisi berdenyut tiap 3 sampai 5 s. Belum ada di lib, jadi tambah di `FaceLife`.
- Ketidaksempurnaan kecil: kedip sesekali telat, lirikan meleset lalu dikoreksi.
- Bersin/cegukan maks 1x per jam.
- Ikuti alur edit lib (check.py, visual.sha256, pin ulang).
- Selesai bila: `check.py` hijau dan host test firmware lulus.
- **Bukti perangkat:** napas terlihat di 128×64. Bila tidak terlihat, matikan lewat profil.

### PD-1.10: Uji penerimaan Fase 1
- Butuh: 1.2 sampai 1.14.
- Jalankan checklist PRODUCT_DIRECTION bagian 14 di perangkat (2 jam tanpa Wi-Fi + 1 malam). Tambahkan pengukuran wake word → Scene Listening <300 ms, dan jam nyala OLED per hari dibanding umur panel.
- Simpan laporan `behavior_log_report.py`.
- Selesai bila: semua butir bagian 14 tercentang dengan path bukti.

## Fase 2: Kenal

### PD-2.1: Ritme harian
- Butuh: 1.2.
- Sambungkan `RoutineDirector` (Morning/Evening) dan `life.setSchedule`. Keduanya ada di lib tetapi belum dipakai.
- Salam pagi sekali per hari: PIR pertama setelah jam tenang selesai.
- Malam: energi turun lebih cepat, suara lebih lembut.
- Selesai bila: host test (sekali per hari, jam belum sinkron) lulus.

### PD-2.2: Milestone dan kangen
- Butuh: 2.1.
- Simpan epoch pertama kali hidup dan epoch terakhir terlihat. NVS maksimal ditulis 1x per hari.
- Hari ke-7, 30, 100, dan 365: Scene Hearts atau Birthday, masing-masing sekali.
- Mati ≥24 jam: bangun "kangen".
- Selesai bila: host test lulus, termasuk reboot, jam mundur, dan jam belum sinkron.

### PD-2.3: Preset kepribadian
- Butuh: tidak ada.
- Setelan `personalityPreset` (Kalem / Ceria / Usil) memetakan `characterIntensity`, `FaceLifeProfile`, frekuensi `SonicDirector` (Low/Normal/Expressive), dan offset sifat awal.
- Sentuh 4 tempat setelan.
- Selesai bila: host test C++ dan test parser Kotlin lulus.

### PD-2.4: Mood live di app
- Butuh: 2.3.
- Ekspor mood, sebab mood, dan status tidur ke app. Pakai dulu kanal status/settings yang sudah ada; jangan tambah kind bila tidak perlu.
- Beranda menampilkan wajah cermin + alasan ("ngantuk, sudah malam").
- Selesai bila: host test lulus dan APK terbangun. **Bukti perangkat.**

### PD-2.5: Jadwal tidur + Tidurkan/Bangunkan
- Butuh: 1.2, 2.4.
- Jam tenang tampil sebagai "Jadwal tidur" (`SetQuietHours` sudah ada).
- Command baru Sleep/Wake di `PhoneCompanionCommand.h`, dengan tombol di Beranda.
- Selesai bila: test C++ dan Kotlin lulus.

### PD-2.6: Momen kecil
- Butuh: 1.2, 2.1.
- Mimpi saat tidur: mata bergetar 1 sampai 2 s, jarang.
- Sesekali memakai ekspresi pagi yang belum dipakai minggu itu.
- Ulang tahun pemilik: Scene Birthday ada di lib tetapi belum dipakai firmware. Tanggal diisi dari app dan disimpan lokal.
- Selesai bila: host test frekuensi dan sekali per tahun lulus.

## Fase 3: Main (setelah kode Fase 1)

### PD-3.1: Mainan suara
- Tool Gemini `play_game{coin|dice|suit}` (`RoboBrain.h:82`). Hasilnya diacak lokal (adil), lalu ditampilkan sebagai ikon OLED + emo-ese.
- Pasang bank `GamePerformances` (sekarang hanya core + extended, `.ino:2192`).
- Pemicu offline lewat TouchGame.

### PD-3.2: Menari mengikuti musik
- Detektor onset/ritme dari frame mic, hanya saat tidak bercakap dan mic tidak mute. Audio tidak disimpan.
- Memicu macro session `SessionKind::Dance`.
- Host test dengan sinyal sintetis. Ukur beban CPU S3.

### PD-3.3: Ekspresi dan elusan dari app
- Command `ShowExpression` dan `RemotePet`, dengan batas laju.
- Test C++ dan Kotlin.

### PD-3.4: Diary mood (opt-in, lokal)
- Ringkasan mood per jam, tanpa transkrip, disimpan di app (`MemoryCacheStore`).
- Ada tombol hapus.

## Fase 4: Siap jual (sebagian menunggu keputusan pemilik di PRODUCT_DIRECTION bagian 15)

| ID | Task | Butuh | Selesai bila |
|---|---|---|---|
| PD-4.0 | Memo keputusan pemilik (PRODUCT_DIRECTION bagian 15): target usia/pasar, akses AI, nama dagang, layar v1, servo. Tiap keputusan berisi opsi, biaya, dampak regulasi, dan task yang terhalang. | – | Pemilik menjawab; jawaban dicatat di PRODUCT_DIRECTION bagian 0 |
| PD-4.1 | Mockup IA 5 tab di `docs/mobile/mockup/android_app_mockup.html` | 2.4 | Disetujui pemilik |
| PD-4.2 | Implementasi 5 tab: `AppModel.kt:43` `TAB_TITLES`, `Shell.kt:53`, layar dipindah. Diagnostik pindah ke Bantuan; tab Kenangan memuat milestone. Teks dipindah ke `strings.xml` (ID/EN), karena sekarang hardcoded. Sinkronkan `ANDROID_APP_SECURITY_FEATURE_ROADMAP.md:21-23`. | 4.1 | APK terbangun, uji parser/privasi lulus, screenshot cocok mockup, target ≥48 dp, font besar |
| PD-4.3 | Onboarding "kelahiran" + memberi nama (`robotName` sudah ada) | 4.2 | Waktu onboarding ≤3 mnt di HP nyata |
| PD-4.4 | Wake word kustom (layanan Espressif). Model ada di partisi `model`, jadi unit lama butuh flash USB. | keputusan nama | Tingkat deteksi dan false-wake diukur |
| PD-4.5 | Kredensial unik per unit + build retail tanpa dashboard. Skrip provisioning `tools/provision_unit.py`. | 0.2 | Tidak ada PIN/AP default publik di unit retail |
| PD-4.6 | Secure Boot v2 + flash encryption. eFuse tidak bisa dibatalkan: uji di 1 unit korban, butuh izin eksplisit. | 4.5 | Rencana + bukti 1 unit, OTA bertanda tangan tetap jalan |
| PD-4.7 | Firmware uji pabrik (role build baru) + `tools/factory_test.py`, keluaran PASS/FAIL JSON per periferal | 0.1 | Lulus di 3 unit |
| PD-4.8 | Release APK: signing, lint, layar penjelasan akses notifikasi, catatan Data Safety | 4.2 | Stage 6 roadmap keamanan app lulus |
| PD-4.9 | Kembalikan pin TX C3 ke GPIO4 (`RoboBuildRole.h:13`) setelah kabel diperbaiki | perbaikan kabel | Soak link UART lulus |
| PD-4.10 | Paket dokumen: panduan cepat ID/EN, keselamatan, draf kebijakan privasi, rencana uji sertifikasi (Komdigi/FCC/CE) | keputusan pasar | Ditinjau pemilik |
| PD-4.12 | Kunci OTA produksi: disimpan offline, prosedur tanda tangan rilis, rencana rotasi (`tools/ota_signing.py`, `FirmwareOtaKey.h`; baca `GITHUB_RELEASES.md` dulu) | 0.1 | Prosedur tertulis dan diuji pada 1 rilis uji |
| PD-4.13 | Kebijakan keamanan publik: kontak pelaporan kerentanan, masa dukungan, SBOM (skrip daftar library + versi + lisensi) | 4.19 | SBOM terbangkit otomatis dari build |
| PD-4.14 | Privasi di perangkat: indikator mic/cloud konsisten, layar persetujuan sebelum audio pertama ke Gemini (belum ada di app), "hapus semua data" tanpa dashboard (memori robot + cache app) | 4.2 | Test Kotlin + C++ lulus. **Bukti perangkat** |
| PD-4.15 | Pagar konten persona (`RoboBrain.h:53-55`): topik tak aman ditolak, plus mode ramah anak bila 4.0 memilih pasar anak | 4.0 | Host test prompt/parser lulus; daftar uji 20 prompt berisiko |
| PD-4.16 | Akses AI produksi sesuai 4.0: UX kunci milik pengguna yang rapi, atau proxy milik penjual dengan autentikasi per unit | 4.0 | Sesuai keputusan, dengan test |
| PD-4.17 | Kanal OTA stable/beta + catatan rilis di app (`GitHubOtaUpdate.h` belum mengenal kanal) | 4.12 | Host test pemilihan kanal lulus; OTA bertanda tangan tetap wajib |
| PD-4.18 | Uji bangku kelistrikan: brownout di volume maksimum + Wi-Fi/BLE, suhu ruang 40 °C, ESD pad sentuh, batas suhu cas | perangkat | Log bukti di `.verification/` |
| PD-4.19 | Audit lisensi (Adafruit, ESP-SR/WakeNet, NimBLE, ArduinoJson, LivingEyes) + cek nama dagang di PDKI | 4.0 | Tabel lisensi + risiko |
| PD-4.20 | Sertifikasi dan produksi eksternal (pemilik/lab): Komdigi, FCC/CE, IEC 62133-2/UN38.3, golden sample, AQL. Agent hanya menyiapkan berkas. | 4.10 | Sertifikat diterima |
| PD-4.11 | Gerbang rilis: soak 72 jam, matriks pairing 5 HP, heap C3 ≥48/24 KiB di bawah beban, power-cycle 500x, OTA/rollback 2 board, semua butir bagian 12 | semua | Bukti lengkap di `.verification/` |

## Backlog v2 (di luar goal)

Servo kepala pan/tilt (`BodyDriverBridge`/`EmbodimentCoordinator` sudah ada di lib), layar lebih besar/berwarna, dok cas, sensor cahaya. Semua menunggu keputusan 4.0.

## Cakupan PRODUCT_DIRECTION → task

| Bagian | Task |
|---|---|
| 0 Keputusan inti | 4.0, 4.19 |
| 1 Celah | tidur 1.1/1.2; variasi & latensi 0.3/1.4/1.5; wake word 4.4; OLED 1.3; baterai 1.9 |
| 2 Jiwa + aturan emas + pagar etika | 1.4, 1.5, 1.7, 1.11, 1.12, 1.14 |
| 3 Arsitektur & prioritas | 1.11 |
| 4 Peta emosi | 1.12, 1.6, 1.7, 1.8b |
| 5 Siklus tidur | 1.1, 1.2, 1.3, 2.6 (mimpi) |
| 6 Idle tiga lapis | 1.4, 1.14 |
| 7 Tabel reaksi | 1.5, 1.6, 1.7, 1.9, 1.13; notifikasi/salam/suhu sudah ada (diuji di 1.10) |
| 8 Emo-ese | 1.8a, 1.8b |
| 9 Ritme, ikatan, momen | 2.1, 2.2, 2.6 |
| 10 Peta fitur | v1: Fase 1 + 4.4; Fase 3; v2: backlog |
| 11 UI app | 2.3, 2.4, 2.5, 4.1, 4.2, 4.3, 4.14 |
| 12A Stabilitas | 0.2, 1.10, 4.11 |
| 12B Keamanan siber | 4.5, 4.6, 4.12, 4.13 |
| 12C Privasi & AI | 4.0, 4.10, 4.14, 4.15, 4.16 |
| 12D Kelistrikan | 1.9, 4.18, 4.20 |
| 12E Regulasi | 4.0, 4.10, 4.20 |
| 12F Umur layar | 1.3, 1.10 |
| 12G Manufaktur | 4.5, 4.7, 4.9, 4.20 |
| 12H Aplikasi | 4.8 |
| 12I Dokumen & dukungan | 4.10, 4.17 |
| 12J Merek & IP | 4.0, 4.4, 4.19 |
| 13 Fase | seluruh dokumen ini |
| 14 Checklist terasa hidup | 1.10 |
| 15 Keputusan terbuka | 4.0 |
