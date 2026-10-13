# Catatan Data Safety Google Play (DRAF PD-4.8)

Jawaban formulir Data Safety untuk aplikasi AILI. Cocokkan lagi dengan kode sebelum submit; ini bukan nasihat hukum.

| Pertanyaan | Jawaban | Dasar |
|---|---|---|
| Apakah app mengumpulkan atau membagikan data pengguna ke pengembang/pihak ketiga? | **Tidak dikumpulkan oleh pengembang.** Data hanya berpindah HP ↔ robot milik pengguna lewat BLE terenkripsi | Tidak ada server pengembang; `PRIVACY` di `docs/product/safety-privacy-certification.md` |
| Notifikasi aplikasi lain | Diproses di HP; judul/cuplikan aplikasi pilihan pengguna dikirim ke robot pengguna. Tidak dikirim ke pengembang | `NotificationPrivacy.kt`, penyaring OTP/rahasia; pengungkapan `NOTIFICATION_ACCESS_DISCLOSURE` |
| Lokasi | Izin lokasi hanya untuk pemindaian BLE (Android 11 ke bawah); lintang/bujur cuaca diisi manual dan disimpan di robot | Manifest + layar Integrasi |
| Kredensial (sandi Wi-Fi, kunci Gemini, token Home Assistant) | Dikirim sekali ke robot lewat BLE terenkripsi, tidak disimpan di HP, tidak dibaca balik | `TabScreens.kt` (tulis-saja) |
| Data di HP | Kunci sesi pairing (Keystore), cache memori terenkripsi, diary mood opt-in (terenkripsi, 7 hari) | `MemoryCacheStore.kt`, `BridgeCredentialStore.kt` |
| Enkripsi saat transit | Ya: BLE LE Secure Connections + HMAC per pesan | Protokol bridge v2 |
| Pengguna bisa meminta penghapusan | Ya, di app: Setelan › Privasi › Hapus semua data, serta Lupakan robot | PD-4.14 |
| Audio ke Gemini | Dikirim langsung **dari robot** ke Google memakai kunci pengguna setelah persetujuan; app tidak memproses audio | PD-4.14 `cloudConsent` |

## Sebelum rilis
- Keystore rilis milik pemilik dipasang lewat env `ROBODESK_ANDROID_KEYSTORE`, `..._STORE_PASSWORD`, `..._KEY_ALIAS`, `..._KEY_PASSWORD` (lihat `app/build.gradle.kts`); simpan offline seperti kunci OTA.
- Ganti label app di manifest dari "RoboDesk Bridge" ke nama dagang setelah merek AILI diajukan (PD-4.19).
- Jalankan lint dan uji perangkat (A11 screenshot vs mockup).
