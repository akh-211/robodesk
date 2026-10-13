# Memo PD-4.0: 5 keputusan pemilik

Status: draf 2026-10-13, **menunggu jawaban pemilik**. Jawaban dicatat di [PRODUCT_DIRECTION.md](PRODUCT_DIRECTION.md) bagian 0. Biaya di sini perkiraan kasar untuk membandingkan opsi, bukan penawaran. Aturan regulasi wajib dicek ulang ke sumber resmi sebelum diputuskan.

Cara menjawab: tulis nomor + huruf opsi (mis. `1B, 2A, 3: "Nama", 4A, 5A`).

## 1. Target usia/pasar
| Opsi | Biaya & kerja | Dampak regulasi | Task terdampak |
|---|---|---|---|
| A. 14+ / dewasa (teman meja) | Paling murah. Tanpa mode anak wajib. | Komdigi (Wi-Fi/BLE) tetap wajib. UU PDP berlaku untuk data pengguna. | 4.10, 4.15 (pagar konten umum saja), 4.20 |
| B. Anak (<14) | Mahal: uji SNI mainan (keselamatan mainan + mainan elektrik), mode ramah anak, persetujuan orang tua di app. | SNI mainan wajib di Indonesia. Data anak di UU PDP butuh persetujuan orang tua. Ketentuan Gemini API saat ini melarang aplikasi yang ditujukan/kemungkinan dipakai di bawah 18 tahun: AI cloud harus nonaktif atau diganti. | 4.10, 4.14, 4.15, 4.16, 4.19, 4.20 |
| C. Keluarga (14+ dengan label "bukan mainan") | Sedang. | Seperti A, tapi pemasaran tidak boleh menyasar anak. | Seperti A + teks label 4.10 |

## 2. Akses AI (Gemini)
| Opsi | Biaya & kerja | Dampak | Task terdampak |
|---|---|---|---|
| A. Kunci milik pengguna (seperti sekarang) | Tanpa biaya server. UX setup lebih rumit (buat kunci, tempel di app). | Pengguna menanggung kuota/biaya. Kunci tersimpan di perangkat (perlindungan flash ikut 4.6). | 4.16 (UX kunci), 4.14 (persetujuan audio pertama) |
| B. Proxy milik penjual | Biaya server + API per unit per bulan, perlu operasi 24/7, autentikasi per unit. | Penjual jadi pemroses data (UU PDP: kebijakan privasi, lokasi data, retensi). Kunci Google tak ada di unit. | 4.5, 4.13, 4.14, 4.16, 4.12 |
| C. Tanpa AI cloud di v1 | Termurah, offline penuh. | Paling ringan regulasinya. Fitur chat/percakapan hilang. | 4.16 (hapus jalur), 4.14 sederhana |

## 3. Nama dagang
| Opsi | Kerja | Dampak | Task terdampak |
|---|---|---|---|
| Pemilik mengusulkan 2-3 nama | Cek PDKI (kelas 9 dan 28) + domain/toko. | Jangan "EMO", jangan "Sonic" (bagian 12J). Nama juga jadi wake word bila 4.4 jalan. | 4.4 (wake word kustom, model baru = flash USB), 4.19, 4.3 (onboarding), 4.10 |

## 4. Layar v1
| Opsi | Biaya & kerja | Dampak | Task terdampak |
|---|---|---|---|
| A. Tetap OLED 0,96" 128x64 | Tanpa biaya hardware. Butuh proteksi burn-in (1.3). | Umur panel dibatasi jam nyala; 1.3 + ukuran jam nyala di 1.10. | 1.3, 1.10, 4.18 |
| B. Naik ukuran (OLED 1,3"/1,5" atau TFT warna) | Ubah PCB/rangka, driver baru, renderer lib diuji ulang, BOM naik. | Uji EMC/termal ulang; mundur ke Fase 1. | 1.3, 1.14, 4.7, 4.10, 4.18 (+ backlog v2 maju) |

## 5. Servo kepala
| Opsi | Biaya & kerja | Dampak | Task terdampak |
|---|---|---|---|
| A. Tetap v2 (sesuai bagian 0) | Tanpa biaya v1. | Anggaran daya & brownout lebih mudah. | – |
| B. Masuk v1 | Servo + mekanik + driver; `BodyDriverBridge`/`EmbodimentCoordinator` disambungkan; keselamatan jepit jari. | Arus puncak naik: uji brownout ulang (4.18), uji keselamatan mekanik; jadwal mundur. | 4.6, 4.7, 4.10, 4.18, 4.20 |

## Yang terhalang sampai memo dijawab
- Langsung: 4.15, 4.16, 4.19, 4.10 (lalu 4.20), 4.4.
- Tidak terhalang: semua task Fase 1-3, 4.1-4.3, 4.12, 4.17.
