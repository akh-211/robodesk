# Kebijakan keamanan AILI

## Melaporkan kerentanan
Kirim laporan ke **[KONTAK KEAMANAN — diisi pemilik sebelum rilis]**. Jangan membuka issue publik untuk kerentanan.
Sertakan versi firmware (Setelan › Bantuan & tentang), langkah reproduksi, dan dampaknya.

Kami menanggapi dalam 5 hari kerja, memberi perkiraan perbaikan dalam 30 hari, dan mencantumkan pelapor di catatan rilis bila diizinkan.

## Masa dukungan (usulan, menunggu keputusan pemilik)
Pembaruan keamanan diberikan minimal 2 tahun setelah unit terakhir dijual. Pembaruan dikirim lewat OTA bertanda tangan (ECDSA P-256); unit menolak image yang tanda tangannya tidak cocok.

## Yang termasuk
- Firmware robot (ESP32-S3) dan gateway (ESP32-C3), termasuk BLE, Wi-Fi, OTA, dan dashboard lokal.
- Aplikasi Android AILI.

## Desain keamanan singkat
- OTA wajib bertanda tangan; kunci produksi disimpan offline (`docs/companion/OTA_PRODUCTION_KEY.md`).
- Pairing BLE dengan LE Secure Connections dan sesi HMAC; kunci Gemini milik pengguna hanya dikirim terenkripsi dan disimpan di robot.
- Transkrip mentah hanya di RAM; memori tersimpan diperlakukan sebagai data, bukan instruksi.
- Daftar komponen (SBOM CycloneDX) dibangkitkan dari setiap build: `python tools/sbom.py` (otomatis di akhir `tools/build_dual_board.ps1`).
