# RoboDesk Bridge — rencana keamanan, fitur, dan UI Android

**Status: DOKUMEN DESAIN HISTORIS.** Sebagian besar rancangan keamanan, privasi,
bridge dan kontrol companion di bawah sudah tercatat di implementasi APK/firmware.
Bagian temuan baseline dan urutan tahap adalah snapshot sebelum implementasi, bukan
gambaran kondisi terkini. Untuk usulan pekerjaan berikutnya, gunakan
[roadmap review terkini](ANDROID_APP_SECURITY_FEATURE_ROADMAP.md).

Tanggal: 2026-10-05. Baseline: APK debug 1.1.0, Android minimum API 26,
Google Maps melalui notifikasi, C3 sebagai BLE/Wi-Fi gateway, S3 sebagai robot.
Mockup: [22 layar interaktif](android-mockup/index.html).

## 1. Tujuan dan batas pengerjaan

Membuat aplikasi pendamping yang mudah digunakan, memilih robot secara eksplisit,
memverifikasi kedua pihak, dan memberi pengguna kontrol atas data yang dibagikan.
Keberhasilan ditentukan oleh perilaku nyata dan ACK robot, bukan hanya tampilan hijau.

Dokumen ini menyimpan tujuan, keputusan desain awal, dan acceptance gate yang
mendasari pekerjaan sebelumnya. Mockup tetap bahan review; UI native belum mencakup
seluruh 22 layar. Implementasi dan verifikasi yang sudah dilakukan dirangkum pada
bagian status implementasi di bawah. Pengujian perangkat fisik dan persiapan APK
release tetap belum selesai.

Default rancangan:

- Satu robot aktif dan satu ponsel dipercaya; tanpa akun, iklan, analytics, atau server APK.
- Semua aplikasi notifikasi nonaktif pada instalasi pertama; mode awal setelah diaktifkan adalah judul saja.
- Navigasi meminta persetujuan terpisah, default nonaktif pada instalasi baru.
- Saat HP terkunci: nama aplikasi saja; pembacaan cloud tetap nonaktif.
- Tidak menambah akses kamera, mikrofon HP, kontak, file pribadi, GPS, atau root.
- Minimum Android 8/API 26 tetap dipertahankan. GATT callback lama dan baru tetap didukung.
- Antrean tetap di RAM: 12 paket HP, 6 notifikasi robot, TTL 5 menit. Navigasi satu instruksi terbaru, stale 30 detik dan TTL 120 detik.
- Tidak membuat peta penuh, balasan pesan, fitur kesehatan, pengambilan OTP, atau HawkFi pada tahap ini.

## 2. Temuan baseline dan prioritas

| Prioritas | Fakta saat ini | Perubahan yang diusulkan |
|---|---|---|
| P0 | HP terhubung ke iklan pertama dengan UUID RoboDesk | Pilihan robot melalui dialog sistem, simpan asosiasi, dan autentikasi identitas setiap sesi |
| P0 | BLE terenkripsi, tetapi robot memakai `ESP_IO_CAP_NONE` | Pairing dengan proteksi MITM; kode acak pada OLED dan dialog pairing Android |
| P0 | Trust terutama ditentukan C3 terhadap HP | Verifikasi robot dari HP juga; UUID/nama/MAC saja tidak cukup |
| P0 | Allowlist membatasi pengiriman, tetapi listener memiliki akses luas | Default off, tingkat konten per aplikasi, redaksi sebelum antrean, kontrol saat terkunci |
| P0 | APK masih debug; penghapusan kredensial/backup belum menjadi kebijakan aplikasi | Keystore, pengecualian backup, revocation, release signing terpisah |
| P1 | Hanya ada layar native sederhana | Onboarding, lima tab utama, pengaturan, diagnostik, dan kondisi gagal sesuai mockup |
| P1 | Reconnect dan antrean terbatas sudah tersedia | Backoff, ACK, expiry end-to-end, status pengiriman yang akurat |
| P1 | Navigasi membaca format notifikasi Maps | Indikator unknown/stale, panduan izin dan sumber data, tanpa menebak arah/jarak |
| P2 | Kontrol karakter tersedia di dashboard, belum melalui APK | Kanal perintah BLE terbatas, terautentikasi, dengan ACK dan gate robot |

Allowlist aplikasi bukan pembatas izin Android: listener tetap dapat menerima
notifikasi yang dibolehkan sistem. Redaksi pola tidak dijanjikan mengenali semua rahasia.
Android dapat membatasi konten sensitif, termasuk OTP; aplikasi tidak mencoba melewati
pembatasan tersebut. [Dokumentasi Android 15](https://developer.android.com/about/versions/15/behavior-changes-all)

## 3. Urutan implementasi yang diusulkan

### Tahap A — identitas, pairing, dan trust (P0)

1. Pisahkan discovery, bonding, persetujuan dashboard, autentikasi aplikasi, dan kesiapan kirim menjadi state eksplisit. Tidak ada konten notifikasi yang masuk antrean sebelum seluruh gate lulus.
2. Gunakan `CompanionDeviceManager` untuk pilihan robot oleh pengguna. UUID hanya filter discovery. Simpan ID asosiasi dan ID robot; reconnect hanya ke robot ini. Perubahan alamat BLE tidak boleh mengganti identitas yang dipercaya. Jika layanan Location diperlukan oleh dialog sistem pada perangkat lama, jelaskan status tersebut; jangan meminta GPS untuk membaca lokasi. [Panduan Android](https://developer.android.com/develop/connectivity/bluetooth/companion-device-pairing)
3. Ubah pairing Android di C3 menjadi LE Secure Connections dengan MITM dan passkey entry: kode 6 digit acak per window 60 detik ditampilkan pada OLED S3, pengguna memasukkannya pada dialog Bluetooth Android. Dashboard tetap menyetujui peer sesudah bond terautentikasi. Jika S3/OLED tidak tersedia, pairing baru gagal dengan instruksi pemulihan, tanpa fallback Just Works.
4. Setelah bond terautentikasi dan dashboard menyetujui kandidat, C3 membuat kunci bridge acak 32 byte untuk peer itu. Kunci diprovision sekali melalui karakteristik terenkripsi yang dibatasi connection ID dan window enrollment; tidak ditampilkan di dashboard, log, clipboard, atau URL. Android menyimpan kunci dengan proteksi Android Keystore.
5. Setiap koneksi baru harus melakukan challenge-response dua arah HMAC-SHA256, dengan domain/version, ID robot, nonce acak 32 byte dari kedua pihak dan session ID. Tampilkan status terverifikasi hanya sesudah proof kedua pihak valid. Kunci salah, nonce replay, timeout, bond hilang, dan identitas berubah menutup sesi.
6. Semua packet sensitif dan perintah baru menggunakan envelope autentikasi: version, session ID, counter monotonik, kind, payload length, payload, MAC. Counter/nonce tidak dipakai ulang pada sesi yang sama. MAC mencakup seluruh field dan domain arah. BLE tetap menjadi kanal enkripsi; tidak membuat cipher sendiri.
7. Enrollment timeout 60 detik, challenge timeout 10 detik, maksimal 3 kegagalan autentikasi per window. Setelah batas tercapai, tutup pairing sampai pengguna membuka window baru. Session baru tidak menghidupkan kembali paket dari identitas lama.
8. Persistensi key/trust pada C3 dilakukan atomik dan terpisah dari behavior settings; jangan kirim kunci ke S3/UART atau backup migrasi. Tidak mengaktifkan eFuse/secure boot/flash encryption otomatis pada board pengguna. Perlindungan terhadap akses fisik flash memerlukan tahap hardware tersendiri.

State enrollment: `unselected → associated → bonding_mitm → awaiting_dashboard → key_pending → authenticated → ready`. C3 mengirim pesan UART khusus yang hanya berisi kode sementara dan batas waktu, tidak key bridge; S3 menampilkan kode hanya selama pairing dan menghapusnya saat timeout/cancel. Persetujuan dashboard harus memverifikasi bond terenkripsi, MITM dan Secure Connections, bukan hanya alamat candidate.

Transaksi credential: setelah persetujuan, C3 menyimpan record pending dengan version, robot ID, peer identity, key dan integrity check; Android menyimpan credential pending secara terenkripsi sebelum mengirim proof. C3 menandai record active hanya setelah proof valid dan mengirim ACK; Android menandai active setelah ACK. Power loss atau timeout pada record pending membatalkan enrollment pada boot berikutnya. Bond tanpa credential active berarti pairing ulang. ACK yang hilang dipulihkan dengan challenge terhadap record active, tanpa reprovision key ke koneksi lain. Tidak ada state gagal yang membuka pengiriman konten.

Mockup layar kode adalah ilustrasi. Pairing asli menggunakan dialog Android,
bukan kolom APK yang mengintip passkey sebelum autentikasi. Detail protokol
autentikasi wajib melalui review keamanan independen sebelum dirilis.

API SDK yang sudah diperiksa secara read-only: Arduino-ESP32 3.3.11 NimBLE menyediakan `BLESecurity::setAuthenticationMode(true,true,true)`, `setCapability(ESP_IO_CAP_OUT)`, `setPassKey(true,generatedCode)` dan callback `onPassKeyNotify(uint32_t)`. Pilih `ESP_IO_CAP_OUT` untuk display-only; kode memakai ESP hardware RNG dan diformat enam digit, bukan generator `random()` bawaan library. Callback menampilkan kode melalui UART dengan window yang sama. State passkey library bersifat global: hanya satu enrollment aktif, hapus kode saat window tutup, dan tolak koneksi kandidat kedua. Bond lama harus dihapus dan dipair ulang; iPhone juga dapat memerlukan izin sharing notifikasi ulang.

Flag konfigurasi SC tidak boleh dianggap bukti bahwa metode SMP yang diminta benar-benar dinegosiasikan. Saat implementasi, verifikasi authenticated/encrypted/bonded state dan metode/key strength yang dilaporkan stack sebelum provisioning. Jika SDK tidak dapat menegakkan security gate tersebut, hentikan fitur enrollment untuk review—jangan fallback ke Just Works. Android pairing UX dan regresi iPhone tetap merupakan gate perangkat terakhir.

### Tahap B — minimisasi konten dan kontrol privasi (P0)

1. Kebijakan efektif adalah irisan allowlist robot dan pilihan lokal HP. HP boleh mempersempit, tidak dapat memperluas izin robot. Pengaturan allowlist otoritatif tetap di dashboard terautentikasi.
2. Tambahkan mode lokal `off`, `app_only`, `title_only`, dan `snippet`. Default aplikasi baru `off`; saat pertama diaktifkan `title_only`. `snippet` maksimum 80 karakter Unicode dan tetap tidak melebihi 180 byte UTF-8 wire. Judul maksimum 40 karakter dan 79 byte. Truncation tidak memotong karakter UTF-8.
3. Periksa package, status ongoing, permission, lock state, dan kebijakan sebelum menyalin judul/body ke antrean. Aplikasi tidak diizinkan tidak boleh meninggalkan konten di buffer antrean atau log.
4. Redaksi lokal default aktif untuk pola OTP/kode verifikasi, bearer/API token, email, nomor telepon, dan URL yang mengandung parameter kredensial. Konteks OTP memblokir cuplikan seluruhnya, bukan hanya mengganti angka. Jangan menjanjikan redaksi universal; nama orang dan informasi sensitif yang tidak berpola membutuhkan `app_only` atau `off`.
5. Saat HP terkunci, turunkan seluruh mode menjadi `app_only`; default jangan menampilkan isi pada preview APK. Lock, perubahan allowlist, pencabutan izin, pause, dan forget membuang konten tertunda yang tidak lagi diizinkan.
6. Tombol jeda global segera menghentikan pengiriman, membuang antrean HP, dan meminta robot membersihkan notifikasi/navigasi. Tampilkan hasil lokal dan hasil remote terpisah; jika offline, remote belum terkonfirmasi dan tidak diberi label selesai.
7. Pembacaan cloud tidak bisa diaktifkan hanya oleh toggle lokal HP. Gunakan persetujuan robot/dashboard per aplikasi dan tampilkan tujuan Google Gemini sebelum opt-in. APK tetap tanpa izin INTERNET. Tidak menyimpan PIN dashboard atau API key Gemini pada HP.
8. Metadata sesi maksimal 50 entry di RAM, tanpa title/body, full MAC, identifier akun, key, atau kode pairing. Paket test selalu sintetis. Ekspor diagnostik hanya atas tindakan pengguna, ditinjau sebelum Android share sheet; hapus file ekspor sementara setelah selesai/24 jam.

### Tahap C — kredensial, revocation, dan paket release (P0)

1. Gunakan Android Keystore AES-GCM untuk melindungi credential pairing yang disimpan privat. Nonce encryption baru setiap write; gagal membuka credential berarti pairing ulang, bukan bypass. Hardware-backed key dipakai bila tersedia, tetapi tidak menjadi syarat minimum API 26. [Android Keystore](https://developer.android.com/privacy-and-security/keystore)
2. Kecualikan credential, identity trust dan cache dari cloud backup serta device transfer; definisikan aturan untuk Android lama dan baru. Preference UI nonrahasia boleh disimpan. Reinstall/restore tidak otomatis mempercayai robot. [Panduan backup](https://developer.android.com/privacy-and-security/risks/backup-best-practices)
3. Forget menghapus credential, antrean, metadata, dan asosiasi lokal segera. Bila online, kirim revoke dan tunggu ACK sebelum menyatakan peer robot dicabut. Bila offline, beri instruksi forget dari dashboard robot; jangan menyimpan ulang secret demi retry revoke.
4. Activity launcher tetap exported sesuai kebutuhan Android. Listener tetap dilindungi `BIND_NOTIFICATION_LISTENER_SERVICE`; komponen internal lain tidak exported. Tolak deep link, intent extra, dan dashboard URL yang tidak diizinkan. Buka dashboard hanya melalui browser setelah tindakan pengguna, bukan WebView dengan credential tersimpan.
5. Siapkan release build `debuggable=false`, key release tersendiri, fingerprint sertifikat dan checksum APK. Secret signing tidak masuk repository/log. Debug APK lama tidak dapat ditimpa oleh release key berbeda: export hanya preferensi nonrahasia, uninstall debug lalu install release, dan pairing ulang. Pisahkan application ID debug di build berikutnya. [Signing Android](https://developer.android.com/studio/publish/app-signing)

### Tahap D — UI lengkap, reliabilitas, dan baterai (P1)

1. Implementasikan UI native Android mengikuti mockup; pertahankan Kotlin dan pola Views yang ada, tanpa migrasi framework pada tahap ini. Buat komponen kartu/toggle/status bersama dan satu `BridgeUiState`; sumber status berasal dari service, bukan parsing kalimat status.
2. Lima tab: Beranda, Aplikasi, Navigasi, Robot, Lainnya. Onboarding, privacy, robot selection, auth, pemulihan izin, dan forget menjadi alur tersendiri. Semua status loading/denied/offline/unknown memiliki tindakan pemulihan.
3. Reconnect memakai backoff 2/5/10/30/60 detik dengan jitter 20%; retry hanya robot terpilih. Saat manual retry, mulai dari 2 detik. Setelah 5 menit offline, gunakan presence/asosiasi saat API mendukung; pada API lama batasi scan window 10 detik per 60 detik. Tidak ada scan permanen untuk seluruh perangkat.
4. Pertahankan satu operasi GATT in-flight dan timeout 20 detik. Tambahkan request ID, ACK, dedup dan reason codes. Label diterima muncul setelah ACK penerimaan S3; bukan sekadar callback write C3. Jangan memperpanjang TTL karena reconnect/retry. Sisa umur HP ikut diteruskan supaya data tidak hidup lagi lima menit dari awal di robot.
5. Navigasi terpisah dari FIFO notifikasi, coalesce latest. Saat tidak punya format/jarak/ETA yang valid, tampilkan unknown/`—`; sumber Maps asli tetap rujukan. Jangan membuat jam tiba yang tidak disediakan sumber. Stop Maps mengirim ended; offline menampilkan umur data.
6. Status izin diperiksa setelah kembali dari settings dan saat service reconnect. Minta POST_NOTIFICATIONS hanya bila menampilkan status aplikasi sendiri. Gunakan fasilitas companion background sesuai API; tidak meminta pengecualian baterai otomatis dan tidak mengambil wake lock tanpa batas. [BLE background Android](https://developer.android.com/develop/connectivity/bluetooth/ble/background)
7. Diagnostik: state autentikasi, selected identity masked, RSSI, MTU, retry, queue depth/age, dropped/coalesced, last ACK, dan S3 link. Telemetry tanpa konten. Hindari label estimasi baterai sampai pengukuran tersedia.
8. Tema dark/light, font scaling 200%, TalkBack, target sentuh minimum 48dp, kontras teks setara WCAG AA, label ikon dan reduced motion. Bahasa awal Indonesia; string resources siap English. Jangan pakai warna sebagai satu-satunya indikator status.

### Tahap E — fitur companion dari HP (P2, sesudah P0 lulus)

- Tambahkan kontrol intensity 0/1/2, aktivitas start/pause/resume/cancel, dan quiet-hours terbatas. Gunakan 13 ID katalog firmware, jangan menciptakan aktivitas baru dari label mockup.
- Semua perintah melewati kanal autentikasi dan allowlist command robot. Robot tetap menerapkan privacy, busy, safety, presence, OTA dan prioritas navigasi; penolakan memiliki reason yang ditampilkan HP.
- Mikrofon robot hanya status/read-only pada versi ini. Tidak ada remote unmute, factory reset, flashing, shell, perubahan Wi-Fi credential atau endpoint Gemini melalui kanal perintah APK.
- Konfigurasi yang tersimpan diproses melalui jalur settings yang ada, dengan validation dan ACK; UI tidak mengklaim perubahan selesai sebelum konfirmasi persistensi.

## 4. Perubahan interface dan kompatibilitas

Pertahankan service `93de0001-2c7d-4a52-9f1c-6f4b4f424c45` dan karakteristik
0002–0006. Jangan mengubah isi config v1 yang dipakai client lama. Tambahkan
karakteristik 0007 untuk identity/capabilities read, 0008 untuk enrollment/auth,
0009 untuk authenticated envelope write, dan 000A untuk encrypted ACK/telemetry notify.

Identity v2 menyatakan stable random robot ID, protocol, role, firmware,
feature flags dan batas ukuran. ID/fingerprint yang sekadar dibaca bukan bukti
keaslian; proof sesi terhadap key pairing yang dipin merupakan bukti trust.

Jenis payload awal: notification upsert/remove, navigation, clear transient,
status query, revoke; companion commands ditambah pada Tahap E. ACK memuat
request ID, status `accepted/rejected/busy/unsupported/expired`, reason code,
dan revision/umur yang relevan, tanpa konten atau secret.

Envelope maksimum 600 byte, payload maksimum 500 byte, dan menyertakan `ageMs` dari antrean asal. Tambahkan fragmentasi untuk control/auth/data pada MTU kecil: header fixed 8 byte berisi version, flags, message ID, total length dan offset; ukuran fragment tidak melebihi `MTU−3`. Reassembly satu pesan per koneksi/arah, buffer fixed 600 byte, urutan offset ketat, timeout 5 detik dan reset saat disconnect. Tidak mengeksekusi payload sampai pesan lengkap, MAC dan counter valid. Long payload tidak boleh dibuang diam-diam hanya karena MTU masih 23.

Migrasi firmware aman menandai peer Android lama `needs_reenrollment`; legacy
reads/diagnostik tetap tersedia tetapi legacy notification/navigation writes ditolak
dengan `unsupported_auth_version`, baik sebelum maupun sesudah enrollment v2.
Key pairing lama tidak dibuat otomatis dari MAC/bond. APK baru pada firmware lama
hanya boleh diagnostik/read-only dan menampilkan instruksi update; tidak downgrade
autentikasi secara diam-diam. ANCS iPhone tetap
jalur terpisah dan tidak membutuhkan APK Android. Perubahan pairing global BLE
wajib memastikan enrollment iPhone juga tetap dapat dilakukan.

Batas resource: gateway C3 terakhir menyisakan 102,320 byte slot OTA. Protokol
memakai kripto SDK yang ada, buffer tetap terbatas, dan tidak menambah TLS/server
baru pada C3. Setiap build wajib mempertahankan margin OTA minimal 64 KiB.
Heap runtime baru dapat dinilai melalui pengujian perangkat.

## 5. Task dan gate penerimaan

| ID | Hasil yang harus tersedia | Gate |
|---|---|---|
| DESIGN-01 | Review 22 layar, default privasi, pairing dan cloud consent | Persetujuan desain; belum implementasi |
| SEC-01 | Threat model + protokol identity/enrollment/auth/ACK versioned | Review keamanan independen |
| SEC-02 | CDM selection, MITM pairing, key storage dan mutual auth | Spoofed UUID/name/MAC, key salah dan replay tidak menerima data |
| DATA-01 | Per-app modes, redaction, lock/pause/revoke gates | Tidak ada konten terlarang pada antrean, log atau backup |
| UI-01 | 22 layar/keadaan native + accessibility + izin sistem | Semua jalur onboarding/pemulihan dapat diselesaikan |
| LINK-01 | Backoff, expiry end-to-end, ACK, diagnostics | Disconnect tidak menduplikasi atau menghidupkan data kedaluwarsa |
| ROBOT-01 | Kontrol companion terbatas | Busy/privacy/OTA menolak command dengan alasan, tanpa bypass |
| REL-01 | APK release signing + migration debug | Fingerprint konsisten; reinstall meminta pairing ulang |
| DEVICE-01 | Matriks HP/board, beban, baterai dan regresi ANCS | Dilakukan terakhir setelah seluruh gate software |

Pemeriksaan software yang harus dibuat nanti: fake GATT/peripheral, malformed
envelope, nonce/counter replay, wrong key, bond/identity change, queue overflow,
MTU 23/247/512, callback timeout, concurrent removal/upsert, permission revoke,
lock/unlock, redaction English/Indonesia, multibyte UTF-8, restore backup dan
rotation/process restart. Skenario UI memakai data sintetis. Build APK serta
kedua firmware, lalu review keamanan dan kompatibilitas sebelum test perangkat.

Pengujian perangkat terakhir:

- API 26, 30, 31, 33 dan 35 atau lebih baru; setidaknya Pixel/AOSP dan satu HP dengan pembatas background vendor.
- Salah robot dengan UUID sama, pairing code salah, bond dihapus, HP/robot reboot, identitas berubah, dan S3 tidak terhubung.
- Pairing Android dan regresi ANCS iPhone; update kedua board, koneksi UART putus/pulih.
- Notifikasi add/update/remove, allowlist, lock/privacy/pause, stop Maps, unknown/stale/expiry dan pergantian jaringan.
- Dua jam beban BLE + Wi-Fi/dashboard + audio: tanpa reset; target C3 internal heap ≥48 KiB dan largest block ≥24 KiB.
- Ukur baterai HP delapan jam connected idle dan delapan jam robot offline, dibandingkan baseline APK nonaktif pada HP yang sama. Ambang proyek: tambahan ≤3 percentage point/8 jam connected idle dan ≤2 percentage point/8 jam offline; ini target pengujian, bukan hasil yang sudah diperoleh.
- Akhiri dengan sign-off hasil nyata dan keputusan release. Jangan menyebut aman/hemat baterai hanya karena build berhasil.

## 6. Status implementasi saat ini (2026-10-05)

- Mockup HTML offline berisi 22 kondisi/layar untuk acuan. APK native memiliki lima bagian utama: Home, Apps, Navigation, Robot, dan More; pemilihan robot, izin, privasi, navigasi, status koneksi, lupakan robot, dan jeda berbagi dapat dijangkau. Variasi layar error/empty dari mockup masih diwakili status dan tindakan pada halaman terkait.
- Pairing memakai Android Companion Device Manager dan identitas robot terverifikasi. Robot mensyaratkan BLE Secure Connections, MITM, bond terenkripsi, autentikasi, key size minimal 16 byte, passkey yang terlihat di OLED, serta persetujuan dashboard.
- Jalur bridge Android-C3 memakai credential per robot dan mutual HMAC handshake yang terikat ke ID robot serta nonce kedua pihak. Envelope v2 mengikat arah/sesi, memeriksa counter replay dan umur pesan, memfragmentasi sesuai MTU, dan menunggu ACK autentikasi sebelum menghapus antrean. Firmware menolak payload tulis legacy.
- Android menyimpan credential terenkripsi dengan Android Keystore, memakai state pending/active, dan mengecualikan data trust dari backup. Forget menghapus trust lokal dan dapat mencabut peer melalui sesi terautentikasi. Jeda berbagi membersihkan antrean ponsel serta meminta robot menghapus notifikasi dan navigasi sementara; status jeda dipulihkan setelah service mulai.
- Kontrol privasi tersedia per aplikasi (`off`, `app_only`, `title_only`, `snippet`), aplikasi baru default off, allowlist ponsel dan robot sama-sama berlaku, konten saat layar terkunci diturunkan, dan ada penyaringan teks berbasis pola. Penyaringan pola tidak menjamin semua rahasia terdeteksi.
- Navigasi membaca instruksi Google Maps yang tersedia sebagai notifikasi; aplikasi tidak meminta izin lokasi dan tidak menghitung rute. Implementasi iPhone ANCS berada di firmware, tetapi belum diuji dengan iPhone fisik.
- Verifikasi lulus: suite host penuh, 11 kontrak source test, framing/handshake/fake-GATT/privacy/envelope tests, kompilasi Kotlin Android debug, dan `assembleDebug`.
- Build gateway C3: 1,885,440 byte dengan margin OTA 80,640 byte (batas minimal build script 65,536 byte). Build standalone S3: 2,079,360 byte dengan margin slot 1,066,368 byte.
- Review keamanan independen menemukan bond kandidat belum disetujui yang dapat tertinggal. Firmware kini menyimpan alamat identitas peer NimBLE dan menghapus bond kandidat yang gagal autentikasi, putus sebelum disetujui, atau kedaluwarsa; persetujuan yang berhasil mempertahankan bond. Review lanjutan mengonfirmasi penghapusan dan penempatan state yang kompatibel dengan build non-NimBLE.
- Belum ada robot atau ponsel Android/iPhone untuk tes fisik. Passkey/OLED, enrollment/approval, force-stop/reconnect, lockscreen, revoke offline, power loss saat state pending, Keystore restore/reinstall, dan matriks versi Android masih menjadi gate lapangan.
- APK release belum siap dipublikasikan: signing key release tidak tersedia di workspace, signing/checksum release belum dilakukan, dan lint vital belum dijalankan karena dependensi lint tidak ada di cache offline. Build berhasil bukan sertifikasi keamanan atau penghematan baterai.

### Sisa gate

- Jalankan matriks uji pada perangkat fisik yang mencakup pairing, persetujuan, pengiriman/ACK, jeda privasi, revoke, reconnect, kehilangan daya, dan pemulihan credential.
- Minta keputusan release setelah hasil uji perangkat, lint release, dan signing key resmi tersedia. Tidak ada firmware yang diflash atau rilis GitHub yang diterbitkan dalam sesi ini.
