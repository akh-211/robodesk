# Android App: Mockup → Aplikasi Penuh (rincian task)

Acuan mutlak: [`mockup/android_app_mockup.html`](mockup/android_app_mockup.html).
Protokol: [`BLE_SETTINGS_PROTOCOL.md`](BLE_SETTINGS_PROTOCOL.md). Kode: `mobile/android/app/src/main/java/com/robodesk/phonebridge/`.

Aturan:
- Teks, urutan, grup, label, dan perilaku layar HARUS sama dengan mockup. Bila protokol/firmware tidak bisa memenuhi satu elemen, JANGAN ubah aplikasi diam-diam: catat di tabel Gap (bawah), lalu ubah mockup lebih dulu dengan persetujuan user.
- Centang `[x]` di sini hanya dengan bukti: host test/APK build untuk logika; HP + robot nyata untuk perilaku BLE (tanpa perangkat = tulis "belum diuji perangkat").
- 1 task = 1 sesi. Urutan gabungan dengan integrasi ada di `ANDROID_ROBOT_INTEGRATION.md` §0. Ringkas: A0 → A1 → A2 → A3 → (A4…A9, A14…A17 boleh paralel, A14–A17 menunggu jalur kontrol baru dari I1) → A10 → A11.

## Keputusan: Jetpack Compose (Material3 hanya sebagai dasar, semua gaya dioverride token mockup)
Alasan: paling dekat ke mockup (kartu bulat, sheet, segmented, animasi, state) dengan kode paling sedikit. Konsekuensi:
- Tambah plugin Kotlin Compose + Compose BOM di Gradle (versi dipasang yang kompatibel dengan Kotlin/AGP proyek); cek `tools/test_android_parser.ps1` & `test_android_privacy.ps1` tetap jalan (logika non-UI tetap di file Kotlin biasa tanpa dependensi Compose).
- `RobotScreens.kt`/`Ui.kt` lama diganti bertahap; logika (`BridgeSettingsClient`, protokol, store) dipertahankan.
- Satu `RobotUiState` (StateFlow) menggerakkan chip, disabled, SaveBar. Tema: `MaterialTheme` dengan colorScheme dari token mockup, font sistem, tanpa dynamic color.

## Token desain dari mockup (dipakai A0)
| Token | Nilai | Token | Nilai |
|---|---|---|---|
| bg | `#0e1116` | accent | `#4fd1c5` |
| panel (nav/sheet) | `#171b22` | accent2 | `#7c9cff` |
| card | `#1e232c` | ok / warn / bad | `#4ade80` / `#fbbf24` / `#f87171` |
| line | `#2a313c` | text / mute | `#e8ecf2` / `#8d97a7` |
| radius card | 16dp | radius tombol/input | 12dp / 10dp |
| padding layar | 16dp | padding card | 14dp |
| judul appbar | 20sp bold | label kecil/note | 11.5sp |
| header card | 12sp UPPERCASE, mute, letterSpacing .06 | | |
Mode: gelap saja (sesuai mockup).

---

## A0. Fondasi desain & komponen Compose (`ui/` baru; `Ui.kt` lama dihapus saat layar terakhir dipindah)
Buat komponen yang meniru kelas CSS mockup, supaya layar berikutnya hanya merakit:
- [x] Setup Compose di Gradle (plugin compose 2.1.20, BOM 2025.02.00, `android.useAndroidX`) + `RoboTheme` (colorScheme, shapes 16/12/10dp, typography) dari token di atas (`ui/RoboTheme.kt`). (APK build lulus 2026-10-11; screenshot vs mockup di A11; belum diuji perangkat)
- [x] `card(title?)`, `row(label, sublabel?, trailing)` (garis pemisah, baris terakhir tanpa garis), `field(label, input)`.
- [x] `switchRow`, `sliderRow` (lebar 130dp, accent), `selectRow` (lebar 140dp), `timeField`, `numberField`, `textField`, `passwordField`.
- [x] Tombol: `primary` (accent, teks gelap), `secondary` (#262d39), `danger` (#4a1c1c / teks #ffb4b4); keadaan disabled 40% opasitas.
- [x] `tag(ok|warn|neutral)`, `chip(connected|off)` dengan titik status.
- [x] `accordionCard` (judul + ▾/▴, default tertutup kecuali "Suara & bicara").
- [x] `SaveBar` (kuning, sticky bawah, "Ada perubahan belum disimpan" + tombol Simpan).
- [x] `ToastHost` (melayang 84dp di atas nav, hilang 2,2 dtk) dan `BottomSheetHost` (latar `#000a`, sudut atas 22dp).
- [x] `ProgressBar` 8dp (accent di atas #12161c).
- Bukti: komponen di `mobile/android/app/src/main/java/com/robodesk/phonebridge/ui/Components.kt` (belum dipakai layar mana pun; `Ui.kt` lama tetap sampai layar terakhir dipindah). `timeField` = input teks HH:MM (bukan dialog jam). APK build lulus; screenshot emulator tiap komponen vs mockup masih menunggu A11.

## A1. Konektivitas robot (`RobotLinkService` + `RobotLink`)
- [x] Foreground service `connectedDevice` memiliki satu GATT + sesi HMAC; pindah dari `RoboNotificationListener` (listener jadi klien). `RobotLinkService.kt`: `RobotLinkCore` (satu-satunya GATT, kode dipindah dari listener), `RobotLinkService`, `RobotLinkBootReceiver`; `RoboNotificationListener` kini hanya `RobotLinkHost` (privasi/notifikasi). APK build lulus, belum diuji perangkat.
- [x] `RobotLink: StateFlow<LinkState>` dengan fase `OFFLINE, SCANNING, CONNECTING, BONDING, HANDSHAKE, READY, RECONNECTING(n), ERROR(sebab)`. Diimplementasi sebagai `Observable<LinkState>` (bukan `StateFlow`, agar logika murni jalan di host test tanpa coroutines; adaptasi ke Compose state di A3). `SCANNING` hanya terdefinisi: pemindaian ada di A2. Host test: pemetaan fase + pengamat.
- [ ] Reconnect: `autoConnect=true` + backoff 1→30 dtk; mulai ulang setelah boot dan Bluetooth dinyalakan. Kode ada (`ReconnectBackoff` host test lulus; `connectGatt(autoConnect=true)`; receiver boot + status Bluetooth), perilaku BLE belum diuji perangkat.
- [x] Sesi berakhir → buang antrean, wipe buffer rahasia, semua tulis gagal `NOT_CONNECTED` (tulis yang sudah terkirim = `UNCERTAIN`, lihat I3). Host test `RobotLinkSchedulerTest`.
- [x] Manifest: `BLUETOOTH_SCAN` (`neverForLocation`), `FOREGROUND_SERVICE_CONNECTED_DEVICE`, `POST_NOTIFICATIONS`, `INTERNET` (cek versi GitHub, A9); permintaan izin runtime lewat alur onboarding (A2), bukan dialog acak. Manifest + `RECEIVE_BOOT_COMPLETED` terpasang, APK build lulus; alur izin runtime menunggu A2.
- [x] Pertahankan `BridgeSettingsClient` + antrean 1-in-flight (antrean lama diganti `RobotRequestScheduler`, I3; tes lama dimigrasi).
- Tes: mesin status & backoff dengan `FakeSettingsTransport` (host); BLE nyata = uji perangkat I9.

## A2. Onboarding & pairing (tanpa dashboard)
_Status 2026-10-11: dikerjakan di `ui/` (Compose), APK `assembleDebug --offline` lulus; belum diuji perangkat dan belum melewati A11 (screenshot vs mockup). `ui/Onboarding.kt`, `RobotScanner.kt`; pindai BLE menggantikan CompanionDeviceManager._
Mockup: overlay `#onb` (6 langkah) (tombol "Lihat onboarding"; juga dibuka dari "Lupakan robot" → konfirmasi → onboarding). Bar langkah 4 segmen.
- [ ] Langkah 1 "Hubungkan RoboDesk": ikon 📡, penjelasan, kartu izin: Perangkat sekitar (diizinkan), Notifikasi (diizinkan), Akses notifikasi HP (opsional); tombol "Mulai mencari". Tag mengikuti status izin nyata dan tombol meminta izin yang belum ada.
- [ ] Langkah 2 "Memindai…": daftar robot hasil pindai service UUID `93de0001-…` (nama `RoboDesk-xxxx`, RSSI, "belum dipasangkan" / nama robot), pilih satu (border accent), tombol Kembali / "Pilih robot ini".
- [ ] Langkah 3 "Buka jendela pairing": instruksi tahan sensor kepala 5 dtk, cincin berdenyut, status "Menunggu jendela pairing terbuka…" → "Pairing terbuka · setujui permintaan sandi di HP" (dari `bleFlags` bit2 + dialog bond Android); tombol Batal / Lanjut (Lanjut aktif setelah jendela terbuka).
- [ ] Langkah 4 "Terhubung": ✅, kartu Bond LESC aktif + Sesi HMAC aktif (benar-benar diverifikasi sebelum tampil), tombol "Lanjut: atur Wi-Fi".
- [ ] Langkah 5 "Wi-Fi robot" (GAP-E): SSID ≤32 + sandi ≤64 → SecretSet slot 0; Lewati / Kirim; sandi tidak pernah dibaca balik.
- [ ] Langkah 6 "Kunci API Gemini": field password → SecretSet tipe 2; Lewati / "Kirim & selesai" → Beranda.
- [ ] Bar langkah 6 segmen (bukan 4).
- [ ] Aplikasi dibuka tanpa robot tersimpan → langsung onboarding; ada robot → langsung shell (A3).
- [ ] Simpan di `TrustedRobotStore`; hapus teks usang "approve in robot dashboard".
- [ ] Indikator jendela pairing dari `bleFlags` bit2 (dipakai juga baris "Jendela pairing" di Koneksi).
- [ ] "Lupakan robot" (Koneksi): sheet "Lupakan robot ini?" (Batal / Lupakan) → hapus bond/kredensial lokal + `forgetLocalState` → onboarding.

## A3. Shell aplikasi (appbar, nav, toast, sheet)
_Status 2026-10-11: dikerjakan di `ui/` (Compose), APK `assembleDebug --offline` lulus; belum diuji perangkat dan belum melewati A11 (screenshot vs mockup). `ui/Shell.kt`, `Sheets.kt`, `AppModel.kt`._
Mockup: `.appbar`, `nav`, `.toast`, `.sheet`.
- [ ] Appbar: tombol kembali ‹ (hanya di sub-halaman Lainnya; kembali ke menu Lainnya) + judul tab + chip kanan ("Terhubung · aman" hijau / "Terputus" merah) dari `LinkState`.
- [ ] Bottom nav 6 tab, urutan & label persis: **Beranda ⌂, Setelan ⚙, Koneksi ⌁, Memori ✎, Ingat ⏰, Lainnya ≡**; tab aktif: ikon dalam pil #173532, warna accent.
- [ ] Back: kembali ke Beranda lalu keluar; status tab dipertahankan saat rotasi.
- [ ] Saat `!READY`: semua kontrol tulis disabled, chip merah, toast "Sesi BLE berakhir · menyambung ulang…".
- [x] Gantikan `MainActivity` menu lama (`MainActivity` = host Compose; `Ui.kt` dan `RobotScreens.kt` lama dihapus; APK build lulus).

## A4. Tab Beranda
_Status 2026-10-11: dikerjakan di `ui/` (Compose), APK `assembleDebug --offline` lulus; belum diuji perangkat dan belum melewati A11 (screenshot vs mockup). `ui/HomeScreen.kt`._
Mockup `#v-home`. Sumber: `StatusSnapshot` (kind 11, schema 2).
- [ ] Kartu hero: wajah animasi (dua pil accent berkedip tiap 4 dtk), nama robot (`robotName`), "RoboDesk · C3 v… / S3 v…", tag "Sesi HMAC aktif" & "Bond LESC".
- [ ] Kartu "Status robot": Link C3↔S3 (+ `linkAge` ms), Wi-Fi (slot + RSSI), Kunci Gemini (configFlags bit0), Waktu aktif, Heap C3/S3 (KiB).
- [ ] Aksi cepat: switch "Matikan mikrofon" (`privacyMicMuted`), "Suara utama" (`masterSound`) → ikut SaveBar; tombol "Reboot…" (sheet) dan "Cek update" (→ tab Lainnya).
- [ ] Banner kuning "Wi-Fi robot belum diatur…" + tombol Atur (→ Koneksi) bila tak ada slot Wi-Fi terkonfigurasi (`configFlags`); banner serupa untuk kunci Gemini belum ada.
- [ ] Kartu "Kontrol langsung": Ekspresi (12 pilihan: happy, curious, shy, love, sad, surprised, thinking, agree, disagree, wink, laugh, sleepy) + "Tampilkan"; tombol "Ajak bicara" (talk), "Jeda robot" (pause), "Lanjutkan" (resume); Pomodoro Mulai/Jeda/Lanjut/Henti dengan status ("berhenti / fokus mm:ss / dijeda") dari status langsung robot. Semua = jalur kontrol baru (lihat ANDROID_ROBOT_INTEGRATION.md I1).
- [ ] Polling StatusSnapshot 3–5 dtk hanya saat Beranda/Lainnya terlihat.

## A5. Tab Setelan (SettingsGet/Set, 9 halaman)
_Status 2026-10-11: dikerjakan di `ui/` (Compose), APK `assembleDebug --offline` lulus; belum diuji perangkat dan belum melewati A11 (screenshot vs mockup). `ui/SettingsScreen.kt`, `NotifyControls.kt`; logika di `RobotSettingsModel.kt` (host test)._
Mockup `#v-set`. Satu dokumen JSON `roboBehaviorImport`; edit lokal, simpan sekali.
- [ ] Muat via `readSettings`; skeleton/loading; gagal → toast pesan status.
- [ ] Grup akordion & isi persis mockup (kunci JSON di kurung):
  - Suara & bicara *(buka default)*: Bicara aktif (`speechEnabled`); Volume (`speakerGainMilli` 50–1000, slider penuh lebar + nilai kanan "0,45" format koma); Mode input (`inputMode`: 2 "Kata bangun" / 1 "Sentuh: ketuk untuk bicara" / 0 "Selalu mendengarkan (lama)"); Suara Gemini (teks, `geminiVoice` ≤31, default Iapetus); Gaya bicara (textarea 3 baris, `speechStyle` ≤240, penghitung "n/240").
  - Karakter & suara karakter: Intensitas (`characterIntensity` segmen 3); SFX karakter lokal (`characterSfx`); Intensitas SFX (`sfxIntensityX100`, slider %); Frekuensi SFX (`sonicFrequency` segmen Rendah/Normal/Ekspresif = 0/1/2); switch terpisah `wakeSfx`, `touchSfx`, `motionSfx`, `notificationSfx`; Volume jam tenang (`quietGainX100`, slider %).
  - Wajah (LivingEyes): `faceLifeEnabled`, `facePupils`, `faceBrows`, `faceAutoBrows`, `faceLashes`, `faceAutoLashes`, `facePupilLessActing`; slider % `faceGazeReachX100`, `faceMicroX100`, `faceSilhouetteX100`; Mulut (`mouthMode` segmen Otomatis/Sembunyi/Selalu = 0/1/2).
  - AI & memori: Nama robot (`robotName` ≤31); **Nama panggilan pemilik** = fakta memori `owner.name` kategori `profile` (MemoryAdd/MemoryList, bukan kunci setelan; sama dengan kolom "Owner preferred name" dashboard); Model Gemini (`geminiModel` ≤47); Model ringkasan (`summaryModel` ≤47, default gemini-3.5-flash-lite); Memori aktif (`memoryEnabled`); Simpan otomatis (`autoMemory`); Reaksi visual proaktif (`proactiveVisual`) dan Isyarat suara proaktif (`proactiveVoice`) sebagai dua switch; Diagnostik interaksi lokal (`interactionMetrics`); Batas tugas latar harian (`backgroundDailyLimit` 0–12).
  - Jam tenang & briefing: `quietHoursEnabled`; Mulai/Selesai (`quietStartMin`/`quietEndMin`, menit↔HH:mm, time picker); `dailyBriefingEnabled`; Waktu briefing (`dailyBriefingMinute`); Zona waktu (`timezoneOffsetMin` −720…840: WIB +420 / WITA +480 / WIT +540 / Kustom).
  - Pomodoro & kenyamanan: Fokus/Istirahat/Panjang (`pomodoroFocusMinutes`/`pomodoroBreakMinutes`/`pomodoroLongBreakMinutes`); `comfortAlertsEnabled`; Suhu min/maks °C (`comfortMin/MaxTempX10`) dan Lembap min/maks % (`comfortMin/MaxHumidityX10`), ×10, validasi min < maks untuk keduanya (firmware menolak bila tidak).
  - Notifikasi HP: switch "Navigasi Maps di layar robot" (`navigationEnabled`); per aplikasi terpasang dua kotak centang Tampil (`notificationAllowlist`) dan Baca (`notificationSpeechAllowlist`); baris "Bank / dompet digital" = tag "diblokir privasi" (tidak bisa diaktifkan); isi disaring privasi di HP; baris kapasitas "n app · x/192 B" (GAP-B) dan penambahan ditolak dengan pesan "Daftar robot penuh · hapus satu aplikasi" bila melebihi 192 B.
  - Mikrofon & VAD (lanjutan): `vadStartX100`, `vadEndX100` (desimal ×noise); `vadEndMs` (hening akhir giliran); `postSpeakGuardMs` (jeda setelah bicara); `wakeFollowupMs`.
- [x] Validasi batas firmware sisi klien (mis. volume 50–1000, zona waktu, panjang teks) sebelum SettingsSet agar tidak berulang status 7 (`RobotSettings.validate`, host test).
- [x] Opsi/rentang nilai diambil dari firmware (`RoboBehaviorWire.h`, `RuntimeSettings.h`), bukan karangan; sinkron dengan G5 (rentang sama dengan `roboBehaviorImport`; `RobotRepositoryTest`).
- [ ] Status "dirty" bersama untuk Setelan dan switch Beranda (mic, suara utama): SaveBar muncul di kedua tab pada perubahan pertama; Simpan = `SettingsSet` berhalaman → baca-ulang → bandingkan (`sameValue`) → toast "Tersimpan & terverifikasi baca-ulang"; gagal → pesan status, data lokal dipertahankan.
- [ ] Setelan yang dulu hanya ada di dashboard dan kini harus ada di app (paritas): seluruh kunci `RoboBehaviorWire.h` yang diekspor dashboard kecuali `schema`, `phonePlatform`, `pomodoroWasActive`, `pomodoroInterrupted` (status internal, dipertahankan). Cek otomatis di A11: tidak boleh ada kunci ekspor tanpa kontrol selain empat itu.
- [x] Kunci tak dikenal/`schema`/`phonePlatform` tidak ditampilkan & dipertahankan utuh saat simpan (`phonePlatform` tak boleh berubah → status 14) (host test).

## A6. Tab Koneksi (SecretSet)
_Status 2026-10-11: dikerjakan di `ui/` (Compose), APK `assembleDebug --offline` lulus; belum diuji perangkat dan belum melewati A11 (screenshot vs mockup). `ui/TabScreens.kt`, `Sheets.kt`._
Mockup `#v-net`.
- [ ] Wi-Fi: 3 slot (utama, cadangan 2, cadangan 3), status "Terpasang/Kosong" dari `configFlags`, tag "terhubung" untuk slot aktif; tombol Ubah/Atur → sheet "Wi-Fi slot" (SSID ≤32, sandi ≤64, "Pertahankan sandi lama", Kosongkan, Kirim). Toast "Terkirim · reboot C3 untuk menerapkan".
- [ ] Kunci Gemini: tag status, field password (alfabet `A-Za-z0-9-_.` ≤96), Kirim kunci, Hapus kunci. Nilai tidak pernah ditampilkan ulang, buffer di-wipe setelah kirim.
- [ ] Pairing & keamanan: HP tepercaya (nama perangkat + "bond LESC aktif"), Jendela pairing (tag), tombol "Lupakan robot" (sheet: robot mencabut kepercayaan dulu, lalu hapus lokal; robot di luar jangkauan → hapus lokal + peringatan, GAP-C), catatan PIN admin (tidak ada lagi).
- [ ] Tanpa tombol "Salin Wi-Fi" (G1): catatan "Isi SSID dan sandi manual di lembar Ubah…"; sheet Wi-Fi punya switch "Tampilkan sandi".

## A7. Tab Memori (kind 13–16)
_Status 2026-10-11: dikerjakan di `ui/` (Compose), APK `assembleDebug --offline` lulus; belum diuji perangkat dan belum melewati A11 (screenshot vs mockup). `ui/TabScreens.kt`._
Mockup `#v-mem`.
- [ ] Daftar "Fakta yang diingat robot (n)" dengan `id` kecil di bawah teks + tombol ✕; kosong → "Belum ada memori."
- [ ] Tarik ulang hanya bila revisi berubah (`MemoryCacheStore`).
- [ ] Tambah fakta: Kunci (≤39) + Nilai (≤159) + Kategori (note/profile/preference/routine/place/event = 0..5) + Tambah; kunci sama = ganti di tempat (toast "Fakta dengan kunci sama diganti"); daftar menampilkan nilai, lalu "kunci · kategori · id"; header "(n/96)"; `ContentRejected` (17) → pesan merah "Ditolak filter privasi robot (kredensial / kesehatan / deret angka panjang). Filter bersifat konservatif, bukan jaminan."; validasi UTF-8/kontrol sisi klien.
- [ ] Hapus semua → sheet konfirmasi "Hapus semua memori?" (Batal / Hapus semua).
- [ ] Tombol "Hapus pengalaman (ringkasan percakapan)" + sheet konfirmasi (setara `/memory/experiences/clear`; jalur baru).
- [ ] Catatan kaki "Robot adalah sumber utama…". Teks memori dirender sebagai data biasa (tanpa interpretasi).

## A8. Tab Ingat (kind 17–19)
_Status 2026-10-11: dikerjakan di `ui/` (Compose), APK `assembleDebug --offline` lulus; belum diuji perangkat dan belum melewati A11 (screenshot vs mockup). `ui/TabScreens.kt`; validasi 'sekali di masa depan' dan batas 8 teruji host._
Mockup `#v-rem`.
- [ ] Daftar "Pengingat (n/8)": teks + "Jenis · waktu" (Sekali = tanggal/jam lokal, Harian = HH:mm, Timer = sisa waktu), ✕ hapus.
- [ ] Form tambah: Jenis (Sekali/Harian/Timer), Waktu/durasi (picker sesuai jenis, bukan teks bebas), Teks 1–111 byte; penuh (8) → toast "Penuh (maks 8)".
- [ ] Sekali harus di masa depan (UTC epoch); validasi sebelum kirim.

## A9. Tab Lainnya (menu + sub-halaman)
_Status 2026-10-11: dikerjakan di `ui/` (Compose), APK `assembleDebug --offline` lulus; belum diuji perangkat dan belum melewati A11 (screenshot vs mockup). `ui/MoreScreens.kt`; A14-A17 ada di file yang sama._
Mockup `#v-more` (menu) dan `#v-s-*` (sub-halaman, tombol ‹ di appbar). Menu, urutan persis: Aktivitas & permainan (A14), Integrasi (A15), Remote inframerah (A16), Tes suara (A17), Diagnostik, Pembaruan firmware, Zona bahaya, Tentang. Tiap butir: ikon, judul, subjudul, ›.
- [ ] Diagnostik: link retry/CRC/putus, umur frame, heap C3 min / blok terbesar, heap S3 min, status BLE; tombol Segarkan → toast "Status diperbarui".
- [ ] Pembaruan firmware (GAP-D, G2): kartu Terpasang / Tersedia; "Cek versi terbaru" = HP mengunduh `manifest-esp32c3.txt` dari GitHub Releases (`github.com/akh-211/robodesk/releases/latest/download/…`), baca `version=`, toast "1.5.0 tersedia · robot memverifikasi ulang saat update" (informasi saja; HP offline/GitHub gagal ditangani); "Mulai update" → sheet konfirmasi "Mulai update firmware?" → Command `ota-start`.
- [ ] Layar "Robot sedang memperbarui": spinner, teks tahap kasar, penghitung detik, chip appbar "Update berjalan · terputus", tanpa tombol tulis; BLE putus itu normal → sambung ulang otomatis → baca `otaState/otaError/otaTargetVersion` → kartu "Hasil update" (Selesai / Sudah terbaru / Gagal + sebab per otaError 1–13, 255). Tidak ada persen live (tidak mungkin; BLE mati selama update). Batas tunggu 10 mnt lalu "belum tersambung, periksa robot".
- [ ] Zona bahaya: Reboot (sheet: switch Robot S3 / Gateway C3, peringatan BLE putus) dan Reset pabrik (sheet: ketik `WIPE` → tombol aktif; token `WIPE`+session id) + catatan kembali ke onboarding.
- [ ] Tentang: versi aplikasi (BuildConfig), "Settings v1 · schema 3", "Dashboard web: tidak dipakai".
- [ ] Ganti PIN admin dihapus (tanpa dashboard otorisasi = bond + sesi HMAC); jangan ada UI PIN.

## A14. Lainnya › Aktivitas & permainan (pengganti companion dashboard)
Mockup `#v-s-act`. Jalur: kontrol baru (I1).
- [ ] Aktivitas robot: pilihan 13 (curious_look, expression_practice, rhythm_play, daydream, stretch_reset, rest, quiet_company, pixel_doodle, watch_room, touch_play, rhythm_improv, focus_company, calm_breathing; label Indonesia seperti mockup) + Mulai / Jeda / Lanjut / Lewati / ★ Favorit / Batalkan / "Hapus riwayat aktivitas".
- [ ] Permainan sentuh: Terima / Batalkan (aktif hanya saat robot mengajak, dari status langsung).
- [ ] Pembelajaran preferensi: switch (`learning_enable`) dan "Atur ulang pembelajaran" (`learning_reset`, dengan konfirmasi).
- [ ] Status aktif (aktivitas berjalan/dijeda) dari status langsung; tombol menyesuaikan status.

## A15. Lainnya › Integrasi (pengganti `/integrations`)
Mockup `#v-s-int`. Jalur: Integrations get/set/refresh/clear/status (I1).
- [ ] Home Assistant: alamat dasar (≤127, https/http sesuai validasi firmware), token akses write-only (≤191; tag "terpasang"; kosong = pertahankan), 3 entitas (≤47).
- [ ] Cuaca: switch aktif, lintang (−90..90), bujur (−180..180) desimal; validasi sama dengan firmware.
- [ ] Kalender: URL https ≤255 + tombol "Hapus kalender".
- [ ] Status penyedia: cuaca, nilai tiap entitas, "terakhir diperbarui"; tombol Simpan, Segarkan (antre; "sibuk, coba lagi" bila 409), Hapus semua (konfirmasi).
- [ ] Nilai tersimpan lama tetap dipakai; token tidak pernah ditampilkan.

## A16. Lainnya › Remote inframerah
Mockup `#v-s-ir`. Jalur: kontrol IR (I1).
- [ ] Kirim NEC: kode hex 8 digit (validasi) + "Kirim NEC".
- [ ] Profil terpelajar (maks 4): daftar tersimpan, nama profil (≤15; huruf/angka/spasi/-/_), "Pelajari 10 detik" dengan hitung mundur dan pesan hasil, "Putar profil".

## A17. Lainnya › Tes suara
Mockup `#v-s-snd`. Jalur: kontrol tes suara (I1).
- [ ] Grid 3 kolom, 12 tombol: wake, happy, curious, affection, thinking, surprise, sad, sleepy, error, success, pickup, shake; toast "Suara: nama"; catatan volume setelan.

## A10. Status, error, dan kondisi kosong
_Status 2026-10-11: `UiErrors` (status 1-23, timeout), `LinkNotices` + `NoticeBar` di bawah appbar (robot belum dipilih, izin ditolak, Bluetooth mati, update, sesi putus, S3 menyambung, keamanan, 3x handshake gagal = "disetel untuk iPhone / tak dipercaya: reset pabrik"), banner iPhone di Beranda, `canWrite` mati saat tidak READY/update. Teruji host (`RobotLifecycleTest.kt`), APK build lulus; belum diuji perangkat dan belum melewati A11. Bar notifikasi ini belum ada di mockup (A13: mockup menyusul). Robot mode iPhone diam saat handshake, jadi dideteksi lewat penghitung handshake gagal._
- [x] Peta status 1–17 → toast Indonesia (Busy, RobotUnavailable, SequenceError, CrcMismatch, InvalidSettings, StorageFailed, UpdateBusy, ConfirmRequired, Forbidden, TooLarge, ContentRejected, …); timeout = "Tidak ada respons".
- [x] Robot dalam mode iPhone (`phonePlatform=1`, dulu hanya bisa diubah di dashboard): handshake/sesi ditolak → layar "Robot disetel untuk iPhone · lakukan reset pabrik dari robot (sentuh/serial) lalu pasangkan ulang"; aplikasi tidak bisa mengubahnya (status 14 Forbidden).
- [x] Keadaan: Bluetooth mati, izin ditolak, robot belum dipilih, sesi putus, loading, kosong.
- [x] Tidak ada rahasia/transkrip di log; tombol tulis disabled saat tidak READY.

## A11. Kesesuaian mockup (gerbang sebelum dianggap selesai)
- [ ] (BLOKER: tak ada emulator/AVD/system-image maupun perangkat adb di mesin ini, 2026-10-11) Emulator/HP 360×800dp: screenshot tiap tab + tiap sheet berdampingan dengan mockup (browser lebar 390px); selisih teks/urutan/warna = bug.
- [ ] Daftar centang per layar: judul, label, urutan baris, warna tag, state disabled, empty state, toast.
- [ ] Layar kecil (320dp) & font besar: tidak ada teks terpotong; target sentuh ≥48dp.

## A12. Tes, build, bukti
- [x] Host: `tools/test_android_parser.ps1`, `tools/test_android_privacy.ps1`; tes konversi nilai (menit↔jam, ×10, ×100, milli) di `RobotRepositoryTest.verifyModelHelpers`; `tests/test_dual_board_contract.py` disesuaikan ke sumber Compose (tanpa melemahkan asersi privasi).
- [x] APK debug build via Gradle 8.11.1 / JDK 17 (`assembleDebug --offline`, 2026-10-11).
- [ ] Perangkat (butuh HP+robot): pairing, baca/tulis setelan + baca-ulang, rahasia, memori, pengingat, OTA, reboot, reset pabrik, reconnect jarak jauh/BT off-on, soak. Tanpa bukti = jangan klaim lulus; catat di `.verification/` (jangan commit).

## A13. Dokumentasi & penutupan
- [ ] Perbarui `docs/mobile/BLE_SETTINGS_PROTOCOL.md` (status, catatan "dashboard unchanged" bila `ROBODESK_NO_DASHBOARD`) dan docs status/task terkait; centang `[ ]` di docs hanya dengan bukti perangkat.
- [ ] Mockup tetap sinkron: setiap perubahan layar di aplikasi diikuti perubahan mockup lebih dulu.
- [x] Hapus `Ui.kt`/`RobotScreens.kt`/`MainActivity` lama (dihapus atas keputusan user sebelum A11; fitur privasi/notifikasi lama dipindah ke Setelan > Notifikasi HP dan Lainnya > Diagnostik). A11 masih terbuka.
- [x] Teks usang dibersihkan ("approve this phone in the robot dashboard", dll.) di `RobotLinkService.kt` dan label status 14; aplikasi tidak lagi memerlukan dashboard maupun PIN admin lewat BLE (host test + grep, 2026-10-11).
- [ ] Tampilan APK baru diuji di perangkat lewat A12 dan I9.

---

## Tabel Gap — keputusan (mockup sudah diperbarui)
| ID | Keputusan |
|---|---|
| G1 | SELESAI. Tombol "Salin Wi-Fi" dihapus; isi SSID + sandi manual di lembar "Ubah" (ada "Tampilkan sandi"). Android tak bisa membaca sandi tersimpan. |
| G2 | SELESAI. HP mengecek versi terbaru dari GitHub Releases (`manifest-esp32c3.txt`, baris `version=`) lewat tombol "Cek versi terbaru". Informasi saja; robot tetap mengunduh sendiri dan memverifikasi tanda tangan saat `ota-start`. Tanpa perubahan firmware. |
| G3 | SELESAI. Daftar app dari aplikasi terpasang di HP; tiap app punya dua pilihan: Tampil (`notificationAllowlist`) dan Baca (`notificationSpeechAllowlist`). Ditambah switch `navigationEnabled`. |
| G4 | SELESAI. Layar onboarding 4 langkah (izin → pindai → buka jendela pairing → terhubung) ada di mockup; "Lupakan robot" membukanya lagi. |
| G5 | SELESAI. Disamakan dengan firmware: `inputMode` 2 Kata bangun / 1 Sentuh: ketuk untuk bicara / 0 Selalu mendengarkan (lama); `geminiVoice` teks ≤31 (default Iapetus); `geminiModel` teks ≤47 (default gemini-3.8-live); `speechStyle` textarea ≤240; `characterIntensity` segmen 0–2 (Halus / Aktif tapi tenang / Ekspresif); volume 50–1000 (default 450); VAD angka ×noise (default 3,50 / 1,90); zona waktu −720…840 mnt (WIB/WITA/WIT + Kustom). |
| G6 | DIBATALKAN: SFX (wake/touch/motion/notification) dan Proaktif (visual/suara) kini switch terpisah, sama dengan dashboard; tidak ada pemetaan 1→n. |
| G7 | Wajah animasi di Beranda hanya hiasan. |
| GAP-A…F | Disetujui (lihat `ANDROID_ROBOT_INTEGRATION.md`): wifiActiveSlot di snapshot; penghitung kapasitas allowlist; forget terautentikasi; OTA tanpa progres live; onboarding + Wi-Fi/Gemini; SEMUA fitur dashboard dipindah ke app (A14–A17, A4, A7). |

Dampak pada task: A2 kini punya acuan mockup (layar onboarding); A5 memakai kontrol di atas (bukan dropdown/slider lama); A6 tanpa tombol salin Wi-Fi; A9 "Cek versi terbaru" = HTTP GET manifest GitHub dari HP (butuh izin INTERNET).
