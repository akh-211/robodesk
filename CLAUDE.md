# RoboDeskSonicCharacterV2

<!-- Dimuat di SETIAP sesi: jaga < 60 baris. Prosedur panjang -> skills. -->

## Project (cek/isi; menghemat eksplorasi)
- Stack: Arduino-ESP32 3.3.11, C++ header-only (ESP32-C3 gateway + ESP32-S3 robot, UART antar board); Android Kotlin (Gradle 8.11.1, JDK 17); tool Python/PowerShell
- Build firmware: `powershell -NoProfile -File tools/build_dual_board.ps1 -Board both` | Test host: `powershell -NoProfile -File tests/run_host_tests.ps1 | tail -n 50` | Android: `tools/test_android_parser.ps1`, `tools/test_android_privacy.ps1` | Lint: tidak ada (pakai `git diff --check`)
- Struktur: `*.h`/`.ino` di root = firmware; `tests/` host test + stubs; `tools/` build/OTA/diagnostik; `mobile/android/` APK bridge; `docs/{companion,hardware,mobile,integrations}` status & task; `.verification/` log bukti (jangan commit)
- Konvensi: BLE dimiliki C3 (jangan pindah ke S3, brownout); guard heap BLE per profil di `RoboC3Gateway.h`; flash app0 di `0x400000` (bukan `0x10000`); centang `[ ]` di docs hanya dengan bukti perangkat; HP/board tidak tersedia = jangan klaim lulus

## Task-agent (1 task = 1 sesi)
Status task ada di TASKS.md (NOW / NEXT / DONE). Statusline membacanya untuk memberi tahu user kapan /clear, /compact, atau handoff, jadi JAGA FORMAT PERSIS (`- [ ]` / `- [x]`). Baca dengan `head -30`, jangan utuh.
- Permintaan kerja baru = 1 task: tulis 1 baris `- [ ] judul` di NOW pada langkah tool pertama (paralel dengan tool call lain). Lewati untuk tanya-jawab dan perubahan sepele.
- Selesai: centang `- [x]` di NOW dalam satu edit, paralel dengan verifikasi akhir. Tutup dengan satu baris: "Selesai."
- Task baru dari user saat NOW masih aktif: tambah ke NEXT saja, jangan dikerjakan.
- Saat memulai task berikutnya: pindahkan item `[x]` lama ke DONE (simpan maks 5) dalam edit yang sama.
- User ketik "lanjut": jika HANDOFF.md ada, baca itu saja lalu lanjutkan; jika NOW aktif, lanjutkan; jika tidak, pindahkan item pertama NEXT ke NOW lalu kerjakan.
- User ketik "handoff": tulis HANDOFF.md (maks 25 baris: tujuan, sudah selesai, langkah berikut, file tersentuh, jebakan, perintah persis), tanpa hal lain, lalu balas "Siap: /clear lalu ketik lanjut".
- Hapus HANDOFF.md saat task selesai. Jangan commit kecuali diminta.

## Membaca & mencari
- Grep/glob dulu, lalu baca hanya file atau rentang baris yang cocok. Jangan baca file besar utuh untuk orientasi.
- Jangan baca ulang file yang sudah ada di konteks. Abaikan: node_modules, dist, build, lockfile, generated, binary.
- Perlu memahami area besar? Delegasikan ke subagent, minta ringkasan maks 15 baris.

## Mengedit & menjalankan
- Diff sekecil mungkin. Tanpa refactor, rename, format, dokumen, atau test tambahan kecuali diminta. Edit terarah, bukan tulis ulang file.
- Permintaan ambigu: ajukan SATU pertanyaan singkat. Menyentuh 3+ file atau pendekatan belum jelas: rencana maks 5 poin, tunggu OK.
- Jalankan test/lint paling sempit; suite penuh hanya di akhir. Batasi output: `| tail -n 50` atau `grep -E "FAIL|ERROR"`.
- Perbaikan sama gagal 2x: berhenti, laporkan yang dicoba dan dugaan penyebab. Jangan looping.

## Balasan
- Singkat. Tanpa pembuka, tanpa mengulang tugas, tanpa rekap diff. Tampilkan hanya kode yang berubah + 1 baris cara verifikasi.

## Compact instructions
Saat compact, pertahankan: task di NOW, keputusan, path file yang diubah, error/test gagal, langkah berikut. Buang: jalan buntu, isi file penuh, log panjang.
