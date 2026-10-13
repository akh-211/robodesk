# AILI – Keselamatan, privasi, dan rencana sertifikasi (DRAF PD-4.10, untuk ditinjau pemilik)

Keputusan dasar (PD-4.0): pasar 14+/dewasa di Indonesia, kunci Gemini milik pengguna, OLED 0,96", tanpa servo di v1. Draf ini bukan nasihat hukum; finalisasi dengan konsultan/lab.

## 1. Informasi keselamatan (untuk kemasan dan aplikasi)
- Bukan mainan. Untuk usia 14 tahun ke atas.
- Gunakan adaptor 5 V bersertifikat SNI. Jangan pakai di tempat basah atau di dekat sumber panas.
- Jangan membuka casing. Tidak ada bagian yang dapat diperbaiki pengguna.
- Bila nanti memakai baterai: jangan menusuk, memanaskan, atau mengisi daya baterai yang menggembung; buang sesuai aturan limbah elektronik.
- Hentikan pemakaian bila robot panas berlebih, berbau, atau berasap.
- Perangkat memancarkan gelombang radio (Wi-Fi 2,4 GHz, Bluetooth LE).

## 2. Draf kebijakan privasi (ringkas, mengacu UU 27/2022 tentang PDP)
- **Pengendali data:** [nama badan usaha pemilik + kontak, diisi pemilik].
- **Data di robot:** setelan, fakta yang Anda minta diingat, pengingat, ringkasan percakapan (bisa dimatikan), statistik interaksi lokal tanpa transkrip (opsional). Audio dan transkrip mentah hanya di RAM, tidak disimpan.
- **Data ke pihak ketiga:** saat Anda mengaktifkan percakapan, audio dikirim langsung dari robot ke Google Gemini memakai **kunci milik Anda**, dan diatur oleh ketentuan Google untuk akun Anda. Kami tidak menjalankan server perantara dan tidak menerima audio Anda.
- **Data di HP:** kredensial pairing, cache memori terenkripsi, dan diary mood (opt-in, terenkripsi, 7 hari). Notifikasi HP disaring di HP; kode OTP tidak diteruskan.
- **Hak Anda:** melihat dan menghapus memori, menghapus diary, mematikan mikrofon, menghapus semua data (Setelan › Privasi), dan reset pabrik.
- **Retensi:** data tersimpan sampai Anda menghapusnya atau melakukan reset pabrik.
- **Keamanan:** pairing BLE terenkripsi, pembaruan firmware bertanda tangan, lihat `SECURITY.md`.
- **Anak:** produk ditujukan untuk 14+; tidak sengaja mengumpulkan data anak.

## 3. Rencana uji sertifikasi (Indonesia dulu)
| Kewajiban | Lingkup | Catatan / tindakan |
|---|---|---|
| Sertifikasi perangkat telekomunikasi (Komdigi/SDPPI) | Wi-Fi 2,4 GHz + BLE | Uji di balai uji yang ditunjuk. **Pakai modul radio bersertifikat** untuk produk; papan pengembangan (ESP32-C3 SuperMini, DevKit S3) belum tentu bersertifikat. Cek ketentuan TKDN yang berlaku untuk kategori perangkat. |
| Keselamatan kelistrikan | Adaptor yang dibundel | Adaptor wajib SNI (IEC 62368-1). Bila adaptor tidak dibundel, cantumkan spesifikasi minimal. |
| Baterai (bila dipakai) | 1S Li-ion/LiPo | IEC 62133-2 dan UN38.3 untuk pengiriman (PD-4.18/4.20). |
| EMC | Seluruh unit | Uji emisi/imunitas sesuai persyaratan lab; brownout di volume maksimum diuji di PD-4.18. |
| Pelabelan | Kemasan + unit | Nomor sertifikat, nama importir/produsen, peringatan usia 14+, simbol limbah elektronik. |
| Ekspor (nanti) | FCC (AS), CE-RED (UE) | Ditunda sampai pasar ekspor diputuskan. |

**Urutan kerja:** pilih modul bersertifikat → selesaikan uji bangku (PD-4.18) → golden sample → daftar uji Komdigi → label dan kemasan → produksi (PD-4.20).
