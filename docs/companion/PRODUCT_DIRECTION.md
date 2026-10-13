# Arah Produk RoboDesk: perilaku ala EMO, fitur, UI, standar jual

Status: rancangan 2026-10-13. Dokumen ini acuan keputusan, bukan bukti uji. Item dianggap selesai hanya dengan bukti perangkat (aturan `CLAUDE.md`).

Sumber: inventaris kode working tree 2026-10-13, ditambah direktif lama yang dihapus di `aa1f703` (baca dengan `git show aa1f703^:EMO_DIRECTIVE_hidup.md`, `...:emo-eye-engine.md`, `...:emo-eye-engine-oled-128x64.md`). Bagian 2 sampai 9 merangkum direktif itu dan menyesuaikannya dengan hardware sekarang.

---

## 0. Keputusan inti

1. **Posisi:** teman meja yang ekspresif, bukan asisten suara. Nilai jualnya: robot terasa hidup tanpa disuruh. AI (Gemini) hanya penambah.
2. **v1 dijual dengan hardware sekarang, tanpa motor.** "Gerak" ala EMO diganti mata (geser, miring, melompat), suara, dan reaksi IMU. Servo kepala masuk SKU v2. Ini sejalan dengan `ROADMAP.md` yang menetapkan software-first.
3. **Hidup penuh secara offline.** Lapisan L0 sampai L2 (bagian 3) tidak boleh butuh Wi-Fi atau Gemini.
4. **Mirip EMO di rasa, bukan di aset.** Jangan pakai nama "EMO", jangan salin desain mata, suara, atau animasi Living.ai, dan jangan pakai kata "Sonic" di nama dagang (bagian 12J).
5. **Urutan kerja:** tutup blocker stabilitas di `TASKS.md` NEXT, lalu Fase 1 Hidup, Fase 2 Kenal, Fase 3 Main, Fase 4 Siap jual (bagian 13).

## 1. Kondisi sekarang (fakta kode, belum semuanya terbukti di perangkat)

| Kanal | Yang ada |
|---|---|
| Layar | SSD1306 128×64 I2C. Mata digambar LivingEyes (lib eksternal `Documents/LivingEyes-merge/LivingEyes`, dipin di `tools/livingeyes-pin.json`): 24 ekspresi, 24 clip, 11 scene. Firmware mengekspos 12 emosi (`RoboBrain.h:82`). |
| Suara keluar | MAX98357A I2S 24 kHz. SonicSynth punya 18 cue prosedural. Jawaban Gemini Live dibatasi Bahasa Indonesia ≤6 kata (`RoboBrain.h:53`). Jam tenang 22:00 sampai 07:00 (`RuntimeSettings.h:141`). |
| Sentuh | 2 pad TTP223: kepala GPIO7 dan samping GPIO16. Gestur: tap, double tap (≤420 ms), elus (≥1,2 s). Tahan samping 1,4 s = mute mic. Tahan kepala 5 s = buka pairing. |
| IMU | MPU6050 mendeteksi angkat, taruh, miring, goyang, jatuh (`RoboDeskSonicCharacter.ino:295-300`). |
| Kehadiran & lingkungan | PIR AM312, AHT20 (suhu/kelembapan), BMP280 (tekanan). Baterai lewat ADC: low 3,5 V, kritis 3,3 V, hanya peringatan (`RoboBatteryMonitor.h:6`). |
| Suara masuk | INMP441 16 kHz dengan VAD. Wake word "Hi ESP". MultiNet offline berbahasa Inggris, 23 perintah. |
| Otak | CharacterMind (energi, curiosity, affection, boredom, valence, socialNeed, trust). RoboBrain memadukan mood tiap 3 s. CharacterEvolution berjalan harian dan tersimpan di NVS. Antrean aksi berprioritas ada di `CompanionCore.h:101-111`. |
| Aktivitas mandiri | 13 aktivitas (`CompanionActivityCatalog.h`), dipilih berbobot mood plus PreferenceLearner, durasi 20 sampai 90 s, jeda 2 sampai 5 mnt. Undangan proaktif maks 4 per hari, cooldown 30 mnt (60 mnt setelah ditolak). |
| HP | Notifikasi, navigasi, chat ke Gemini, setelan, memori, dan OTA bertanda tangan. |

**Celah terbesar dibanding EMO:**
- **Tidak ada siklus tidur/bangun.** Lib punya `Scene::Sleep`/`Activity::Sleeping`, tetapi `.ino` tidak pernah memanggil `setActivity`. Padahal momen mengantuk, tidur, dan bangun adalah yang paling membuat EMO terasa hidup.
- **Belum ada spesifikasi variasi reaksi** (jumlah varian, larangan ulangan beruntun) dan latensi belum terukur di perangkat.
- **Wake word "Hi ESP" dan perintah offline berbahasa Inggris** tidak layak jual di pasar Indonesia.
- **OLED menyala terus tanpa perlindungan:** tidak ditemukan kode peredupan atau pergeseran piksel. Mata yang posisinya hampir statis 24/7 mempercepat burn-in.
- **Robot tidak punya cara pamit:** baterai kritis tidak membuatnya tidur dengan anggun sebelum proteksi sel memutus daya.

## 2. Jiwa karakter

- **Kepribadian:** ceria, penasaran, manja, gampang kaget lalu cepat pulih, ekspresif, sedikit usil, mudah bosan lalu mencari kegiatan sendiri. Tidak pernah jahat, sarkas, atau menakutkan.
- **Cara bicara:** mata menanggung sekitar 70% ekspresi. Sisanya bip bernada ("emo-ese") dan kata sangat pendek: Gemini ≤6 kata (sudah), offline tanpa kata karena tidak ada TTS lokal.
- **Aturan emas:**
  1. Tidak pernah diam total; mikro-gerak selalu jalan.
  2. Reaksi pertama <100 ms, sebelum "berpikir".
  3. Setiap reaksi punya 3 sampai 5 varian; tidak ada dua reaksi identik berturut-turut.
  4. Ada ketidaksempurnaan kecil: kedip telat, lirikan salah arah lalu dikoreksi.
  5. Emosi punya inersia: transisi 0,5 sampai 3 s.
  6. Setiap emosi punya sebab yang bisa ditelusuri.
  7. Punya kegiatan sendiri saat tidak diajak main.
  8. Ekspresi sedikit dilebihkan, gaya kartun.
  9. Jujur soal keadaannya: kalau capek, tampak capek.
- **Pagar etika produk (wajib untuk dijual):**
  - Tanpa rasa bersalah atau manipulasi.
  - Ngambek ≤3 mnt dan pulih sendiri. Saat ngambek, mute, alarm, pengingat, dan fungsi keselamatan tetap jalan.
  - Tidak menghukum karena diabaikan.
  - Tidak mencari perhatian di jam tenang.
  - Batas undangan proaktif tetap (sejalan CHAR-02 di `tasks/02-character-and-touch.md`).
  - "Cemburu" dari direktif lama dibuang.

## 3. Arsitektur perilaku

| Lapisan | Peran | Target | Kode sekarang |
|---|---|---|---|
| L0 Refleks | Sentuh, IMU, suara keras → clip + SFX langsung | <100 ms, offline | Handler di `.ino` + clip LivingEyes |
| L1 Direktur | Memilih mood, siklus energi, idle, aktivitas, arbitrasi | <500 ms, offline | `CompanionCore`, `CompanionScheduler`; **siklus energi belum ada** |
| L2 Kepribadian & memori | Sifat, kedekatan, kebiasaan | berkala, offline | `CharacterMind`, `CharacterEvolution`, `LongTermSocialMemory`, `PreferenceLearner`, `BrainMemoryStore` |
| L3 AI | Percakapan pendek, niat, emosi jawaban | 1 sampai 4 s, butuh Wi-Fi | `GeminiLiveDirect`. LLM memberi emosi/niat; L1 yang menggerakkan mata. |

**Prioritas (tinggi ke rendah):**
1. Keselamatan IMU: jatuh, goyang, angkat.
2. Sentuh dan suara pemilik.
3. Emosi kuat yang sedang berjalan.
4. Kebutuhan mendesak: sangat mengantuk, baterai kritis.
5. Rutinitas terjadwal: alarm, pengingat, salam pagi.
6. Aktivitas mandiri.
7. Mikro-gerak (kedip, napas, lirik), yang selalu jalan di bawah semuanya.

Interupsi harus mulus: clip yang sedang jalan ditutup cepat (≤150 ms, ease-out), tidak dipotong kasar. Cocokkan urutan ini dengan `companion::Priority` di `CompanionCore.h`.

## 4. Peta emosi tunggal (mata, clip, SFX)

Satukan 12 emosi firmware, nama mood CharacterMind (sleepy, subdued, affectionate, curious, restless, cheerful, calm), dan "sulking" ke dalam satu tabel. Intensitas 1 sampai 3 mengatur ukuran mata, volume, dan durasi.

| Emosi | Mata (Expression/Scene) | Clip | SFX | Pemicu utama |
|---|---|---|---|---|
| Netral/tenang | Normal + napas | DoubleBlink | – | default |
| Ceria | Happy, Smile | Happy, Giggle | Happy | valence tinggi, disentuh |
| Sayang | Heart, Love | PetEnjoy, Love | Affection | elus ≥1,2 s |
| Penasaran | Curious, LookLeft/Right | Curious, Scan, PeekL/R | Curious | orang datang (PIR), curiosity >0,72 |
| Kaget | Surprised, Shock | Startle, Surprise | Surprise | sentuh saat tidur, angkat mendadak, suara keras |
| Bersemangat | Excited, Laugh | Laugh | Giggle | double tap, pemilik kembali setelah lama |
| Malu | Nervous | Shy | Giggle (pelan) | dipuji lewat chat, tertangkap "melamun" |
| Mengantuk | Tired | Sleepy | Yawn, Sleepy | energi <0,25, malam |
| Tidur | Scene Sleep | – | senyap | bagian 5 |
| Sedih | Sad, Crying (jarang) | Sad | Sad | valence <0,32, jatuh |
| Kesal/ngambek | Suspicious, Angry (intensitas 1) | Annoyed | cue "hmph" **(baru)** | tap ≥5x dalam 3 s, digoyang berulang |
| Pusing | Nervous (bentuk spiral sudah ada); preset Dizzy bila kurang terbaca | – | Shake | digoyang |
| Berpikir | Thinking, Scene Thinking | Think | Thinking | menunggu Gemini |
| Mendengar/bicara | Scene Listening | Listen, Speak | WakeAck | wake word, jawaban |
| Berhasil/gagal | Scene Success/Failure | Agree/Disagree | Success/Failure | timer, perintah |

## 5. Siklus energi dan tidur (prioritas perilaku nomor 1, belum ada)

Siklusnya: **Aktif → Mengantuk → Tidur ringan → Tidur nyenyak → Bangun.**

- **Masuk Mengantuk** bila energi <0,25, ATAU jam tenang dimulai, ATAU tidak ada orang dan interaksi ≥10 mnt.
- **Mengantuk** (1 sampai 3 mnt): kedip berat, 1 sampai 2 kali menguap, mata turun pelan seperti terkantuk, tanpa aktivitas panjang. Sentuhan membatalkan kantuk sebentar.
- **Tidur ringan:** Scene Sleep, napas pelan, sesekali "z", kecerahan turun. Bangun oleh sentuh, wake word, atau PIR (PIR hanya di luar jam tenang).
- **Tidur nyenyak** (setelah ≥30 mnt tidur atau di jam tenang): layar padam atau hampir padam, sesekali "mimpi" (mata bergetar kecil 1 sampai 2 s). Wake word tetap aktif sesuai setelan privasi.
- **Bangun:** kaget kecil, mata memicing seperti mengucek, menguap, lalu menyapa sesuai jam. Bila dibangunkan tengah malam: bingung atau kesal ringan, lalu tidur lagi kalau tidak ada interaksi selama 20 s.
- **Energi pulih saat tidur.** Lib sudah memulihkan energi di Scene Sleep.
- **Pengecualian:**
  - Alarm, timer, dan pengingat membangunkan robot sebentar, lalu robot tidur lagi.
  - Notifikasi HP saat tidur hanya ditampilkan bila prioritas tinggi.
  - Navigasi atau sesi fokus aktif mencegah tidur.
- **Ritual pagi:** gerakan pertama yang terdeteksi PIR setelah jam tenang selesai memicu salam pagi, sekali per hari.

## 6. Idle hidup tiga lapis

- **A. Mikro (selalu):** kedip tiap 2 sampai 6 s (kadang ganda, kadang lambat), napas (mata mengembang tipis), lirikan tiap 1 sampai 4 s, sesekali menatap lama satu arah. Verifikasi parameter LivingEyes terhadap angka ini.
- **B. Aksi kecil (baru, tiap 15 sampai 40 s, acak berbobot):** lihat sekitar (Scan), mengintip (PeekL/R), menguap bila energi rendah, senyum sendiri lalu malu, menoleh seperti "mendengar sesuatu", kedip-geleng mengusir kantuk. Bersin/cegukan maks 1 kali per jam.
- **C. Aktivitas panjang (sudah ada):** 13 aktivitas dengan jeda 2 sampai 5 mnt.
- **Aturan:**
  - Lapis B juga tidak boleh mengulang aksi yang sama berturut-turut.
  - Tunggu 20 sampai 30 s setelah interaksi sebelum masuk B atau C.
  - B dan C hanya berjalan saat ada orang. Tanpa orang, robot menuju kantuk (bagian 5), bukan menatap kosong.
  - Setiap reaksi dan aksi diberi jitter: durasi ±15%, nada SFX ±3 sampai 5%, intensitas ±1.

## 7. Tabel reaksi L0

| Masukan | Reaksi (pilih acak dari 3 sampai 5 varian) |
|---|---|
| Tap kepala | Kaget kecil lalu senang (^ ^), bip ceria |
| Double tap | Terkekeh, bersemangat |
| Elus ≥1,2 s | Mata hati, dengkur pelan, affection naik |
| Elus berulang yang lembut | Trust naik; lama-lama respons lebih manja |
| Tap ≥5x dalam 3 s | Kesal (intensitas 1). Bila berlanjut: ngambek ≤3 mnt, membuang muka (mata ke samping), pulih bila dielus sabar atau dibiarkan |
| Sentuh saat tidur | Ikuti aturan Bangun di bagian 5 |
| Sentuh setelah ≥2 jam sendiri | Gembira besar, "kangen" (Hearts) |
| Tap samping | Mulai bicara (pengganti wake word). Tahan 1,4 s = mute; perilaku apa pun tidak boleh memblokir mute |
| Diangkat pelan | Kaget lalu penasaran, mata melirik ke bawah |
| Ditaruh lagi | Lega, senang |
| Dimiringkan | Pasangan mata ikut bergeser/miring, bingung lucu |
| Digoyang | Pusing (spiral); bila berulang, kesal |
| Jatuh | Kaget kuat lalu sedih kecil; pulih lebih cepat bila dielus |
| Wake word | Scene Listening + WakeAck <300 ms |
| Suara keras mendadak (lonjakan energi VAD, bukan musik) | Kaget, lalu waspada sebentar |
| Orang datang setelah ≥2 mnt absen | Salam (sudah ada; cooldown 30 mnt) |
| Notifikasi HP | Lirik + bip Notification, hormati jam tenang dan filter app |
| Baterai rendah | Tampak lelah dan meminta dicas sekali, tidak berulang-ulang |
| Baterai kritis | Pamit (mengantuk), simpan state, lalu tidur dalam (**baru**) |
| Suhu >30 °C / lembap | Mengeluh kepanasan (sudah ada comfort alert tiap 15 mnt) |

## 8. Suara "emo-ese"

- Pakai 18 cue SonicSynth yang sudah ada, ditambah: "hmph" (ngambek), pusing, dan dengkur/mimpi (sangat pelan, mati di jam tenang).
- Setiap bunyi diberi jitter nada dan tempo agar tidak terdengar mesin. Volume mengikuti mood dan jam; malam lebih pelan.
- **Anggaran bunyi:** maks 6 bunyi spontan per jam tanpa interaksi, dan nol di jam tenang. Diam juga bagian dari karakter.
- Mode senyap mematikan bunyi tetapi tidak mengurangi ekspresi mata.

## 9. Ritme harian, ikatan, momen

- **Pagi (05 sampai 09):** bangun bertahap, salam pagi, energi tinggi.
- **Siang (09 sampai 16):** paling aktif dan penasaran.
- **Sore (16 sampai 19):** tenang, suka dielus.
- **Malam (19 sampai 22):** mengantuk, suara pelan.
- **Jam tenang:** tidur nyenyak.
- **Momen:**
  - Milestone hari ke-7, 30, 100, dan 365 bersama: Scene Hearts atau Birthday.
  - Ulang tahun pemilik: Scene Birthday sudah ada.
  - Setelah ≥1 hari dimatikan: bangun "kangen" dan bingung.
  - Mimpi saat tidur.
  - Sesekali muncul ekspresi baru di pagi hari.
- **Kejujuran fitur:** tanpa kamera atau pengenal suara, robot memperlakukan semua orang sama. Jangan klaim "mengenali pemilik" di pemasaran. Kedekatan (trust) berlaku per robot, bukan per orang.

## 10. Peta fitur

Status: **Kode** = ada di kode (host test/build, belum tentu terbukti perangkat); **Belum** = belum ada; **HW** = butuh hardware baru.

| Fitur | Status | Rilis |
|---|---|---|
| Karakter hidup offline: siklus tidur, idle 3 lapis, tabel reaksi, emo-ese | Sebagian kode, siklus tidur Belum | v1 (wajib) |
| Reaksi sentuh dan IMU | Kode | v1 |
| Ngobrol: wake word + chat dari app (Gemini) | Kode | v1 |
| Wake word kustom (mis. "Hai Robo") | Belum | v1 |
| Notifikasi HP, timer, Pomodoro, pengingat, jam, cuaca | Kode | v1 |
| Mute mic fisik, OTA bertanda tangan, onboarding app | Kode | v1 |
| Anti burn-in OLED dan tidur dalam saat baterai kritis | Belum | v1 |
| Navigasi, briefing pagi, comfort alert, preference learning, permainan sentuh | Kode | v1.x |
| IR remote, Home Assistant, kalender | Kode (IR hanya build dual) | v1.x |
| Mainan suara: lempar koin, dadu, suit | Belum | Fase 3 |
| Menari mengikuti musik (deteksi ritme dari mic) | Belum | Fase 3 |
| Kirim ekspresi/"elusan jarak jauh" dari app, diary mood harian lokal | Belum | Fase 3 |
| Servo kepala pan/tilt (menoleh ke sentuh/suara) | HW | v2 |
| Layar lebih besar atau berwarna, dok cas, sensor cahaya | HW | v2 |
| Kamera/pengenal wajah, roda/berjalan | Tidak direncanakan (privasi, biaya, risiko jatuh dari meja) | – |

## 11. UI aplikasi

**Sekarang:**
- 6 tab: Beranda, Setelan, Koneksi, Memori, Ingat, Lainnya.
- Onboarding 6 langkah.
- Acuan mutlak tampilan: `docs/mobile/mockup/android_app_mockup.html`.
- Dokumen tidak sinkron: `ANDROID_APP_SECURITY_FEATURE_ROADMAP.md:21-23` masih menyebut 5 destinasi lama.

**Usulan untuk konsumen (5 tab).** Perubahan IA wajib dimulai dari memperbarui mockup, baru kemudian kode.

1. **Robot:**
   - Wajah hidup yang mencerminkan mood robot saat ini, plus alasannya (mis. "ngantuk, sudah malam").
   - Status baterai, koneksi, dan mic.
   - Aksi cepat: ajak main, tidurkan/bangunkan, jangan ganggu.
2. **Ngobrol:** chat yang sudah ada, plus pilihan mode jawaban (suara, teks, atau keduanya).
3. **Main & Alat:** aktivitas, permainan, timer/Pomodoro, pengingat, IR, integrasi.
4. **Kenangan:** memori (lihat/hapus), milestone, diary mood harian (opt-in, lokal).
5. **Setelan:**
   - Karakter: preset Kalem / Ceria / Usil yang memetakan intensitas dan sifat awal, jadwal tidur, dan bunyi.
   - Suara & mic.
   - Notifikasi HP (filter per app).
   - Jaringan & AI.
   - Privasi (mute, hapus semua data).
   - Pembaruan: OTA plus catatan rilis.
   - Bantuan & Tentang: diagnostik pindah ke sini.
   - Zona bahaya.

**Prinsip:**
- Tanpa jargon di permukaan. Istilah seperti VAD, heap, dan WSS hanya muncul di Diagnostik.
- Onboarding ≤3 mnt dan diawali momen "kelahiran": mata robot terbuka pertama kali, lalu pemilik memberi nama.
- Bahasa Indonesia dan Inggris.
- Target sentuh ≥48 dp, font besar, TalkBack.
- Status offline selalu jelas.
- Toggle langsung berlaku (task NEXT sudah ada).

## 12. Standar siap jual (centang hanya dengan bukti)

**A. Stabilitas (blocker)**
- [ ] Crash heap corruption S3 (`TASKS.md` NEXT) ditemukan akarnya dan diperbaiki
- [ ] Soak 72 jam tanpa reset, dengan HP terhubung, chat, dan notifikasi
- [ ] Uji offline 2 jam lulus
- [ ] Pairing 10/10 di ≥5 model HP (Samsung, Xiaomi, Oppo/Vivo, Pixel, iPhone)
- [ ] Target heap C3 ≥48 KiB free dan ≥24 KiB largest di bawah beban
- [ ] Power-cycle 500 kali tanpa korupsi LittleFS/NVS
- [ ] Brownout tidak terjadi saat speaker di volume maksimum dan Wi-Fi/BLE aktif
- [ ] OTA dan rollback lulus di kedua board

**B. Keamanan siber** (acuan ETSI EN 303 645; untuk ekspor ke UE: EN 18031 di bawah RED, wajib sejak 1 Agustus 2025)
- [ ] Tidak ada PIN atau password AP default yang sama untuk semua unit (sekarang `robodesk`/`robodesk123`): kredensial unik per unit atau tanpa dashboard
- [ ] Build retail tanpa dashboard (build no-dashboard sudah ada)
- [ ] Secure Boot v2 dan flash encryption di unit produksi
- [ ] Kunci tanda tangan OTA disimpan offline, dengan rencana rotasi
- [ ] Kontak pelaporan kerentanan, masa dukungan pembaruan keamanan dinyatakan, dan daftar komponen (SBOM)

**C. Privasi dan AI**
- [ ] Patuh UU PDP No. 27/2022, dengan kebijakan privasi di app dan kemasan
- [ ] Indikator visual setiap kali mic mendengar atau audio dikirim ke cloud, plus mute fisik
- [ ] Persetujuan eksplisit sebelum audio dikirim ke Gemini; "hapus semua data" menghapus memori di robot dan cache di app
- [ ] Model akses AI diputuskan: kunci milik pengguna (sekarang) atau server proxy milik penjual. Periksa syarat komersial dan penggunaan data Gemini API.
- [ ] Pagar konten di persona (topik tak aman ditolak), plus mode ramah anak bila target pasarnya anak

**D. Kelistrikan dan baterai**
- [ ] Sel terlindungi PCM (sudah diasumsikan) dan firmware menidurkan robot sebelum PCM memutus
- [ ] Batas suhu pengisian
- [ ] IEC 62133-2 untuk sel dan UN38.3 untuk pengiriman
- [ ] Adaptor bersertifikat
- [ ] Uji panas di suhu ruang 40 °C
- [ ] Uji ESD pada pad sentuh (IEC 61000-4-2)

**E. Regulasi radio dan EMC**
- [ ] Sertifikasi perangkat telekomunikasi Komdigi (dulu SDPPI Kominfo) untuk Wi-Fi/BLE, dengan label di produk
- [ ] Untuk ekspor: FCC Part 15, CE RED (EN 300 328, EN 301 489), RoHS/REACH, label WEEE
- [ ] Target usia diputuskan: bila mainan anak di bawah 14 tahun, wajib SNI ISO 8124. Bila "gadget meja 14+", cantumkan di kemasan.

**F. Umur layar**
- [ ] Anti burn-in: pergeseran 1 sampai 2 px tiap beberapa menit, kecerahan turun saat idle dan malam, layar padam saat tidur nyenyak
- [ ] Jam nyala harian diukur dan dibandingkan dengan umur panel

**G. Manufaktur**
- [ ] Firmware uji pabrik yang menguji: OLED, 2 pad sentuh, IMU, PIR, AHT20/BMP280, mic lewat loopback speaker, baterai, RSSI Wi-Fi/BLE, dan link UART
- [ ] Nomor seri dan QR per unit, provisioning kunci, golden sample, inspeksi AQL
- [ ] Flash mengikuti layout partisi terpasang (app0 di `0x400000`)
- [ ] Pin TX C3 dikembalikan ke GPIO4 (sekarang sementara GPIO3)

**H. Aplikasi**
- [ ] Release signing dan Play App Signing, target SDK terbaru, lint bersih
- [ ] Formulir Data Safety
- [ ] Penjelasan dan persetujuan yang menonjol untuk akses notifikasi
- [ ] Laporan crash opt-in
- [ ] Release hardening Stage 6 di `ANDROID_APP_SECURITY_FEATURE_ROADMAP.md`

**I. Dokumen dan dukungan**
- [ ] Panduan cepat (ID/EN), peringatan keselamatan, kartu garansi, label kepatuhan
- [ ] FAQ: pairing basi, cara reset, mute
- [ ] Kanal OTA stable/beta dengan catatan rilis, proses RMA

**J. Merek dan IP**
- [ ] Nama dagang tanpa "EMO" (merek Living.ai) dan tanpa "Sonic"; cek PDKI DJKI sebelum mencetak kemasan
- [ ] Desain mata dan suara orisinal
- [ ] Lisensi semua library diperiksa untuk distribusi komersial (Adafruit, ESP-SR/WakeNet, LivingEyes)
- [ ] Wake word kustom lewat layanan Espressif

## 13. Fase kerja dan kriteria terima

Setiap butir menjadi 1 task di `TASKS.md` saat dikerjakan (1 task = 1 sesi).

| Fase | Isi | Kriteria terima (di perangkat) |
|---|---|---|
| 0 Stabil | Tutup NEXT: crash heap S3, pairing, chat, banner OLED | Soak 24 jam tanpa reset |
| 1 Hidup | 1.1 siklus tidur/bangun + anti burn-in; 1.2 idle lapis B + aturan variasi/jitter; 1.3 reaksi sentuh/IMU lengkap (pusing, ngambek berpagar); 1.4 emo-ese + anggaran bunyi; 1.5 tidur dalam saat baterai kritis | Checklist bagian 14 lulus; p95 sentuh → render ≤100 ms |
| 2 Kenal | Ritme harian, salam pagi/malam, milestone, preset kepribadian, mood live di tab Robot | Uji 7 hari pemakaian harian tanpa keluhan "berisik"/"mengganggu" |
| 3 Main | Mainan suara, menari mengikuti musik, ekspresi dari app, diary | Tiap fitur jalan offline atau gagal dengan anggun |
| 4 Siap jual | IA app 5 tab (mockup dulu), wake word kustom, hardening, sertifikasi, manufaktur, dokumen | Semua butir bagian 12 tercentang dengan bukti |

## 14. Checklist "terasa hidup" (uji penerimaan Fase 1)

- [ ] Saat bangun, tidak pernah diam total lebih dari 6 s
- [ ] Reaksi pertama terhadap sentuhan ≤100 ms (p95, 50 sampel)
- [ ] 20 tap beruntun tidak pernah menghasilkan dua reaksi identik berurutan
- [ ] Ruang kosong 15 mnt: robot mengantuk lalu tidur, dan layar meredup/padam
- [ ] Bangun dari tidur lewat transisi (kaget, kucek, menguap), tidak melompat
- [ ] Ngambek pulih sendiri ≤3 mnt; mute dan alarm tetap jalan selama ngambek
- [ ] Nol bunyi spontan di jam tenang
- [ ] Angkat, goyang, miring, dan jatuh masing-masing punya reaksi berbeda dan pulih ke netral
- [ ] 2 jam tanpa Wi-Fi: aktivitas, tidur, dan reaksi tetap jalan
- [ ] Keterbacaan: 5 orang awam menebak ≥8 dari 12 emosi dengan benar di layar 128×64

## 15. Keputusan terbuka untuk pemilik

1. Target pasar: anak-anak atau 14+ (menentukan SNI mainan dan mode ramah anak).
2. Akses AI: kunci Gemini milik pengguna atau server proxy milik penjual.
3. Nama dagang.
4. Layar v1: tetap OLED 0,96" atau naik ukuran sebelum dijual.
5. Servo kepala: masuk v1 atau tetap v2.
