# Menambah tangan / prop / gerak (resep singkat)

Semua konten performer adalah tabel + fungsi gambar kecil. Mesin (`RoboActor.h`) tidak perlu diubah.

## Prop baru (mic, payung, balon, ...)
1. Rancang bentuknya di `docs/companion/PD-5.2_tangan_mic.html` (tambah ke `PROPS`), lihat hasil piksel 128x64.
2. Salin ke `RoboProps.h` sebagai fungsi `void namaProp(const Brush&, const Frame&, const Pass&, const PropCtx&)`.
   - Gambar di bingkai tangan: `f.at(u, v)`, u = kanan, v = naik searah tangan; titik genggam = `f.at(0,0)`.
   - Pakai `b.circle/cap/line/px`; selalu tambahkan `p.gr` ke radius (halo hitam), detail hitam hanya saat `p.c == 1`.
   - `x.len` = pengali panjang tangkai/tali (dianimasikan), `x.t` = detik, `x.side` = sisi tangan.
3. Tambah satu baris di `props()`: `{"id", "Nama", Group::Tool, 1 /*tangan*/, lenMin, lenDef, lenMax, namaProp}`.
4. `tests/robo_actor_test.cpp` otomatis menguji semua entri (ID unik, di layar, deterministik). Jalankan `tests/run_host_tests.ps1`.

## Gaya tangan baru
Fungsi `HandFn` + satu baris di `handStyles()`. Pilih lewat `Actor::setHandStyle("id")`.

## Memakai
- `Actor::setProp("id")` memaksa prop (nullptr = otomatis per aktivitas); `heldPropId()` / `handStyleId()` membaca pilihan.
- Pemilihan otomatis per aktivitas: `Actor::start()` (`pickProp(Group)`), tanpa pengulangan berurutan.
