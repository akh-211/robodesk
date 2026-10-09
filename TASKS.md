# TASKS
<!-- Dibaca statusline. Dikelola Claude; jaga format: bagian NOW / NEXT / DONE, item `- [ ]` / `- [x]`. -->

## NOW
- [x] Dashboard ringan: pangkas HTML/JS/CSS + navigasi agar tidak membebani C3 · 18m · $1.27
- [ ] BLE: nama BLE belum muncul + forget current phone & start pairing belum bisa

## NEXT
- [x] Sesi cloud: gabung panduan CLAUDE.md lama, review kode dual-board/BLE, verifikasi host, handoff
- [ ] Review-1: pairing BLE tolak HP beralamat privat (lease alamat connect vs identitas) — lihat AGENT_HANDOFF.md
- [ ] Review-2/3: link UART reset saat penerima sibuk + key Gemini S3 hilang setelah reset link C3
- [ ] Build C3 untuk cek perbaikan auth /ota/s3 + jalankan run_host_tests.ps1 penuh di PC
- [ ] BLE-H5: pangkas puncak heap boot C3 (buffer RPC idle, gatewaySyncSettings, migrasi sebelum BLE init)
- [ ] v13: perbaiki fitur dashboard (companion activity, activity history, style face) + rilis
- [ ] Fase C (jeda): BLE diag ready=true, heap C3 12 KB; uji iPhone ANCS
- [x] Dashboard C3: upload firmware S3 lokal lewat UART (build+host test lulus; belum diuji di perangkat)

- [ ] Fase A.5-7: gate rilis APK (lint, signing), parser kalender P3, security-review BLE
- [ ] Fase B-D: bring-up, ukur BLE, qualifikasi (butuh board/HP)
- [ ] BLE-H5: BLE C3 masih crash-loop saat guard diturunkan (heap boot min ~2 KB); butuh trimming puncak boot (RPC 12 KB, sync settings) lalu uji HP

## DONE
- [x] Fase A.1-3: baseline test hijau, rekonsiliasi guard BLE di dok, isi CLAUDE.md
- [x] Dashboard: save&reboot tidak menyimpan + blank page; samakan tampilan dgn mockup HTML
- [x] Fase B.1-2: backup + flash C3/S3 (hash OK), S3 heartbeat OK
- [x] A.7: temuan BLE 1,2,4 diperbaiki (build+host test lulus); temuan 3 menunggu keputusan
- [x] BLE-H1: audit peta tanggung jawab C3 vs S3 (TLS, integrasi, RPC, dashboard, OTA, BLE auth) + anggaran RAM per modul (PRD: docs/hardware/BLE_MEMORY_FIX_PLAN.md)
