# Kunci OTA produksi (PD-4.12)

Status 2026-10-13: kunci produksi dibuat dan diuji pada satu image uji (`.verification/pd-4.12/signing-test-20261013.txt`). Firmware **masih** memakai kunci dev (`FirmwareOtaKey.h`). Belum ada rilis yang ditandatangani kunci produksi.

## Lokasi dan penyimpanan
- Dibuat dengan `python tools/ota_signing.py init-production-key --out C:\Users\imipi\RoboDesk-OTA-PRODUCTION-OFFLINE`, di luar repo dan di luar OneDrive.
- Isi folder: `production-signing-key.pem` (PKCS#8 terenkripsi), `production-passphrase.txt` (frasa sandi acak), `production-public-key.pem`, `FirmwareOtaKey.production.h`, `README.txt`.
- Public key SHA-256: `0d92ac541477d47942c86a69964dacca2d9ce0dafb5e6a9c87ff7ee0bef2237f`.
- **Tugas pemilik:** salin file kunci dan file frasa sandi ke **dua media offline yang berbeda** (mis. dua flashdisk di tempat terpisah, atau flashdisk + cetakan kertas untuk frasa sandi). Cek kedua salinan bisa dibaca, lalu hapus folder itu dari PC. Jangan pernah commit, sinkron cloud, atau kirim lewat chat.

## Prosedur menandatangani rilis (setelah kunci produksi aktif)
1. Baca `GITHUB_RELEASES.md`. Tentukan versi berikutnya yang belum terpakai (monoton naik).
2. Build kedua board dengan `tools/build_dual_board.ps1 -Board both`.
3. Colokkan media offline. Tanda tangani per board:
   `python tools/ota_signing.py sign <robot.bin> --version N --board ESP32-S3/robot-link-v1 --key-file E:\production-signing-key.pem --passphrase-file F:\production-passphrase.txt`
   (ulangi untuk gateway dengan `--board ESP32-C3/gateway-link-v1`).
4. Verifikasi: `python tools/ota_signing.py verify <bin> --board ... --public-header E:\FirmwareOtaKey.production.h`.
5. Cabut media offline sebelum mengunggah rilis.

## Rencana rotasi
Firmware hanya menyimpan satu kunci publik, jadi pergantian kunci selalu lewat **rilis jembatan**:
1. **Dev → produksi (sekali, saat rilis jual pertama):** ganti isi `FirmwareOtaKey.h` dengan `FirmwareOtaKey.production.h`, build, lalu tandatangani rilis jembatan itu dengan **kunci dev** (kunci yang dipercaya unit terpasang). Setelah unit memasang rilis jembatan, semua rilis berikutnya ditandatangani kunci produksi. Simpan kunci dev sampai semua unit dipastikan sudah lewat jembatan.
2. **Rotasi terjadwal (mis. tiap 2 tahun) atau saat media hilang:** buat kunci baru dengan perintah yang sama di folder baru, lalu ulangi pola jembatan: rilis berisi kunci publik baru, ditandatangani kunci lama.
3. **Kunci bocor:** segera buat kunci baru dan rilis jembatan yang ditandatangani kunci lama. Unit yang belum memasang jembatan masih menerima image bertanda tangan kunci bocor; risiko ini hilang setelah semua unit memperbarui. Pertimbangkan dukungan dua kunci di firmware (backlog) agar pencabutan tidak bergantung pada jembatan.
4. **Kunci hilang tanpa salinan:** unit tidak bisa menerima OTA lagi; perbaikan hanya lewat flash USB. Itu alasan dua salinan offline wajib.
