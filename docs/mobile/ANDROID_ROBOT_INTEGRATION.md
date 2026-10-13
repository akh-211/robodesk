# Integrasi Android ↔ Robot (rencana task)

Tujuan: setiap fitur di [mockup](mockup/android_app_mockup.html) benar-benar terhubung ke robot lewat BLE, tanpa dashboard.
Pasangan dokumen: [`ANDROID_APP_TASKS.md`](ANDROID_APP_TASKS.md) (UI), [`BLE_SETTINGS_PROTOCOL.md`](BLE_SETTINGS_PROTOCOL.md) (kontrak).
Aturan: `[x]` hanya dengan bukti (host test = logika; HP+robot = perilaku). Tanpa perangkat = tulis "belum diuji perangkat".

## 0. Urutan eksekusi gabungan (I = integrasi, A = UI) dan batasan wajib

Urutan (A = `ANDROID_APP_TASKS.md`):
1. **I0** (keputusan, decode versi) → **I1** (protokol + firmware baru; pekerjaan firmware/host, tanpa perangkat).
2. Paralel dengan I1: **A0** (Compose + komponen) → **A1** (GATT/service/status; I3 membangun di atasnya, JANGAN buat dua implementasi) → **I2** (codec Kotlin) → **I3** (penjadwal).
3. **A2** onboarding, **A3** shell → **A4–A9** dan **I4** (penghubung per layar; item yang bergantung jalur baru I1 menunggu I1).
4. **A14–A17** dan kartu "Kontrol langsung" A4 hanya setelah I1 selesai dan lulus tes host.
5. **A10** (error/status), **I5** (siklus hidup), **I6** (jalur lama), **I7** (robot tiruan) — I7 sebaiknya mulai lebih awal dan dipakai oleh I2–I6.
6. **A11** (kesesuaian mockup) → **I8** (uji silang host) → **I9** (uji perangkat) → **I10** (keamanan/rilis) → **A12**, **A13** (penutupan).

Batasan wajib (dari `CLAUDE.md`, berlaku untuk semua task di sini):
- BLE tetap dimiliki C3, tidak dipindah ke S3 (brownout); guard heap BLE 96/80 KiB tidak boleh dilanggar oleh kind/ops baru.
- Tidak mengubah tabel partisi atau offset yang terpasang; flash app0 di `0x400000`. Mount LittleFS gagal TIDAK memicu format; konfigurasi baru (integrasi, profil IR) memakai NVS/penyimpanan yang ada, tanpa perubahan format tanpa rencana migrasi.
- OTA tetap bertanda tangan; app tidak menyuplai URL/biner ke robot.
- Rahasia write-only (Wi-Fi, kunci Gemini, token Home Assistant) tidak pernah dikembalikan, dicatat, atau di-commit; `secrets.h` dan `.verification/` tidak di-commit.
- Transkrip mentah hanya di RAM; memori/data pengguna = data tak tepercaya, bukan instruksi.
- Dashboard hanya untuk jaringan tepercaya; PIN/AP default harus diganti — pada build dashboard-less aspek ini hilang, bukan dilonggarkan.
- Centang `[ ]` di docs hanya dengan bukti perangkat; HP/board tidak tersedia = jangan klaim lulus.
- Tidak commit kecuali diminta; jalankan tes paling sempit dulu, suite penuh di akhir.

## 1. Matriks fitur → jalur robot (audit)

Status: **OK** = protokol/firmware sudah ada; **GAP** = ada celah, lihat bagian 2.

| # | Fitur mockup | Jalur ke robot | Status |
|---|---|---|---|
| 1 | Nama robot (Beranda, hero) | SettingsGet `robotName` | OK |
| 2 | Versi C3 / S3 | StatusSnapshot off 1/5 (u32 versi bertanda tangan) | OK, tapi format u32 → "1.4.2" harus dipastikan (GAP-H) |
| 3 | Uptime, heap, link, retry/CRC/putus, umur frame | StatusSnapshot (kind 11) | OK |
| 4 | Wi-Fi terhubung + RSSI | `wifiState` bit0, RSSI int8 | OK |
| 5 | **Slot Wi-Fi aktif** ("Slot 1", tag "terhubung") | tidak ada di snapshot (hanya "slot terkonfigurasi") | **GAP-A** |
| 6 | Kunci Gemini terpasang | `configFlags` bit0 | OK |
| 7 | Mute mikrofon, suara utama | SettingsSet `privacyMicMuted`, `masterSound` | OK |
| 8 | Semua setelan (Setelan, 7 grup) | SettingsGet/Set 9 halaman, `roboBehaviorImport` | OK (semua kunci ada di `RoboBehaviorWire.h`) |
| 9 | Allowlist notifikasi Tampil / Baca per aplikasi | `notificationAllowlist` & `notificationSpeechAllowlist`, masing-masing string ≤ 192 B | **GAP-B** (batas jumlah app) |
| 10 | Navigasi Maps | `navigationEnabled` | OK |
| 11 | Wi-Fi 3 slot (SSID/sandi) | SecretSet type 1 | OK (write-only) |
| 12 | Kunci Gemini kirim/hapus | SecretSet type 2 | OK |
| 13 | Jendela pairing (tag) | `bleFlags` bit2 | OK |
| 14 | Lupakan robot | forget terautentikasi (`PhoneBridgeStatus.authenticatedForget`) + hapus lokal | **GAP-C** (sisi robot harus dicabut juga) |
| 15 | Memori: daftar/tambah/hapus/hapus semua | kind 13–16 | OK |
| 16 | Pengingat: daftar/tambah/hapus | kind 17–19 | OK (butuh jam robot, GAP-G) |
| 17 | Diagnostik | StatusSnapshot | OK |
| 18 | Update: cek versi | HP ← GitHub (keputusan G2) | OK |
| 19 | Update: **progres bar & tahap** | `ota-start`; BLE **mati** selama update, hasil baru terbaca setelah C3 restart (§8.1, §13) | **GAP-D** (mockup bar live tidak mungkin) |
| 20 | Reboot S3 / C3 | Command op 1 | OK (BLE putus bila C3) |
| 21 | Reset pabrik | Command op 2 + token `WIPE`+session | OK (butuh onboarding ulang) |
| 22 | Onboarding: pindai, pairing | GATT scan + bond LESC + jendela pairing (task DONE: pemicu tanpa dashboard) | OK, tapi tak ada langkah Wi-Fi (**GAP-E**) |
| 23 | Notifikasi HP → robot, navigasi Maps, companion command | jalur lama `RoboNotificationListener` (kind 1–7) | OK, harus tetap jalan setelah pindah ke `RobotLinkService` |
| 24 | Ekspresi, bicara (talk), jeda/lanjut, Pomodoro start/pause/resume/stop (Beranda) | dashboard `/companion/action` (`expression`, `talk`, `pause`, `resume`, `pomodoro_*`); BLE hanya punya companion command terbatas | **GAP-F1** (kontrol baru) |
| 25 | Aktivitas start/pause/resume/cancel/skip/favorite/history_clear, game_accept/cancel, learning_enable/reset (A14) | `activity_*`, `game_*`, `learning_*` di companion; BLE lama hanya intensitas, jam tenang, activity start/pause/resume/cancel | **GAP-F1** |
| 26 | Integrasi: Home Assistant (URL, token, 3 entitas), cuaca (lat/lon), kalender (URL ICS), status, segarkan, hapus (A15) | dashboard `/external/config|status|refresh|clear` (config disimpan di C3, `RoboExternalIntegrations.h`) | **GAP-F2** |
| 27 | Remote IR: kirim NEC, pelajari 10 dtk, putar profil, daftar profil ≤4 (A16) | companion `ir_send|ir_learn|ir_replay` (profil di S3) | **GAP-F3** |
| 28 | Tes suara 12 cue (A17) | dashboard `/sound/test` | **GAP-F4** |
| 29 | Hapus pengalaman (Memori) | dashboard `/memory/experiences/clear` | **GAP-F5** |
| 30 | Status langsung: Pomodoro, aktivitas aktif, game ditawarkan, pembelajaran aktif, jumlah profil IR (untuk mengaktifkan/menonaktifkan kontrol) | tidak ada di snapshot schema 2 | **GAP-F6** (snapshot schema 3) |
| 31 | Ganti PIN admin, upload gambar lokal (`/ota`, `/ota/s3`), AP setup | tidak dipindah: PIN tidak dipakai (bond+HMAC); AP diganti onboarding Wi-Fi; upload lokal = alat USB/`tools/` (BLE terlalu lambat untuk ±1,5 MB dan tak aman membuka URL bebas) | **Pengecualian, DISETUJUI user** |

## 2. Keputusan (disetujui user)

| ID | Keputusan |
|---|---|
| GAP-A | Snapshot schema 3 menambah `wifiActiveSlot` (1 byte). |
| GAP-B | UI menghitung byte dua daftar (≤192 B), menampilkan "n app · x/192 B", menolak penambahan saat penuh. |
| GAP-C | "Lupakan robot": forget terautentikasi dulu; bila tak terjangkau, hapus lokal + peringatan robot masih mempercayai HP. |
| GAP-D | Update: layar "Memperbarui", BLE putus normal, sambung ulang otomatis, hasil dibaca dari `otaState/otaError`. Tanpa persen live. |
| GAP-E | Onboarding 6 langkah: izin → pindai → pairing → terhubung → Wi-Fi → kunci Gemini; banner Beranda bila belum ada Wi-Fi. |
| GAP-F | **Semua fitur dashboard dipindah ke aplikasi** (F1–F6 di bawah). Pengecualian (disetujui user): ganti PIN admin (dihapus karena PIN tak dipakai lagi), AP setup (diganti onboarding), upload gambar lokal (tetap lewat `tools/` USB; baris 31). |
| GAP-G | Pengingat sekali: tangani jam robot belum sinkron (`TimeNotSet`/`NetworkUnavailable`). |
| GAP-H | Aturan decode u32 versi dipastikan di I0 dengan vektor uji. |

### Tambahan protokol yang harus dirancang (I1) — usulan, final di I1
Semua memakai otentikasi §2 (bond + HMAC, Android saja), satu permintaan in-flight, respons `0x7d`, status baru bila perlu.

| Usulan | Isi | Eksekutor |
|---|---|---|
| Command op 4 `expression` | `04 id` (12 ekspresi) | S3 via UART |
| op 5 `talk`, op 6 `pause`/`resume` (`06 0/1`) | tanpa/1 byte | S3 |
| op 7 `pomodoro` | `07 {start,pause,resume,stop}` | S3 |
| op 8 `activity` | `08 {start(id 1..13),pause,resume,cancel,skip,favorite,history_clear}` | S3 (perluas `PhoneCompanionCommand.h`; op lama tetap) |
| op 9 `game` | `09 {accept,cancel}` | S3 |
| op 10 `learning` | `0a {enable 0/1, reset}` | S3 |
| op 11 `sound-test` | `0b cue(0..11)` | S3 |
| op 12 `ir` | `0c {send nec u32, learn nameLen name, replay nameLen name}` | S3 |
| op 13 `experiences-clear` | `0d` | S3 (`RoboBrain`, jalur sama dengan dashboard) |
| kind 20 `IrProfileList` | daftar nama profil (≤4 × 15) | S3 |
| kind 21 `IntegrationsGet` / 22 `IntegrationsSet` | dokumen berhalaman: haBase, tokenConfigured, entities[3], weatherEnabled, lat, lon, calendarUrl (≤ ±800 B); Set menerima token terpisah (write-only, seperti SecretSet); validasi sama dengan `RoboExternalIntegrations.h` (https, lat/lon, panjang) | C3 (config di C3) |
| kind 23 `IntegrationsAction` | `{refresh, clear}` + status `Busy` (setara 409) | C3 |
| kind 24 `IntegrationsStatus` | cuaca ringkas, nilai entitas, umur data | C3 |
| StatusSnapshot schema 3 | tambah `wifiActiveSlot`, `pomodoroState` (+sisa detik), `activityId/state`, `gameOffered`, `learningEnabled`, `irProfileCount`, `irLearnState` (idle/mendengarkan/berhasil/gagal), `integrationBusy`; append-only, reader schema 2 tetap valid | C3 + info dari S3 |

**Operasi panjang harus asinkron** (timeout permintaan hanya 5–10 s dan hanya 1 permintaan in-flight): `ir learn` (10 s), `integrations refresh`, `ota-start`, reboot/factory reset → respons `Ok` segera berarti "diterima"; hasil dibaca lewat StatusSnapshot (`irLearnState`, `integrationBusy`, `otaState`) atau IntegrationsStatus. Tidak ada respons yang menunggu hasil akhir.

Catatan teknis: perintah S3 melewati UART (§11) → tambah ops frame + batas ukuran; jaga `MessageMax` dan heap BLE C3 (guard 96/80 KiB); balasan ≤ 500 B; waktu tunggu per jenis.

## 3. Task integrasi

Urutan: **I0 → I1 → I2 → I3 → (I4…I7 paralel) → I8 → I9 → I10**. UI mengikuti `ANDROID_APP_TASKS.md`; task ini fokus lapisan data/robot.

### I0. Putuskan celah (gerbang)
- [x] User memutuskan GAP-A…GAP-F (bagian 2; disetujui, tercatat di tabel keputusan). Tiap keputusan diterapkan ke: mockup dulu → `BLE_SETTINGS_PROTOCOL.md` → `ANDROID_APP_TASKS.md`.
- [x] GAP-H: versi u32 = nomor rilis monoton (bukan semver), 0 = tak diketahui, tampil `v<N>`; `BLE_SETTINGS_PROTOCOL.md` §7 + `formatFirmwareVersion` + tes di `BridgeSettingsProtocolTest.kt` (host test).
- [x] Paritas kunci ekspor `RoboBehaviorWire.h` (62) vs mockup: semua ada; 4 label waktu (`quietStartMin/EndMin`, `dailyBriefingMinute`) tampil sebagai time picker; `pomodoroWasActive/Interrupted` = status internal firmware (tak ditampilkan; klien mempertahankan nilai saat menulis ulang); `phonePlatform` = `Forbidden` lewat BLE (lihat keputusan di bawah).
- [x] Robot lama dengan `phonePlatform=1` (iPhone): putuskan pesan & jalur pemulihan (reset pabrik); nilai default setelah reset = 0 (Android). **Keputusan:** app mendeteksi `phonePlatform=1` dari SettingsGet, menampilkan "Robot ini terdaftar untuk iPhone; lakukan reset pabrik (sentuh/serial) agar bisa dipakai dengan Android", dan tidak mencoba mengubahnya (BLE menolak `Forbidden`).

### I1. Perubahan firmware & kontrak (hanya yang diputuskan di I0)
- [x] StatusSnapshot schema 3 (`wifiActiveSlot` + status langsung, 76 B, append-only) di `PhoneSettingsProtocol.h` + doc §7 + fixture + `tests/phone_settings_protocol_test.cpp` + codec Kotlin; schema 1/2 tetap valid (host test; belum diuji perangkat).
- [x] GAP-B diputuskan sisi UI (§2): tidak ada perubahan firmware, buffer, NVS atau format. Alasan: allowlist sudah berupa dua string <= 192 B di `RoboBehaviorWire.h` yang divalidasi `roboBehaviorImport` (kelebihan = `InvalidSettings`, host test ada), jadi UI cukup menghitung byte ("n app · x/192 B") dan menolak penambahan saat penuh; itu dikerjakan di A-task UI/I4/I5, bukan di I1.
- [x] GAP-F1 kontrol langsung: Command op 4–13 di `PhoneSettingsProtocol.h` (final di doc §8.2; `PhoneCompanionCommand.h` tidak diubah, jalur kind 1–7 lama utuh), op UART `LinkCommand=12` (frame `Settings` yang ada; `RoboLinkProtocol.h` tidak perlu berubah), handler S3 `roboRobotRunLiveCommand` memanggil `dashboardCompanionAction` yang SAMA dengan dashboard; status `Rejected=23`; doc + fixture + tes host (kode/build lulus; belum diuji perangkat).
- [x] GAP-F2 integrasi: kind 21–24 (doc §17) di `PhoneSettingsProtocol.h` + `gatewayRunIntegrations` (`RoboC3Gateway.h`): memakai `roboexternal::httpsUrl/validEntity/save/requestRefresh/busy` yang sama; token dan URL kalender write-only (tak pernah dikembalikan/dicatat, buffer dibersihkan); dokumen <= 500 B di exchange buffer yang ada, tanpa alokasi baru (guard heap 96/80 KiB tak disentuh). Beda dari usulan: URL kalender juga write-only (flag saja), seperti dashboard. Host test + fixture + build (belum diuji perangkat).
- [x] GAP-F3 IR: send/learn/replay (op 12) + `IrProfileList` (kind 20, doc §16; UART op 13 `LinkIrProfiles`, handler S3 di `RoboRobotRole.h`), nama tervalidasi 1..15 B, `irLearnState`/`irProfileCount` di snapshot (host test + build; belum diuji perangkat).
- [x] GAP-F4 tes suara dan GAP-F5 hapus pengalaman: op 11 (`dashboardSoundTest`) dan 13 (`brain.clearExperiences`), keduanya via S3 (host test + build; belum diuji perangkat).
- [x] GAP-F6 snapshot schema 3 (status langsung) + kompat schema 1/2 (host test; belum diuji perangkat). `integrationBusy` sudah terisi dari `roboexternal::busy()`.
- [x] Semua kind/op baru: host test (`phone_settings_protocol_test.cpp` bagian 9 + 7b) untuk gerbang `admitted` (5 flag, semua kind 8–24 dan op 4–13: tanpa sesi = tak dijawab), envelope sesi salah/age/tamper ditolak, di luar batas = `BadRequest`/`PageOutOfRange`, rahasia tak muncul di respons dan dibersihkan `complete()`; build default dan `-NoDashboard` lulus. `RobotUnavailable`/`UpdateBusy` dari worker C3/S3 (butuh UART + Arduino) hanya terbukti lewat kode dan fixture respons, tidak ada host test; belum diuji perangkat.
- [ ] Dependensi dashboard dilepas dari konfigurasi rilis HANYA setelah semua kemampuan di atas terbukti di perangkat (I10).
- [x] Build `tools/build_dual_board.ps1 -Board both` (gateway 1875648 B, margin slot 90432 B; robot margin 872416 B), `-Board gateway -NoDashboard` + `tests/run_host_tests.ps1` penuh hijau (2026-10-11); flash tetap butuh perangkat (belum diuji perangkat).

### I2. Codec Kotlin = codec firmware (satu sumber kebenaran)
- [x] Semua kind 8–24 + respons `0x7d` + SecretSet + Command + StatusSnapshot (schema 1/2/3) diuji terhadap `tests/fixtures/ble_settings_v1.txt` yang SAMA dengan tes C++ (`parser_tests/BridgeSettingsProtocolTest.kt`; 44 vektor permintaan + 36 respons, encoder byte-exact + envelope; `tools/test_android_privacy.ps1` hijau 2026-10-11).
- [x] Framing 8-byte, reassembly maks 600 B, timeout fragmen 5 s, CRC32 `java.util.zip.CRC32` (host test `BridgeGattFramingTest`, `BridgeSettingsProtocolTest`).
- [x] Kasus negatif: CRC salah, halaman tak berurutan, panjang tak sah, status 1–17, trailing bytes schema > 1 diabaikan (+ fuzz 5000 input acak ke semua parser tanpa exception).

### I3. Penjadwal permintaan & sesi (`RobotLinkService`)
- [x] Tepat 1 permintaan in-flight global (protokol); antrean berprioritas: aksi user > simpan setelan > polling status > tarik memori. `RobotRequestScheduler` + `RequestPriority`; halaman lanjutan SettingsSet memakai jalur CONTINUATION agar tidak disela. Host test lewat `FakeRobot` (envelope+framing sungguhan).
- [x] Timeout 5 s (10 s halaman terakhir SettingsSet); retry hanya dengan counter baru; `Busy` → backoff pendek; `SequenceError/CrcMismatch` → ulang dari halaman 0 (batas ulang). Baca di-ulang sekali setelah timeout; tulis tidak pernah di-ulang otomatis; `Busy` maks 3x backoff 250/500/750 ms; Sequence/CRC pada SettingsSet mengulang dari halaman 0 maks 2x (`writeSettings`), pembacaan berhalaman sudah restart maks 3x.
- [x] Polling status hanya saat layar butuh; dijeda saat ada tulis; berhenti di latar belakang. `StatusPoller` (interval 5 s) terpasang di `RobotLinkCore`; bendera `screenNeedsStatus` diisi layar (A4/I4), `appForeground` oleh MainActivity. Host test; belum diuji perangkat.
- [x] Counter envelope naik tegas per arah; sesi baru reset; respons terikat sesi (buang respons sesi lama). Host test: counter 1..n per arah, replay ditolak, balasan sesi lama dibuang.
- [x] Putus di tengah SettingsSet/SecretSet → hasil "tidak pasti": UI wajib baca-ulang setelah sambung (tidak anggap sukses). Scheduler menghasilkan `SettingsFailure.Reason.UNCERTAIN` + `needsReread` untuk tulis yang sudah terkirim (timeout atau putus); layar yang membaca ulang = I4/A5-A8.
- [x] Semua buffer rahasia (sandi Wi-Fi, kunci Gemini) di-wipe setelah kirim/gagal/putus (`Request.wipe()` di `finish`; host test untuk sukses, timeout, putus, antre-belum-terkirim). Buffer di sisi UI (field teks) = I4/A6.

### I4. Penghubung per layar (lapisan `RobotRepository`)
Satu fungsi repositori per baris matriks 1–21, masing-masing dengan pemetaan error ke pesan UI (A10):
- [x] Beranda: status + nama robot + switch mic/suara (batch ke SettingsSet bersama Setelan): `readStatus`, `robotName`, `loadSettings/saveSettings`. (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Setelan: baca → model lokal → konversi unit (menit, ×10, ×100, milli) → tulis → baca-ulang → bandingkan; pertahankan kunci tak dikenal (`RobotSettings`, `SettingsUnits`, `FlatJson`). (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Notifikasi: baca/tulis dua allowlist (parse/serialisasi paket, batas 192 B, GAP-B), sinkron dengan filter privasi HP (HP boleh mempersempit, tak memperluas): `Allowlist.State` (Baca mensyaratkan Tampil, blokir privasi, kapasitas 191 B/192 B). (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Koneksi: SecretSet Wi-Fi/Gemini; "terhubung" dari snapshot; pairing window; forget (GAP-C): `sendWifi/clearWifi/sendGeminiKey/forgetRobot(ForgetPort)`. (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Memori: cache + revisi "tarik bila berubah"; tolak 17 `ContentRejected`. (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Pengingat: list/add/delete; validasi waktu; GAP-G. (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Lainnya: diagnostik; ota-start; reboot (target bitmask); factory-reset (token); `otaResult`, `latestVersion`. (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Kontrol langsung (Beranda) dan Aktivitas & permainan: repositori memetakan tombol ke Command op 4–10; tombol dinonaktifkan sesuai status langsung (`LiveAvailability`). (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Integrasi: Get/Set berhalaman, token write-only, refresh/clear, status; tangani `Busy`. (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] IR: send NEC, learn (timeout 10 s + hasil), replay, daftar profil; validasi nama/hex di klien. (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)
- [x] Tes suara dan hapus pengalaman. (host test `RobotRepositoryTest.kt` terhadap `FakeRobot`; belum diuji perangkat)

### I5. Peristiwa siklus hidup (robot memutus BLE atau berubah)
- [x] Reboot S3: sesi tetap; tampilkan "menyambung ulang robot" sampai `linkFlags` bit1 kembali. `LinkNotices` (S3_LINK) (logika host `RobotLifecycleTest.kt` terhadap FakeRobot lewat `tools/test_android_privacy.ps1`, 2026-10-11; belum diuji perangkat)
- [x] Reboot C3: BLE putus ±1,5 dtk → reconnect otomatis → baca-ulang status. Jawaban hilang pada reboot C3 = hasil yang diharapkan (`RobotRepository.reboot`), backoff 1→30 dtk, `onReady` membaca status (logika host `RobotLifecycleTest.kt` terhadap FakeRobot lewat `tools/test_android_privacy.ps1`, 2026-10-11; belum diuji perangkat)
- [x] OTA (GAP-D) `OtaTracker` (adopsi bila jawaban `ota-start` hilang, hasil hanya setelah reconnect/30 dtk, batas 10 mnt, tulis dinonaktifkan selama update, label untuk otaError 1-13 dan 255) (logika host `RobotLifecycleTest.kt` terhadap FakeRobot lewat `tools/test_android_privacy.ps1`, 2026-10-11; belum diuji perangkat): setelah `ota-start` Ok → layar "Memperbarui" (putus normal, timer, tidak ada tombol tulis) → auto-reconnect → baca `otaState/otaError/otaTargetVersion` → hasil (Done / UpToDate / Failed + pesan per otaError 1–13/255). Batas waktu menunggu wajar (mis. 10 mnt) lalu "belum tersambung, periksa robot".
- [ ] Reset pabrik: ±5 dtk → bond/kredensial dihapus → app kembali ke onboarding; bond lokal dibersihkan. Sebagian: token `WIPE` dicek lokal, kredensial/cache dihapus + onboarding (kode, host test token/uncertain); pembersihan bond Android memakai `removeBond` via refleksi (`TrustedRobotStore.removeBond`, best-effort, tanpa API publik), belum diuji perangkat sehingga tidak dicentang.
- [ ] Bluetooth HP dimatikan/dinyalakan, aplikasi dimatikan paksa, HP restart, robot di luar jangkauan. Kode: receiver BT + boot, notice "Bluetooth mati", backoff teruji host; perilaku nyata belum diuji perangkat.
- [x] Revoke/perubahan sesi dari sisi robot (replay counter → ack status 1/reason 2) ditangani tanpa loop: `RobotRequestScheduler.onRejectedAck` (gagal REJECTED, tak diulang), `SessionRejectGuard` (1 resync, lalu berhenti sampai user menekan hubungkan ulang), `HandshakeGuard` (3 sesi gagal = notice reset pabrik/iPhone) (logika host `RobotLifecycleTest.kt` terhadap FakeRobot lewat `tools/test_android_privacy.ps1`, 2026-10-11; belum diuji perangkat)

### I6. Kesinambungan jalur lama
- [x] Notifikasi HP → robot (kind 1–7), navigasi Maps, `BridgeCompanionCommand` tetap bekerja setelah GATT pindah ke `RobotLinkService`; tes regresi: `test_android_privacy.ps1`, `test_android_parser.ps1`, `tests/test_dual_board_contract.py::test_android_single_gatt_and_legacy_path_survive_service_move` (2026-10-11; belum diuji perangkat).
- [x] Satu GATT untuk semuanya: kontrak tes memastikan `connectGatt(` hanya satu kali, di `RobotLinkService.kt` (belum diuji perangkat).
- [x] Efektif allowlist = irisan robot ∩ pilihan HP: kind 1 dikirim hanya bila paket ada di allowlist robot DAN mode HP bukan OFF (kontrak tes). Perbaikan: setelah Simpan setelan, `RobotLinkCore.setAllowed` menyegarkan allowlist robot tanpa menunggu reconnect (belum diuji perangkat).

### I7. Robot tiruan untuk uji (tanpa perangkat)
- [x] `FakeRobot` (Kotlin, host test) yang memerankan C3: handshake, HMAC, counter, halaman, semua kind, status error injeksi (Busy, Timeout, CRC, Sequence, ContentRejected), putus di tengah operasi, OTA memutus lalu hasil. **Sebagian** (`parser_tests/FakeRobot.kt`): HMAC, counter, framing MTU 23, halaman, respons 0x7d, injeksi Busy/Drop(timeout)/Tamper/Disconnect/Sequence/CRC/ContentRejected, OTA memutus lalu snapshot baru; handshake v2 sungguhan (hello/challenge/finish/accepted) menurunkan kunci+sesi FakeRobot di `RobotRepositoryTest.verifyHandshakeFakeRobot`; skrip balasan stateful (`RobotModel`) untuk semua kind yang dipakai repositori.
- [x] Tes integrasi repositori+penjadwal melawan `FakeRobot` untuk setiap baris matriks (`RobotRepositoryTest.kt`, hijau lewat `tools/test_android_privacy.ps1`).
- [x] Skenario fuzz ringan: respons rusak/acak tidak membuat crash/ANR (5000 input acak ke parser dan scheduler; host test).

### I8. Uji silang firmware ↔ Android (host)
- [x] Fixture bersama `tests/fixtures/ble_settings_v1.txt` dipakai C++ (`phone_settings_protocol_test.cpp`), Kotlin (`BridgeSettingsProtocolTest.kt`) dan Python (`test_ble_settings_vectors.py`); ketiganya hijau 2026-10-11.
- [x] `tests/run_host_tests.ps1`, `tools/test_android_parser.ps1`, `tools/test_android_privacy.ps1`, `tests/test_ble_settings_vectors.py`, `tests/test_no_dashboard_contract.py`, `tests/test_dual_board_contract.py` hijau; `build_dual_board.ps1 -Board both` (gateway 1875648 B, margin 90432 B; robot margin 872416 B) dan `-Board gateway -NoDashboard` (margin 371280 B) lulus; `assembleDebug --offline` lulus (2026-10-11).

### I9. Uji perangkat (butuh HP + C3 + S3) — matriks bukti
Tiap baris: perintah/aksi, hasil yang diharapkan, log di `.verification/` (jangan commit).
- [ ] Onboarding dari nol: pindai → jendela pairing → bond → sesi → Wi-Fi → kunci Gemini.
- [ ] Setelan: ubah satu kunci per grup → simpan → baca-ulang cocok → perilaku robot berubah (suara, wajah, jam tenang, pomodoro, VAD, notifikasi).
- [ ] Mute mic & suara utama dari Beranda berpengaruh langsung.
- [ ] Wi-Fi: ganti slot → reboot C3 → tersambung ke slot baru; kunci Gemini salah/benar → perilaku Gemini.
- [ ] Memori: tambah (diterima & ditolak filter), kunci sama = ganti, hapus, hapus semua, hapus pengalaman; muncul di percakapan robot.
- [ ] Kontrol langsung: tiap ekspresi tampil di robot; talk, pause/resume; Pomodoro start/pause/resume/stop sinkron dengan status.
- [ ] Aktivitas: 13 aktivitas dimulai/jeda/lanjut/lewati/favorit/batal; game accept/cancel; learning on/off/reset.
- [ ] Integrasi: HA (URL+token+entitas) terbaca nilainya di status, cuaca, kalender ICS; segarkan; hapus; token tak pernah kembali ke HP.
- [ ] IR: kirim NEC mengendalikan perangkat nyata; pelajari dan putar profil; batas 4 profil.
- [ ] Tes suara: 12 cue terdengar di speaker robot.
- [ ] Onboarding: Wi-Fi + kunci Gemini dari nol tanpa AP/dashboard; banner Beranda bila belum ada Wi-Fi.
- [ ] Pengingat: sekali/harian/timer berbunyi pada waktunya; jam belum sinkron ditangani.
- [ ] Notifikasi per aplikasi (Tampil/Baca) dan navigasi Maps.
- [ ] Diagnostik cocok dengan serial `tools/serial_monitor.py`.
- [ ] OTA bertanda tangan end-to-end (gate rilis di `GITHUB_RELEASES.md`), hasil terbaca setelah restart; kegagalan (Wi-Fi mati) terbaca.
- [ ] Reboot S3/C3, reset pabrik → onboarding ulang; lupakan robot dua arah (GAP-C).
- [ ] Ketahanan: jarak jauh/putus, BT off-on, app force-stop, HP restart, 20 siklus reconnect, soak (`tools/serial_health_check.py COMx --cycles 20 --soak-seconds 7200`) dengan heap BLE C3 di atas guard (96/80 KiB).
- [ ] Throughput/latensi: waktu SettingsGet 9 halaman dan SettingsSet; target diukur dulu, bukan diasumsikan.

### I10. Keamanan, privasi, gerbang rilis
- [x] `security-review` BLE (audit kode + host test, 2026-10-11): semua kind lewat `admitted()` (bond+sesi+MAC+Android), replay ditolak (`counter<=lastRx` → ack 1/2), rahasia write-only dibersihkan (`complete()`), tak ada `Log.*`/println di Android, tak ada FileProvider, `bridge_secrets`/`trusted_robot` dikecualikan dari backup. Bagian perangkat (sniffer, log serial) belum diuji perangkat.
- [x] Android: `allowBackup` aman (prefs kredensial + cache memori terenkripsi Keystore dikecualikan; kunci Keystore tak ikut backup), kredensial di Keystore (`BridgeCredentialStore`), tidak ada PIN admin di BLE (`PhoneBleTransport.h`). Temuan diperbaiki: allowlist robot basi setelah simpan, teks usang "dashboard", label status 14.
- [ ] Kesiapan tanpa dashboard: paritas fitur GAP-F terbukti satu per satu di perangkat; jalur pemulihan pairing (sudah ada pemicu sentuh/serial `pair`, `factory_reset` serial C3) terbukti di perangkat; baru `ROBODESK_NO_DASHBOARD=1` boleh jadi konfigurasi rilis.
- [ ] Jangan klaim gate rilis lulus tanpa bukti; baca `GITHUB_RELEASES.md` sebelum rilis.
