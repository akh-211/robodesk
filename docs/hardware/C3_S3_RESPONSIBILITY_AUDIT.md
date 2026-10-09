# Audit tanggung jawab C3 vs S3 (BLE-H1, 2026-10-07)

Tujuan: C3 hanya radio (Wi-Fi + BLE) dan S3 memegang proses berat, tanpa S3 memakai radio sendiri (S3 BOD saat Wi-Fi/BLE). Sumber: kode di root repo; angka RAM berlabel **ukur** (dari `BLE_MEMORY_FIX_PLAN.md`) atau **kode** (konstanta di source). Tidak ada angka perkiraan baru yang dianggap terukur.

## Peta saat ini

| Fungsi | Pemilik sekarang | Dasar | Target opsi 2 |
|---|---|---|---|
| Wi-Fi STA/AP, DNS, NTP | C3 | `RoboC3Gateway.h` | Tetap C3 |
| BLE host, SMP/bonding, allowlist | C3 | `PhoneBleTransport.h`, `tools/nimble_gateway_config.h` | Tetap C3 (stack radio); passkey sudah tampil di S3 via UART |
| Framing/HMAC aplikasi BLE (`PhoneBridgeProtocol.h`) | C3 | `PhoneBleTransport.h` | Bisa tetap C3; pindah hanya jika RAM C3 masih kurang (tidak di scope H1-H5) |
| Link UART 921600, COBS+CRC, payload 1024, kredit, sesi | Kedua | `RoboBoardLink.h`, `RoboLinkProtocol.h` | Tetap; dasar tunnel |
| Tunnel TCP (`TcpOpen/State/Data/Close`) | Sudah ada, **plaintext ke Google** | `RoboDualRuntime.h`, `RoboLinkTlsClient.h` | Jadi raw TCP relay generik beralaskan allowlist host |
| TLS Gemini Live (WSS) | **C3** (`gatewayTls` = `WiFiClientSecure`, task 8192 B, CA GTS R1) | `RoboC3Gateway.h:72,240-256` | **S3** (TLS ujung-ke-ujung di atas tunnel) |
| Klien Gemini / WebSocket / audio | S3 (`GeminiLiveDirectClient`, `RoboLinkTlsClient` sebagai `RoboDeskTlsClient`) | `GeminiLiveDirect.h:29` | Tetap S3 |
| Allocator TLS PSRAM-first | S3 sudah aktif | `TlsMemory.h`, `.ino:2034` | Dipakai ulang oleh TLS S3 |
| Integrasi (cuaca/HA/kalender) | **C3**: `esp_http_client` + crt bundle, task 6144 B, buffer 8193 B | `RoboExternalIntegrations.h:104-113,169,186` | **S3** lewat tunnel; konfigurasi/token tetap di NVS C3 atau pindah (keputusan di H3) |
| Cek/instal OTA GitHub (HTTPS) | C3 | `GitHubOtaUpdate.h`, `RoboC3Gateway.h:280-312` | Tetap C3 (jarang; BLE dijeda saat berjalan) |
| Dashboard (WebServer) + proxy RPC ke S3 | C3 (`gatewayProxy`), RPC buffer 12288 B di S3 `RoboDual` | `RoboC3Gateway.h:204`, `RoboDualRuntime.h:9` | Tetap C3 sebagai reverse proxy tipis; hentikan buffering penuh |
| Otak/companion/audio/IR/layar | S3 | `RoboBrain.h`, `.ino` | Tetap S3 |
| BLE di S3 (`RoboRobotRole.h:40-44`, guard `s3-reserve-low`) | S3 (kode ada, tidak dipakai profil produksi) | | Tidak dipakai (BOD) |

## Anggaran RAM internal C3 (tanpa PSRAM)

| Pos | Bytes | Jenis |
|---|---|---|
| Heap sebelum Wi-Fi | 169.392 | ukur |
| Setelah queue BLE (sebelum host) | 74.008–90.388 | ukur |
| Host+queue NimBLE | ~50.000 | ukur |
| Heap saat BLE+Wi-Fi+UART (min / largest) | 11.856 / 13.812 | ukur |
| Wi-Fi AP+STA (idle, tanpa BLE) | heap 75.8 KB, largest 65.5 KB | ukur |
| Task link UART (stack/RX/TX) | 6.144 / 4.096 / 2.048 | kode |
| Task perintah ponsel | 4.096 | kode |
| Task TLS Gemini | 8.192 | kode |
| Task integrasi + buffer respons | 6.144 + 8.193 | kode |
| `gatewayReceive` | 1.024 | kode |
| Guard TLS C3 sekarang | heap ≥ 98.304 dan largest ≥ 32.768 | kode `RoboC3Gateway.h:254` |

Sesi TLS di C3 tidak punya angka ukur terpisah dalam dokumen; yang terukur hanya guard di atas. Perkiraan biaya (sesi mbedTLS ~30–40 KB internal) belum diverifikasi: **ukur pada H5**.

## Temuan penting

1. **Tunnel sudah ada.** UART 921600, kredit, generation, sesi. H2 tidak membangun dari nol, hanya memperluas `TcpOpen` (host/port/mode) dan menegakkan allowlist di C3. Throughput UART ≈ 92 KB/s, di atas kebutuhan audio Gemini (32 KB/s per arah) tetapi bersama frame lain; perlu pengukuran latensi.
2. **TLS Gemini saat ini berakhir di C3**, bukan S3. S3 hanya bicara plaintext lewat UART, jadi memindahkan TLS ke S3 mengubah model keamanan: C3 menjadi relay buta (hanya melihat ciphertext). Pin host `generativelanguage.googleapis.com` harus digantikan allowlist di C3 karena integrasi memakai host lain (`api.open-meteo.com`, host HA pengguna, URL kalender pengguna).
3. **Integrasi memakai `esp_http_client` + crt bundle ESP-IDF di C3.** Memindahkannya ke S3 butuh transport kustom (`esp_transport`) atau klien HTTP sendiri di atas mbedTLS S3 dengan bio tunnel. Ini pekerjaan terbesar di H3.
4. **Puncak TLS C3 bukan penjumlahan.** `externalSafe` sudah menserialisasi integrasi terhadap Gemini (`RoboC3Gateway.h:373`). Setelah Gemini dan integrasi pindah, C3 menahan TLS hanya untuk OTA GitHub; OTA sudah menyetel `gatewaySuspended` dan bisa menjeda BLE.
5. **Host HA pengguna mungkin alamat LAN privat.** Allowlist C3 tidak boleh memblokir RFC1918 yang sengaja dikonfigurasi, tetapi harus menolak target lain yang tidak terdaftar (cegah relay terbuka).
6. **Konfigurasi dan token (HA, kalender) tersimpan di NVS C3** (`roboexternal::save`). Opsi: tetap di C3 dan dikirim S3 saat permintaan (token ikut lewat UART) atau dipindah ke NVS S3. Rekomendasi: tetap di C3, karena dashboard C3 sudah mengelolanya; S3 menerima token hanya di memori selama satu permintaan.
7. **Uji host yang terdampak:** `robo_link_protocol_test`, `robo_dual_runtime_test`, `robo_link_queue_test`, `tls_memory_test`, `test_dual_board_contract.py`, `test_ble_profile.py`.

## Keputusan desain untuk H2-H5

- H2: perluas `TcpOpen` payload menjadi `generation(4) + port(2) + flags(1) + hostLen(1) + host`; C3 menolak host di luar allowlist; mode raw (tanpa TLS di C3). Versi protokol dinaikkan agar C3/S3 lama tidak salah paham.
- H3: S3 mengimplementasikan TLS memakai mbedTLS dengan bio `RoboDual.writeTcp/readTcp`, allocator PSRAM; Gemini lebih dulu, integrasi sesudahnya.
- H4: setelah H3 terbukti di perangkat, hapus TLS worker Gemini dan task integrasi di C3; dashboard tetap di C3.
- H5: turunkan guard BLE berdasarkan pengukuran, bukan angka tebakan; wajib uji perangkat/HP.

## BLE-H2: tunnel TCP (2026-10-07)

- Wire: `TcpOpen` v2 = `generation(4) port(2) flags(1) hostLen(1) host` (`RoboTunnelProtocol.h`); payload 4 byte tetap diterima sebagai mode lama (TLS Google di C3). Flag `RawTcp=1`: C3 hanya merelay byte. Flag/panjang/karakter host tak dikenal ditolak.
- C3 (`RoboC3Gateway.h`): `gatewayTunnelAllowed` = Gemini + `api.open-meteo.com` + host HA/kalender yang dikonfigurasi (port persis, tanpa wildcard); relay memakai `WiFiClient` + task 5.120 B; DNS terjadi di C3 lewat nama host. NTP tetap lewat frame `Clock`. Guard `GatewayRelayReserve=24576`/`LargestBlock=8192` **belum diukur**; H5 menggantinya.
- S3 belum memakai mode raw sampai H3 (`RoboLinkTlsClient` masih mengirim TcpOpen lama).
- Kapasitas teoretis (belum diukur di perangkat): UART 921.600 baud 8N1 = 92.160 B/s per arah; overhead frame 16 B + gen 4 B + COBS ≈ 2 % → ~90 KB/s payload. Satu frame 1.024 B ≈ 11,3 ms di kabel. Audio Gemini 16 kHz PCM16 ≈ 32 KB/s ke atas, 24 kHz PCM16 ≈ 48 KB/s ke bawah → ±36 % dan ±53 % kapasitas sebelum overhead TLS/WebSocket. Ring `TcpCapacity` 4.096 B ≈ 85 ms audio turun; latensi/credit stall harus diukur pada H5 (log `qmax`/`streamRejected` sudah ada).
- Catatan keamanan untuk H3: kunci API Gemini kini hanya ada di C3 (header disuntik C3). TLS ujung-ke-ujung di S3 berarti kunci harus dikirim ke S3 lewat UART sekali per sesi dan hanya disimpan di RAM; ini keputusan yang perlu kamu setujui sebelum H3 dikerjakan.

## BLE-H3: TLS di S3 (2026-10-07)

- `RoboLinkTlsClient.h` kini mbedTLS penuh di S3: `TcpOpen` v2 RawTcp ke C3, handshake memakai bio `RoboDual.readTcp/writeTcp`, verifikasi CA GTS R1 (atau `setInsecure` bila `GEMINI_TLS_INSECURE`), waktu dari frame `Clock`, SNI host. Allocator TLS PSRAM-first kini dipasang juga di S3 (`.ino` setup).
- Kunci Gemini: frame baru `robolink::GeminiKey` (C3 -> S3, `gatewayServiceKey`) dikirim sekali per sesi link dan saat kunci berubah/dihapus; S3 menyimpannya hanya di RAM (`RoboDualRuntime::copyGeminiKey`), dihapus saat link putus. Gemini Live dan `BackgroundMemoryWorker` memakai kunci ini; placeholder `__ROBODESK_GATEWAY__` tidak lagi dipakai oleh klien S3.
- Jalur lama C3 (TLS Gemini, penyuntikan header) belum dihapus; itu H4 setelah jalur baru lolos uji perangkat. Tanpa uji perangkat, penghapusan akan menghilangkan satu-satunya fallback.
- Belum selesai: integrasi (cuaca/HA/kalender) masih di C3 karena butuh token HA dan URL kalender (rahasia) di S3. Persetujuan terpisah diperlukan.
- Verifikasi: build C3 1.885.600 B (margin 80.480) dan S3 2.188.576 B (margin 957.152); seluruh test host dan 39 test pytest lulus. Tidak ada uji handshake nyata (host test tidak mencakup mbedTLS).

## BLE-H3b: integrasi via S3 (2026-10-07)

- C3 tetap pemilik konfigurasi, dashboard, parsing dan cache; hanya transport HTTPS yang pindah. `roboexternal::remoteFetch` (diisi `gatewayExternalFetch`) mengirim `POST /_external/fetch` (url, bearer) lalu polling `GET /_external/fetch/result` tiap 150 ms (maks 15 s). Tanpa hook (build satu-board) jalur `esp_http_client` lama tetap dipakai.
- S3 (`RoboExternalFetchS3.h`): task 12 KB, `HTTPClient` + `RoboLinkTlsClient` dengan crt bundle ESP-IDF, tanpa redirect, body maks 8 KB di PSRAM, URL/token dihapus setelah selesai. Kode balasan: 202 mulai, 204 berjalan, 200 body, 422 status upstream, 413 kebesaran, 502 jaringan, 503 tunnel dipakai Gemini/memory worker.
- Arbitrasi tunnel: `RoboDual.tunnelBusy` mencegah Gemini/memory worker mulai saat fetch berjalan; fetch ditolak saat Gemini terhubung. C3 hanya mengizinkan host yang dikonfigurasi (allowlist).
- Yang diterima S3 kini termasuk token HA dan URL kalender, per permintaan dan hanya di RAM (disetujui pengguna 2026-10-07).
- Build: C3 1.886.400 B (margin 79.680), S3 2.262.304 B (margin 883.424); 36 test host dan 39 pytest lulus. Belum diuji di perangkat.

## Uji perangkat dan BLE-H4/H5 (2026-10-07)

Perangkat: C3 `COM16` (MAC 44:bd:8d:24:91:50), S3 `COM9` (MAC 68:ee:8f:4b:63:7c). Slot aktif: C3 app0 `0x10000`, S3 app0 `0x400000` (tabel partisi dan otadata dibaca dari perangkat). Backup baru: C3 `0x0-0x1f0000`, S3 `0x0-0x10000` dan app0 (folder scratchpad sesi, bukan di repo). Semua tulis flash `--no-stub` dengan hash terverifikasi (stub S3 gagal membaca).

Hasil terukur di perangkat:
- Link C3-S3 stabil: `connected=1,internet=1`, retries 0, CRC 0; S3 internal heap ~169 KB, PSRAM ~8,2 MB.
- Tunnel + TLS S3 (self-test sementara, sudah dihapus): HTTPS ke `api.open-meteo.com` lewat relay C3 -> `code=200`, 321 byte; handshake `generativelanguage.googleapis.com` (CA GTS R1) berhasil dalam 850 ms. Gemini Live penuh belum diuji: C3 belum punya kunci Gemini valid (`key=0`), kunci diatur lewat dashboard.
- BLE di C3 saat guard diturunkan (reserve 80 KiB, floor 32 KiB): BLE ready, heap setelah start 50 KB, lalu mapan ~30 KB (largest 20 KB). Namun puncak transien setelah boot menurunkan heap sampai ~2 KB dan task watchdog me-reboot C3 berulang (9 kali dalam 8 menit). Profil ini **tidak aman**; guard dikembalikan ke 140 KiB/80 KiB (BLE off). C3 produksi 4 menit: heap 84 KB, minimum 73 KB, tanpa reset.
- Kandidat berikutnya (belum dikerjakan): alokasikan buffer RPC 12 KB sebelum BLE init atau tunda BLE sampai sinkronisasi pertama selesai; kecilkan buffer/JSON sinkronisasi settings; ukur ulang dengan log alokasi.
- Belum terbukti di perangkat: Gemini Live end-to-end, fetch integrasi dengan konfigurasi nyata, pairing HP, ANCS, stabilitas BLE 15 menit.

### Pelacak heap C3 (2026-10-07, build diagnostik)

`gatewayTraceMin` (tetap di firmware, hanya mencetak saat minimum historis turun >= 2 KB) dan log `[Key]` menunjukkan, dengan BLE aktif dan C3 idle selama >10 menit: heap mapan ~30 KB, minimum 19,6 KB, tanpa crash. Puncak terbesar berasal dari RPC `/_migration/get` (~10 KB), alokasi buffer RPC 12 KB di `RoboDual` dan dashboard (~6 KB). Crash watchdog sebelumnya terjadi bersamaan dengan penyimpanan dashboard (kunci Gemini tidak tersimpan; `[Key] valid=0`), jadi beban dashboard saat BLE aktif belum aman. Guard produksi tetap 140 KiB/80 KiB (BLE off). Kandidat penghematan: bebaskan buffer RPC saat idle, hapus string `before/after` di `gatewaySyncSettings`, jalankan migrasi sebelum BLE init.
