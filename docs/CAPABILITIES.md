# Kemampuan Firmware RoboDesk (rujukan tunggal)

Dokumen ini merangkum semua fitur, perintah, perilaku, perangkat keras, dan batasan firmware, supaya tidak perlu membaca seluruh proyek.
Dibuat 2026-10-16 dari pembacaan kode oleh Claude, lalu diperiksa silang oleh tiga sub agent. Rujukan `File:baris` ada di kode saat itu dan bisa bergeser.

**Cara membaca status**
- **Terverifikasi kode** berarti ada di kode dan lulus host test atau build.
- **Uji perangkat** berarti pernah terbukti di robot.
- Banyak fitur hanya "terverifikasi kode". Pegangan resmi tetap `TASKS.md` dan `docs/`. Item `[ ]` di docs hanya dicentang dengan bukti perangkat.
- Bagian yang disimpulkan dari nama file atau pengaturan tanpa membaca isinya ditandai *(inferensi)*.

---

## 1. Arsitektur dan perangkat keras

| Board | Peran |
|---|---|
| **ESP32-S3** (DevKit N16R8, 16 MB flash, 8 MB PSRAM) | Semua periferal, perilaku, memori, audio, pengenalan lokal, dan TLS Gemini |
| **ESP32-C3** (SuperMini, 4 MB flash) | BLE, Wi-Fi, NTP, dashboard web, OTA GitHub, integrasi eksternal |

- **Tautan:** UART 3,3 V, 921600 baud, 8N1. S3 TX=GPIO17 ke C3 RX=GPIO6; C3 TX ke S3 RX=GPIO18. Pin TX C3 saat ini sementara GPIO3 (lihat memori proyek).
- **BLE dimiliki C3** dan tidak boleh dipindah ke S3 (brownout). Ada guard heap BLE per profil di `RoboC3Gateway.h`.
- **Firmware dibangun** dengan Arduino-ESP32 3.3.11, C++ header-only.

**Pin S3** ([dual-board-wiring.md](hardware/dual-board-wiring.md), `RoboBoardProfile.h`)

| Fungsi | Pin |
|---|---|
| I2C SDA / SCL | 8 / 9 (OLED SSD1306 0x3C, MPU6050 0x68, AHT20, BMP280 0x77) |
| Sentuh kepala / samping (TTP223) | 7 / 16 |
| PIR (AM312) | 15 |
| Mikrofon INMP441 WS / BCLK / DATA | 4 / 5 / 6 |
| Speaker MAX98357A DIN / BCLK / LRC | 11 / 12 / 13 |
| IR terima / kirim | 1 / 2 |
| Monitor baterai (ADC1) | 10 (pembagi 100k:100k) |
| UART ke C3 | 17 TX / 18 RX |

**Layar:** SSD1306 128×64 monokrom, I2C 400 kHz, sekitar 30 FPS dengan pengiriman delta (`Ssd1306DeltaPresenter`). Satu frame hanya 1 KB, jadi PSRAM tidak berguna untuk frame penuh.

**Partisi dan penyimpanan**
- S3: `partitions/robodesk_ota_16mb.csv` (2 slot OTA, `spiffs` berisi LittleFS, `model` ESP-SR 6 MiB). App0 ada di `0x400000`, bukan `0x10000`.
- C3: `partitions/robodesk_ota_4mb_c3.csv`.
- Mount LittleFS yang gagal tidak boleh memicu format.

---

## 2. Perintah

### 2.1 Chat di app dan teks lokal (`RoboCommands.h`, tidak sampai ke Gemini)
Tanpa `/`, seluruh pesan harus cocok persis. Dengan `/` bentuk pendek diterima.

| Perintah | Fungsi |
|---|---|
| `/timer N`, "timer 5 menit", "set timer 10 minutes", "5 minute timer" | Timer N menit, 1–180 |
| `/fokus`, "mulai fokus" | Mulai sesi fokus (pomodoro). Kata "fokus" saja tidak memulai sesi. |
| "jeda fokus", "lanjut fokus", "stop fokus" | Jeda, lanjut, hentikan (juga lanjutkan, berhenti, selesai) |
| "jeda inisiatif", "lanjut inisiatif" | Larang atau izinkan robot memulai aksi sendiri |
| `/status`, "cek status" | Status robot |
| `/help`, "bantuan" | Daftar perintah singkat (maksimal 95 karakter karena buffer balasan 96 byte) |
| `/nyanyi`, `/joget`, `/lambai`, "angkat tangan", `/diam` | Aksi pertunjukan (3.1). `/diam` mengakhiri. "tunjukkan joget" juga diterima. Tanpa slash, kata tunggal ("joget") tidak dianggap perintah. |
| `/senang` `/penasaran` `/malu` `/cinta` `/sedih` `/kaget` `/berpikir` `/setuju` `/menolak` `/kedip` `/tertawa` `/ngantuk` | Ekspresi. Juga padanan Inggris (`/happy`). Tanpa slash harus berbentuk "tunjukkan/tampilkan/show/look <kata>". |

**Saran "Maksud kamu …?"** (`suggest()` dan `describe()`)
- Hanya aktif saat Gemini tidak tersambung. Saat online, teks langsung ke Gemini.
- Mengenali kata longgar: alarm atau pengingat atau countdown menjadi timer, pomodoro menjadi fokus, hentikan atau matikan atau akhiri menjadi stop, dan seterusnya.
- Mengenali angka dalam kata ("lima belas menit"), kata sopan yang diabaikan (tolong, dong, ya, robot, aili), dan salah ketik satu huruf pada kata sekitar 5 huruf ke atas.
- Jawaban ya: ya, iya, yes, y, yap, ok, oke, betul, boleh. Berlaku 15 detik.
- Jawaban tidak: tidak, nggak, ngga, gak, enggak, no, n, batal, jangan. Dibalas "Oke, dibatalkan".

### 2.2 Suara offline (ESP-SR MultiNet, `OfflineVoiceCommands.h`)
23 frasa Inggris, aktif hanya di S3 yang memuat model.
- **Ekspresi (12):** Show happy, Look curious, Look shy, Show love, Look sad, Act surprised, Start thinking, Show agreement, Show disagreement, Wink, Laugh, Look sleepy.
- **Sistem:** Check status, Pause initiative, Resume initiative.
- **Timer:** hanya 5, 10, 25, 45 menit ("Start five minute timer", dan seterusnya).
- **Fokus:** Start, Pause, Resume, Stop focus session.
- **Kata bangun:** ditentukan model WakeNet di partisi `model`. "Hi ESP" di `WakeWordBuildConfig.h` hanya label dan tidak bisa diganti runtime.

### 2.3 Tool Gemini (12, `RoboBrain.h`)
- `express_character`: 22 emosi.
- `run_command`: status, pause_initiative, resume_initiative, timer (1–180), pomodoro_start/pause/resume/stop. Hanya jalan jika ucapan pengguna menyebut keluarga perintahnya.
- `remember_fact`, `recall_memory` (maksimal 5), `forget_memory`, `set_owner_name`.
- `create_reminder` (sekali, harian, timer), `list_reminders`, `cancel_reminder`.
- `play_game`: lempar koin, dadu, suit (batu-gunting-kertas), tebak angka 1–5, bola 8 ajaib.
- `get_robot_state`.
- `get_external_context`: cache baca-saja untuk weather, calendar, atau home_assistant. Hanya jika permintaan pengguna menyebut kategorinya, satu kategori per panggilan, dan tidak pernah mengendalikan perangkat.

### 2.4 Dashboard web (dari C3, butuh PIN)
- **Aksi companion (`/companion/action`):** sleep_now, wake_now, remote_pet, talk, expression, jeda 15 menit atau 1 jam, resume, privacy (toggle mic), activity_start/pause/resume/cancel/skip/favorite/history_clear, learning_enable/reset, game_accept/cancel, ble_pair, ble_approve, sound_test (12 cue), experiences_clear, pomodoro_*, dan ir_send/ir_learn/ir_replay (hanya build dual-board S3).
- **Rute lain:** `/` `/d.css` `/d.js` `/api/status` `/api/memory` `/api/reminders` `/api/notifications` `/api/ota`, POST `/save` `/wifi-save` `/reboot` `/factory`, `/memory/add|delete|clear`, `/memory/experiences/clear`, `/reminder/add|cancel`, `/sound/test`, `/ota` (unggah .bin dengan header `X-Robo-Signature`), `/ota/github/check|install`.
- **Integrasi eksternal:** `/integrations`, `/external/config|status|refresh|clear`.
- **Gateway:** `/ota/s3`.
- **Dashboard hanya untuk jaringan tepercaya.**

### 2.5 Perintah BLE dari app (terautentikasi, `PhoneSettingsProtocol.h`, `PhoneCompanionCommand.h`)
- **Companion:** SetIntensity (0–2), SetQuietHours, StartActivity, PauseActivity, ResumeActivity, CancelActivity.
- **Aksi:** Reboot, FactoryReset, OtaStart, Expression, Talk, Pause, Pomodoro, Activity, Game, Learning, SoundTest, Ir, ExperiencesClear, Rest (tidur atau bangun), Pet.
- **Data:** settings get/set, secret set, status snapshot, memory list/add/delete/clear, reminder list/add/delete, IR profile list, integrations get/set/action/status, chat send/poll.

### 2.6 Konsol serial C3
`pair` (hanya build non-BLE-robot), `factory_reset` diikuti `confirm` dalam 10 detik.

---

## 3. Ekspresi dan suara

**22 emosi** (`RoboEmotionMap.h`): happy, curious, shy, love, sad, surprised, thinking, agree, disagree, wink, laugh, sleepy, annoyed, dizzy, excited, calm, bored, proud, anxious, relieved, mischievous, lonely.
- Tiga level intensitas mengatur ukuran, volume, dan durasi. "annoyed" dibatasi 0,45.
- Wajah kuat pada intensitas tinggi: happy menjadi Happy, love menjadi Heart, sad menjadi Crying, surprised menjadi Shock, dizzy menjadi Fail (X X), proud menjadi Cool.
- Alias: sulking, subdued, affectionate, restless, cheerful, neutral, tired, bashful, worried, playful.

**Efek suara (21 SonicCue):** Affection, Curious, Dizzy, Error, Failure, Giggle, Greeting, Happy, Hmph, Notification, Pickup, PutDown, Sad, Shake, Sleepy, Snore, Success, Surprise, Thinking, WakeAck, Yawn.
- Anggaran suara spontan maksimal 6 per jam (`RoboSoundBudget.h`), nol saat jam tenang. Suara yang dipicu pengguna tidak dihitung.
- Suara tidak pernah diputar di atas ucapan Gemini.

**Klip sudut** (`RoboSpriteClips.h`): 8 klip, 32×32 piksel, 12 frame, 100 ms/frame, tahan 3 detik, jeda minimal 5 detik untuk tier rendah.
- hati (love, affectionate), kilau (happy, excited, proud), not (laugh, agree), Zzz (sleepy), tetes (sad, anxious, lonely), tanda tanya (curious, thinking), marah (annoyed), pusing (dizzy).
- Frame dibuat saat boot di PSRAM (12 KB).
- Tidak tampil saat tidur nyenyak, saat ada overlay bawah, atau saat overlay aktivitas penuh aktif.
- Digambar transparan dengan halo hitam 1 piksel.
- Hook: `RoboBrain::setExpressHook`. Wajah yang sedang berbicara tidak memicu klip.
- Sejak 2026-10-16 klip ini hanya menjadi **ikon prop** yang muncul sesekali di sudut kiri atau kanan selama sebuah aksi (3.1); pemutar klip lama diganti.

### 3.1 Aksi pertunjukan gaya EMO (`RoboActor.h`)
Mata selalu tetap tampil. Aksi hanya (a) menggeser seluruh wajah (`moveFace`: goyang, memantul, meloncat, mengangguk, gemetar), (b) memunculkan tangan kecil dari tepi bawah layar (dengan mic saat bernyanyi, melambai, atau bertepuk), dan (c) meminta ekspresi mata acak. Tidak ada frame tersimpan; semua digambar dari rumus dengan pegas sehingga durasi bebas.
- **11 aksi:** nyanyi (5 gerakan), joget (7 gerakan), lambai atau angkat tangan (3), serta 8 emosi (cinta, senang, tertawa, ngantuk, sedih, bingung, marah, pusing) yang hanya menggerakkan wajah + ikon prop.
- **Pemicu:** perintah `/nyanyi` `/joget` `/lambai` (tier Owner, durasi acak: nyanyi 20–45 dtk, joget 18–40 dtk, lambai 3–5,5 dtk); joget otomatis dari ketukan mikrofon (`RoboDance.h`, tempo mengikuti ketukan); emosi dari hook ekspresi dengan peluang 35–75% dan jeda acak 8–22 dtk untuk tier rendah.
- **Anti-pengulangan:** gerakan berikutnya diundi tertimbang dan tidak sama dengan dua gerakan terakhir; amplitudo, kecepatan, arah cermin, dan fase diacak per gerakan; derau berlapis pada tiap sendi; panjang joget dalam 2–16 ketukan; ikon prop dan isyarat mata muncul pada waktu acak.
- **Batas:** tidak tampil saat mic dimatikan, ada alert, overlay bawah (navigasi, notifikasi, passkey, game), atau tidur nyenyak. Selama aksi, perilaku otonom menunggu (lease `RoboArbiter`) dan mesin mata dipaksa menyajikan frame tiap ±33 ms.
- **Belum:** level suara speaker belum disambungkan ke mulut/mic (`Actor::setVoice` tersedia); belum dilihat di OLED oleh pemilik.

---

## 4. Perilaku otomatis

- **Mata hidup (LivingEyes, pustaka luar repo):** kedip, mikro-gerak, pupil, alis, bulu mata, gaya mata (Klasik atau Kontur).
  - Perilaku otonom dibuka lewat AUTOGATE: jendela 6 detik, jeda acak 40–120 detik. Gerbang ini hanya terbuka jika tidak ada lease arbiter, tidak ada navigasi atau notifikasi, tidak sibuk, dan bangun.
  - Audit drift tiap 250 ms memaksa pupil dan alis sesuai pengaturan.
- **Aksi idle lapis B** (`RoboIdleActions.h`, jeda 30–120 detik, hanya bangun dan ada orang):
  - scan, intip kiri, intip kanan, giggle malu, menguap, kedip ganda, kedip sebelah, putar mata, mengangguk, percaya diri, licik.
  - Tiga aksi terakhir tidak diulang. Setelah interaksi menunggu sekitar 25 detik.
- **Mood ambient** (`RoboMoodAmbient.h`): emosi latar dari mood, maksimal tiap 2–3 menit. Pemasangannya belum dikonfirmasi sub agent.
- **Pikiran (CharacterMind / `RoboBrain.h`):** energi, rasa ingin tahu, bosan, kebutuhan sosial; Isolation tiap 20 menit tanpa sentuhan dan suara menurunkan kepercayaan. Umpan balik `express_character` menggeser mood.
- **Tidur** (`RoboSleepCycle.h`): Awake, Drowsy, LightSleep, DeepSleep, Waking.
  - **Alur:**
    - Awake ke Drowsy jika (tanpa orang dan 10 menit tanpa interaksi), atau energi di bawah 0,25, atau jam tenang.
    - Drowsy ke LightSleep 2 menit.
    - LightSleep ke DeepSleep 30 menit (60 detik saat jam tenang).
    - Waking 3 detik.
    - Doze ulang 20 detik setelah bangun malam atau alarm.
  - **Pembangun:** sentuh, kata bangun, PIR (tidak saat jam tenang), alarm, RemoteSleep/RemoteWake dari app.
- **Mimpi** (`RoboSmallMoments.h`): hanya DeepSleep, jeda 45–90 menit, 1–2 detik, dengan dengkur dan OLED menyala.
- **Ritme harian** (`RoboDailyRhythm.h`): sapaan pagi sekali sehari di PIR pertama setelah jam tenang (jam sinkron, sampai siang), tampilan pagi mingguan bergilir (happy, curious, excited, love, wink, calm, laugh), suara Greeting. Setelah 18:00 energi ×0,8 dan volume ×0,6.
- **Tonggak** (`RoboMilestones.h`): hari ke-7, 30, 100, 365; "kangen" setelah mati 48 jam atau lebih; NVS maksimal 1 tulis per hari. **Ulang tahun pemilik** (29 Feb dirayakan 28 Feb).
- **Ritual kehadiran** (`PresenceRitual.h`): satu sapaan saat tiba atau kembali (PIR), tanpa identifikasi dan tanpa riwayat.
- **Joget** (`RoboDance.h`): 8 ketukan konsisten, 60–200 BPM, berhenti setelah 4 detik tanpa ketukan atau 60 detik, jeda 5 menit. Tanpa toggle. Kini disertai tangan dan goyang wajah (`RoboActor.h`, 3.1). Diblokir saat bicara, mic mute, atau tidur.
- **Suara keras** (`RoboLoudSound.h`): lonjakan sekitar 8 kali dasar, transien maksimal 3 frame, jeda 8 detik. Mati saat percakapan, mic mute, atau ada musik.
- **Persona** (`RoboPersonalityPreset.h`): Kalem, Ceria, Usil (skala kehidupan wajah dan offset sifat awal).
- **Aktivitas pendamping** (13): Curious Look, Expression Practice, Rhythm Play, Daydream, Stretch Reset, Rest, Quiet Company, Pixel Doodle, Watch Room, Touch Play, Rhythm Improv, Focus Company, Calm Breathing.
  - Scheduler (`CompanionScheduler.h`): idle minimal 30 detik, jeda 2–5 menit (intensitas 0: 5–10, intensitas 2: 1–3), durasi 20–90 detik, 3 aktivitas terakhir tidak diulang, Rhythm* ditahan saat jam tenang.
  - Visual OLED hanya untuk Pixel Doodle (8), Touch Play (10, strip), Focus Company (12, strip), Calm Breathing (13, layar penuh).
- **Pembelajaran preferensi** (`PreferenceLearner.h`): opsional, agregat saja. Tidak belajar otomatis, hanya dari penilaian favorit di dashboard. Bias dikunci per kelompok aktivitas.
- **Peringatan kenyamanan** (`RobotFeatures.h`): maksimal satu per 15 menit, hanya di luar 18–30 °C atau 30–70% RH (bisa diatur), butuh data AHT segar (30 detik). Tampil sebagai strip alert 10 detik dengan suara Notification dan ekspresi happy. Teks berbahasa Inggris.
- **Briefing harian:** default menit ke-480 (08:00). Isi "Briefing: <rutinitas>; room T C RH%". Status hari hanya di RAM, jadi setelah reboot di hari yang sama bisa terulang.
- **Pomodoro:** fokus, istirahat, istirahat panjang tiap 4 ronde, fokus maksimal 90 menit. Event lewat alert. Setelah reboot dilanjutkan sebagai terganggu.
- **Jam tenang:** sapaan dan suara spontan dilunakkan atau ditiadakan; sentuhan masih dijawab.

---

## 5. Sentuhan, gerak, dan sensor

- **Sentuh kepala** (`RoboTouchMood.h`): tap, tap ganda, elus, reuni; setiap jenis punya 4 varian (ekspresi, suara, intensitas) yang tidak diulang berturut-turut. Pengecualian: privasi dan game selalu menang atas reaksi mood.
- **Kesal dan ngambek:**
  - 5 tap dalam 3 detik membuat robot kesal.
  - Ngambek hanya muncul jika ledakan kedua terjadi dalam 30 detik setelah kesal. Maksimal 3 menit, dan elus panjang mengakhirinya lebih awal.
  - Wajah ngambek (`RoboSulkFace.h`): mata curiga menoleh ke satu sisi, lalu pulih shy ke happy.
  - Pagar etika (tidak menahan mute, privasi, alarm, pengingat, notifikasi, kata bangun) hanya konvensi. Fungsi `sulkBlocks()` tidak pernah dipanggil.
- **Tahan kepala 5 detik:** meminta pairing BLE ke C3.
- **Sentuh samping 1,4 detik:** toggle mic mute atau aktif (juga dari dashboard `privacy`).
- **Game sentuh** (`TouchGame.h`): dimulai dari aktivitas Touch Play, sesi 45 detik, pola Tap-Hold-Tap. Tap maksimal 350 ms, tahan 600–1050 ms. Status: selesai, gagal, batal, habis waktu.
- **IMU MPU6050** (`RoboMotionMood.h`): tilt menggerakkan mata. Digoyang 3 kali dalam 30 detik menghasilkan "dizzy". Jatuh membuat kaget lalu sedih singkat (elus mempersingkat). Diletakkan membuat lega. Diangkat memicu suara Pickup dan memblokir aktivitas.
- **PIR AM312:** kehadiran, bangun, sapaan, jeda idle.
- **AHT20:** suhu dan kelembapan (peringatan kenyamanan, briefing, dashboard). **BMP280:** hanya konteks LLM dan telemetri.
- **Mikrofon INMP441:** kata bangun, perintah offline, VAD, joget, suara keras. Transkrip mentah hanya di RAM. **Speaker MAX98357A:** suara karakter dan ucapan Gemini.
- **IR 38 kHz** (`RoboIr.h`, RMT, tidak memblokir audio): tanpa perilaku otomatis dan tanpa pola TV atau AC bawaan. 4 slot profil NVS `robodesk_ir`, tangkap maksimal 63 simbol, `ir_send` mengirim frame NEC 32-bit mentah. Hanya lewat dashboard atau BLE, hanya build dual-board S3.
- **Baterai:** `RoboBatteryMonitor.h` membaca ADC dan menghaluskan nilai. Ambang mentah: rendah ≤3500 mV, kritis ≤3300 mV. Hanya dikirim sebagai telemetri (`valid/millivolts/low/critical`). `RoboBatteryPolicy.h` (histeresis) ada dan teruji tetapi **belum terpasang** di firmware. Tidak ada peringatan baterai dan nilainya tidak tampil di app.

---

## 6. Layar OLED

- **Jalur gambar:** `RoboDeskOledSurface::present()` di `RoboDeskSonicCharacter.ino` adalah satu-satunya jalur ke panel. Mata digambar dulu, lalu overlay, lalu kirim, lalu buffer dikembalikan supaya mata tidak berisi overlay.
- **Urutan overlay** (rantai `if/else`, pemilih `OverlayState` di `RoboArbiter.h`): passkey pairing, navigasi Maps, kartu notifikasi, strip game sentuh, overlay aktivitas, banner "MIC OFF - PRIVACY", "PAIRING TERBUKA", strip alert lokal.
  - Banner "MIC OFF" dan strip alert/pairing mengalah kepada kartu penuh dan strip game (`micBannerAllowed`, `stripAllowed`).
  - Sprite sudut tidak tampil saat overlay bawah aktif.
  - Banner pairing dan balasan layar penuh (dual-board) digambar terakhir dan menimpa overlay lain kecuali passkey.
- **Ikon animasi (8):** bel, pesan, telepon, surat, timer, mikrofon, centang, silang. Dipilih dari nama aplikasi dengan pencocokan substring.
- **Proteksi burn-in** (`RoboOledProtection.h`): kontras 0xCF/0x40/0x30/0x08 sesuai keadaan tidur, panel mati total saat tidur nyenyak (overlay yang harus dibaca memaksa nyala), geser mata 1–2 piksel tiap 3 menit.

---

## 7. Arbitrasi anti-tabrakan (`RoboArbiter.h`)

- **Tujuh tier (kecil menang):** Safety, Owner, Emotion, Urgent, Routine, Activity, Micro.
- **`decide()`** menghasilkan Start, Interrupt, Defer, atau Drop. Tier Owner dan Safety yang sama saling menggantikan (sentuhan terbaru menang). Klip yang diinterupsi tidak dipotong keras (ease 150 ms).
- **`Tracker`:** satu slot lease yang kedaluwarsa sendiri. Dipakai untuk ekspresi, sentuhan, jatuh, joget, dan lain-lain.
- **`clipPriority(tier)`** memberi prioritas klip LivingEyes.
- **Ditambahkan 2026-10-16:**
  - `OverlayState`, `micBannerAllowed`, `stripAllowed`, `autonomousAllowed`.
  - Pemain aksi (`RoboActor.h`) memakai `Tracker` sendiri dan menyalinnya ke `arbTracker` tiap loop supaya otonom menunggu.
- **Celah:** overlay belum ikut sistem lease secara penuh (hanya aturan prioritas statis). Tracker hanya satu slot tanpa antrian.
- **Gerbang lain:**
  - Scheduler companion berhenti (`ownerPaused`) saat tidur, DND, aktivitas, navigasi, atau notifikasi.
  - Aksi idle dan AUTOGATE diblokir saat `busy` (percakapan, sentuhan, game, dan lain-lain).
  - Render OLED dilewati saat antrean mic penuh atau speaker butuh isi.

---

## 8. Pengaturan (`RuntimeSettings.h`, diubah dari app atau dashboard)

- **Memori dan privasi:** `autoMemory`, `memoryEnabled`, `backgroundDailyLimit`, `privacyMicMuted`, `cloudConsent`, `interactionMetrics`.
- **Input dan suara:** `inputMode`, `speakerGain`, VAD (`vadStart/End*`, `vadEndMs`), `postSpeakGuardMs`, `wakeFollowupMs`, `voiceReplyMode`, `chatReplyMode`, `speechEnabled`, `masterSound`.
- **SFX:** `characterSfx`, `wakeSfx`, `touchSfx`, `motionSfx`, `notificationSfx`, `sonicFrequency`, `sfxIntensity`, `quietHoursEnabled`, `quietStart/End`, `quietGain`.
- **Proaktif:** `proactiveVisual`, `proactiveVoice`, `characterIntensity`, `personalityPreset`.
- **Wajah:** `faceLifeEnabled`, `facePupils`, `faceBrows`, `faceLashes`, `faceAutoBrows`, `faceAutoLashes`, `mouthMode`, `facePupilLessActing`, `faceMicro`, `faceSilhouette`, `faceGazeReach`, `faceEyeStyle`.
- **Kenyamanan dan briefing:** `comfortAlertsEnabled`, `comfortMin/MaxHumidity`, `dailyBriefingEnabled`, `dailyBriefingMinute`.
- **Pomodoro:** `pomodoroFocusMinutes`, `pomodoroBreakMinutes`, `pomodoroLongBreakMinutes`.
- **Lain-lain:** `ownerBirthMonth/Day`, `phonePlatform`, `navigationEnabled`, tiga slot Wi-Fi, kunci Gemini (RAM saja di S3).
- **Tidak di app:** `adminPin`, `phoneBlePeer`, status internal pomodoro, Wi-Fi dan kunci Gemini (hanya bisa ditulis).

---

## 9. App Android (AILI, `mobile/android/`)

- **Lima tab:** Robot, Ngobrol, Main & Alat, Kenangan, Setelan.
  - **Ngobrol:** giliran Gemini hanya ditandai "terkirim ke robot" tanpa jawaban. Perintah lokal dibalas teks di app.
  - **Main & Alat:** pengingat (layar `ReminderScreen`), aktivitas, game, dan alat lain.
  - **Kenangan:** milestone, fakta, diary mood (AES-GCM dengan Keystore, 7 hari).
  - **Setelan:** semua pengaturan, Wi-Fi, kunci Gemini, Pembaruan (versi C3 dan S3, OTA), Tentang (hanya versi app dan protokol).
  - Beranda: versi firmware, status link.
  - Onboarding kelahiran robot, 13 jenis sheet.
- **Notifikasi HP ke OLED:**
  - Layanan notifikasi Android, bukan ANCS (`PhoneAncsClient.h` hanya untuk iPhone, `phonePlatform==1`).
  - Redaksi: OFF dan APP_ONLY mengosongkan isi, OTP dibuang, batas 79 dan 180 byte, layar terkunci turun ke APP_ONLY.
  - Navigasi Maps diparse (`MapsNavigationParser.kt`) dan tidak diteruskan saat terkunci.
- **Keamanan:** bridge BLE dengan handshake v2 (identitas robot, nonce, kunci provisioning), envelope terautentikasi (MAC, arah, sesi, counter replay). Backup Android mengecualikan `bridge_secrets` dan `trusted_robot`.
- **Tes:** `tools/test_android_parser.ps1`, `tools/test_android_privacy.ps1`.

---

## 10. Jaringan, keamanan, OTA

- **Gemini Live:** dari S3 lewat relay TCP C3 dengan allowlist. Butuh `cloudConsent` dan jam NTP valid (TLS gagal tanpa epoch). Ada fallback Wi-Fi tiga slot lalu AP pengaturan 192.168.4.1.
- **Dashboard:** PIN admin (`SettingsDashboard.h`), pemeriksaan Origin (`DashboardSecurity.h`). `RoboRpcWebServer.h` S3 selalu mengizinkan, bergantung pada autentikasi C3 dan UART. `/_migration/*` hanya saat jendela migrasi USB terbuka.
- **OTA:**
  - Wajib bertanda tangan ECDSA P-256 (`FirmwareOtaKey.h`, `RoboSignedImage.h`, `tools/ota_signing.py`).
  - GitHub OTA di C3 (`GitHubOtaUpdate.h`): manifest diverifikasi sebelum unduh, image diverifikasi setelahnya.
  - Unggah lokal butuh header `X-Robo-Signature`.
  - S3 menolak versi lama dan tanda tangan salah. OTA aplikasi tidak menulis partisi `model`.
  - `RoboOfflineOta.h` hanya stub di S3.
  - Baca `GITHUB_RELEASES.md` sebelum kerja rilis.
- **Privasi:** transkrip mentah hanya di RAM. Memori tersimpan dan data pengguna diperlakukan sebagai data tidak tepercaya, bukan instruksi. Filter privasi lokal konservatif, bukan jaminan.
- **Risiko yang diketahui:**
  - PIN admin default "robodesk" dan sandi AP "robodesk123" tidak dipaksa diganti. `secrets.h` tidak boleh di-commit.
  - Log C3 mencatat TX=4/RX=5 padahal profil build TX=3/RX=6 (pin sementara).
- **Diagnostik:** audit heap tiap 2 detik dan log `LEV,HEAP,CORRUPT` (`RoboMemoryDiagnostics.h` hanya helper snapshot). `LEV,MEMORY` tiap 30 detik: internal free, min free, blok terbesar, PSRAM free dan min.

---

## 11. Peta file (root = firmware, `*.h` header-only)

| Kelompok | File |
|---|---|
| Entri | `RoboDeskSonicCharacter.ino` (loop, `present()`, dashboard, `runRobotCommand`) |
| Peran build | `RoboBuildRole.h`, `RoboBoardProfile.h`, `RoboRobotRole.h` (S3), `RoboC3Gateway.h` (C3) |
| Tautan S3-C3 | `RoboBoardLink.h`, `RoboLinkProtocol.h`, `RoboDualRuntime.h`, `RoboTunnelProtocol.h`, `RoboLinkTlsClient.h`, `RoboBehaviorWire.h` |
| Otak dan karakter | `RoboBrain.h`, `RoboEmotionMap.h`, `RoboMoodAmbient.h`, `RoboIdleActions.h`, `RoboPersonalityPreset.h`, `RoboArbiter.h`, `CompanionCore.h`, `CompanionActivity*.h`, `CompanionScheduler.h`, `CompanionInteraction.h` |
| Perilaku | `RoboSleepCycle.h`, `RoboDailyRhythm.h`, `RoboMilestones.h`, `RoboSmallMoments.h`, `RoboTouchMood.h`, `RoboMotionMood.h`, `RoboSulkFace.h`, `RoboDance.h`, `RoboLoudSound.h`, `RoboSoundBudget.h`, `PresenceRitual.h`, `PreferenceLearner.h`, `TouchGame.h`, `RobotFeatures.h`, `RoboBatteryPolicy.h` (belum terpasang) |
| Layar | `PhoneDisplayOverlay.h`, `RoboSpriteClips.h`, `RoboActor.h`, `RoboOledProtection.h` |
| Perintah | `RoboCommands.h`, `OfflineVoiceCommands.h`, `WakeWord*.h`, `PhoneChat.h` |
| Gemini | `GeminiLiveDirect.h`, `GeminiStreamParser.h`, `RoboGeminiSink.h`, `MicCapturedFrame.h`, `NativeSpeakerI2S.h`, `RoboPsramByteRing.h` |
| Memori | `BrainMemoryStore.h`, `BackgroundMemoryWorker.h`, `PhoneMemoryOps.h`, `SnapshotStore.h` |
| Ponsel dan BLE | `PhoneBle*.h`, `PhoneBridge*.h`, `PhoneSettingsProtocol.h`, `PhoneCompanionCommand.h`, `PhoneNotificationBridge.h`, `PhoneNavigationBridge.h`, `PhoneAnc*.h` (iPhone) |
| Jaringan | `RoboOfflineNetwork.h`, `RoboExternalFetchS3.h`, `RoboExternalIntegrations.h`, `TimeCompat.h`, `TlsMemory.h`, `GoogleGtsRootR1.h` |
| Dashboard | `SettingsDashboard.h`, `DashboardSecurity.h`, `DashboardJsonWriter.h`, `RoboRpcWebServer.h` |
| OTA | `GitHubOtaUpdate.h`, `OtaRedirectPolicy.h`, `OtaWorkflowPolicy.h`, `RoboSignedImage.h`, `FirmwareOtaKey.h`, `RoboOfflineOta.h` |
| Perangkat keras | `RoboIr.h`, `RoboBatteryMonitor.h`, `RoboAllocation.h`, `RoboMemoryDiagnostics.h`, `RoboLog.h`, `RoboBehaviorLog.h` |
| Pengaturan | `RuntimeSettings.h`, `secrets.example.h` |
| Tes dan alat | `tests/` (host test + stub, jalankan `tests/run_host_tests.ps1`), `tools/` (build, OTA, diagnostik), `.verification/` (log bukti, jangan commit) |

*Pengelompokan file yang tidak saya buka isinya adalah inferensi dari nama.*

---

## 12. Perintah kerja

- **Build kedua board:** `powershell -NoProfile -File tools/build_dual_board.ps1 -Board both`. Build dingin bisa sampai puluhan menit. Sisa flash C3 sekitar 4%.
- **Host test:** `powershell -NoProfile -File tests/run_host_tests.ps1 | tail -n 50`.
- **Android:** `tools/test_android_parser.ps1`, `tools/test_android_privacy.ps1`.
- **Lint:** tidak ada, pakai `git diff --check`.
- **Flash S3:** `esptool.exe` dari paket Arduino ke COM9, alamat `0x400000`, dengan backup 12 potongan 256 KiB lebih dulu (baca 3 MiB sekaligus sering gagal).
- **Monitor serial:** `python tools/serial_monitor.py COMx` (DTR/RTS tidak aktif). Soak: `python tools/serial_health_check.py COMx --cycles 20 --soak-seconds 7200`.

---

## 13. Belum atau terhalang (per 2026-10-16)

- **Belum diuji di perangkat atau HP:**
  - chat input-saja dan perbaikan refresh chat;
  - perilaku PD-6 (mood ambient, idle baru, persona pet);
  - notifikasi OLED bergambar;
  - klip sudut dan aksi pertunjukan (`RoboActor.h`: nyanyi, joget, lambai);
  - anti-tabrakan overlay.
- **Terhalang tindakan pemilik atau perangkat:**
  - baterai tidak terpasang (PD-1.9, PD-1.10);
  - Secure Boot v2 dan enkripsi flash (sengaja ditunda);
  - kredensial unik per unit dan build retail;
  - firmware uji pabrik;
  - release APK (butuh keystore);
  - OTA kanal beta (C3 hampir penuh);
  - investigasi heap corruption PD-0.2 dan BLE putus-sambung;
  - pilihan gaya mata default.
- **Tidak ada di robot:** pengenalan objek atau wajah, kontrol perangkat eksternal, dan gerak fisik (tanpa motor).
- **Celah yang diketahui:**
  - Passkey dual-board kini tidak tertimpa overlay lain (diperbaiki 2026-10-16, belum di-flash).
  - Overlay belum memakai lease arbiter penuh.
  - Ikon notifikasi memakai substring.
  - Kebijakan baterai belum terpasang.
  - Teks peringatan kenyamanan berbahasa Inggris.
