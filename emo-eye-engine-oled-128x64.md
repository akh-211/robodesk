# Emo Eye Engine: Lampiran OLED 0,96" (SSD1306 128 × 64)

Lampiran ini menyesuaikan `emo-eye-engine.md` untuk layar monokrom 128 × 64. Bab 4 (dinamika), 9 (mood engine), dan 10 (stimulus dan perilaku) di dokumen utama dipakai apa adanya. Yang berubah: skala, cara menggambar tepi, efek, preset dalam piksel, dan cara mengirim ke layar.

---

## 1. Spesifikasi dan keputusan desain

| Hal | Nilai |
|---|---|
| Driver | SSD1306, I2C alamat `0x3C` (kadang `0x3D`) |
| Resolusi | 128 × 64, 1 bit per piksel |
| Framebuffer | 1024 byte (8 page × 128 kolom, 1 byte = 8 baris vertikal) |
| Pin default ESP32 devkit | SDA = GPIO21, SCL = GPIO22 |
| Warna | Semua efek warna dan glow dihapus. Kecerahan diatur lewat register kontras. |
| Varian dua warna (kuning atas, biru bawah) | 16 baris teratas kuning. Lihat bab 8. |

Konsekuensi utama:

- Tidak ada alpha, glow, atau gradasi. Ganti dengan **dithering** (bab 3).
- Tidak ada warna mood. Ganti dengan **kecerahan** dan **bentuk fx** (bab 5).
- Layar kecil, jadi gerakan sub-piksel terlihat berkedip. Amplitudo kecil dikuantisasi (bab 4).

---

## 2. Skala dan koordinat

Pakai skala seragam agar proporsi mata tidak berubah:

```
S = 0.24                            (tinggi mata normal 150 → 36 px, muat di 64 px)
pusat layar = (64, 32)
cx = 64 + s·gap/2 + ox
cy = 32 + y + oy
```

Semua angka px di dokumen utama dikalikan `S = 0.24`, dengan pengecualian:

| Parameter | Dokumen utama | OLED |
|---|---|---|
| Rentang lirik `Rx, Ry` | 55, 35 | **13 px, 8 px** |
| Napas | 2 px, 4 s | **1 px**, 4 s, dikuantisasi ke bilangan bulat |
| Tinggi minimum saat kedip | 4 px | **2 px** (garis tertutup) |
| Amplitudo `shake` | 3 px | **≥ 1.5 px** (kalau lebih kecil tidak terlihat) |
| Jitter mikro lirik | ± 0.03 | **loncatan ± 1 px**, tiap 300–800 ms |
| Ukuran hati (`Sh`) | `0.55·h` | **`0.42·h` ≈ 15 px** (lebar hati sekitar 34 px, tidak menabrak mata lain) |

Radius sudut tetap `r = ρ·min(w,h)/2`. Batas layar untuk keamanan: tepi luar mata terjauh `≈ 64 + gap/2 + w/2 + Rx ≤ 124`. Semua preset di bab 6 sudah memenuhi ini.

---

## 3. Rasterisasi monokrom

### 3.1 Coverage sub-piksel

Untuk tiap kolom integer `x`, hitung `yT'` dan `yB'` seperti bab 2 dokumen utama (float). Lalu:

```
baris penuh:      dari ceil(yT') sampai floor(yB') - 1        → selalu nyala
baris atas parsial:  row = floor(yT'),  α = 1 - frac(yT')
baris bawah parsial: row = floor(yB'),  α = frac(yB')
```

### 3.2 Ordered dithering (pengganti anti-aliasing)

Piksel parsial menyala jika `α > B(x mod 4, y mod 4)`, dengan matriks Bayer 4 × 4 dinormalisasi:

```
B = 1/16 · | 0  8  2 10 |
           |12  4 14  6 |
           | 3 11  1  9 |
           |15  7 13  5 |
```

Hasilnya tepi terlihat halus walau hanya 1 bit, dan gerakan sub-piksel menjadi pola dither yang bergeser, bukan lompatan.

### 3.3 Menulis ke framebuffer (per kolom, cepat)

Untuk tiap page `p` (baris `8p .. 8p+7`), baris nyala dari `a` sampai `b` (relatif ke page, 0..7):

```
mask = (0xFF << a) & (0xFF >> (7 - b))
buf[p*128 + x] |= mask
```

Piksel parsial ditambahkan satu per satu dengan `buf[(y>>3)*128 + x] |= 1 << (y & 7)`.

### 3.4 Pengganti lapisan visual dokumen utama

| Dokumen utama | OLED |
|---|---|
| Glow | Dihapus. Diganti pulsa kecerahan (bab 7). |
| Gradasi vertikal | Dithering 25% pada 3 baris terbawah mata (opsional). |
| Highlight | Kotak **hitam** 3 × 2 px (menghapus piksel) di `(x0 + 0.22w, yT' + 0.16h)`. Ikut hilang saat kedip. |
| Kotak bezel Emo | Tidak perlu (bezel fisik layar sudah ada). |
| Gaya piksel | Tidak relevan. |

---

## 4. Dinamika yang perlu disesuaikan

- **Spring:** tetap sama. Jalankan dengan sub-step tetap 5 ms (`n = ceil(dt/0.005)` kali) agar stabil di 30 fps.
- **Frame rate target:** 30 fps. Kedip (70 ms tutup, 120 ms buka) tetap terlihat, sekitar 2 dan 4 frame.
- **Kedip:** `h_eff = max(2, h·(1 - B))`. Squash `w_eff = w·(1 + 0.25·B)` dibulatkan ke piksel terdekat.
- **Napas:** `y += round(sin(2π·t/4))`, jadi hanya bergeser 1 px pada puncak.
- **Lirik:** posisi bulat `ox = round(lx·13)`, `oy = round(ly·8)`. Karena dither menangani tepi, lirik dengan `lx` non-bulat tetap halus.

---

## 5. Efek (fx) untuk monokrom

Bitmap kecil memakai format kolom, bit 0 = baris paling atas (seperti font 5 × 7 standar).

| Kode | Implementasi OLED |
|---|---|
| `shake(A, f)` | `ox += A·sin(2π·f·t)` dengan `A ≥ 1.5`. |
| `tremble` | `ox, oy += round(noise)`, amplitudo 1 px, 20 Hz. |
| `tear` | Titik 2 × 3 px jatuh dari `(cx ± 0.25w, yB')`, kecepatan `35 px/s`, ulang tiap 1.4 s, hilang saat keluar layar. Dua tetes dengan fase berbeda. |
| `blush` | Elips 20 × 5 px di bawah mata, diisi pola **kotak-kotak** (checkerboard 50%). |
| `zzz` | Tiga glyph "Z" 5 × 7 di posisi naik ke kanan atas mata, skala 1×. Bitmap Z: `{0x61,0x51,0x49,0x45,0x43}`. |
| `mark !` | Glyph "!" `{0x00,0x00,0x5F,0x00,0x00}` di antara mata, muncul dengan spring ζ 0.3, hilang setelah 1.2 s. |
| `mark ?` | Glyph "?" `{0x02,0x01,0x51,0x09,0x06}`, cara sama seperti "!". |
| `hearts` | Hati kecil 5 × 5 `{0x06,0x0F,0x1E,0x0F,0x06}` naik `y = cy - 25·τ`, `x = cx + 5·sin(3τ + fase)`, hilang di `τ = 1.5`. |
| `sweat` | Tetes 2 × 3 px di sisi luar atas mata, jatuh 20 px/s, ulang tiap 1.2 s. |
| `pulse(a, f)` | Kecerahan berdenyut lewat kontras (bab 7), bukan ukuran. Ukuran hanya berdenyut jika `a ≥ 0.08`. |
| `dim(k)` | Kontras `= 255·k` (bab 7). |
| `glitch` | Tiap 100–400 ms selama 70 ms: geser tiap page secara acak horizontal `± 3 px` dengan `memmove`. Tidak ada pemisahan warna. |
| `drift` | Sama seperti dokumen utama, dengan `Rx, Ry` OLED. |

---

## 6. Preset dalam piksel (S = 0.24)

Kolom `ρ`, `tilt`, `lt`, `lb`, `cv`, `fx` **sama persis** dengan tabel 7.1 dokumen utama. Yang berubah hanya ukuran, jarak, dan offset (dalam px layar), plus penyesuaian shape.

| ID | Nama | w | h | gap | y | Catatan OLED |
|---|---|---|---|---|---|---|
| 1 | normal | 31 | 36 | 46 | 0 | |
| 2 | happy | 31 | 29 | 46 | 2 | |
| 3 | laugh | 31 | 24 | 47 | 2 | shake(1.5, 14) |
| 4 | joy (^ ^) | 34 | 17 | 46 | 2 | |
| 5 | love | Sh = 15 | – | 48 | 0 | hati, denyut lewat kontras |
| 6 | angry | 32 | 31 | 48 | 1 | |
| 7 | furious | 34 | 28 | 44 | 1 | shake(1.5, 22) |
| 8 | sad | 30 | 32 | 46 | 2 | |
| 9 | cry | 30 | 29 | 46 | 2 | tear 2 × 3 px |
| 10 | surprised | 36 | 44 | 50 | -1 | mark "!" |
| 11 | scared | 29 | 41 | 55 | 0 | tremble 1 px |
| 12 | sleepy | 32 | 14 | 46 | 3 | kontras ×0.6 |
| 13 | sleeping | 32 | 36 | 46 | 3 | zzz, kontras ×0.4 |
| 14 | suspicious | 32 | 22 | 46 | 2 | |
| 15 | smug | 32 | 23 | 46 | 1 | |
| 16 | excited | 31 | 31 | 47 | -1 | bintang n = 0.55, denyut kontras |
| 17 | dizzy | 31 | 31 | 46 | 0 | spiral, `b = 0.45·31/(4π)` ≈ 1.1, tebal 2 px |
| 18 | dead | 31 | 31 | 46 | 0 | X tebal 3 px |
| 19 | shy | 26 | 29 | 44 | 2 | blush, gaze(-.6, .7) |
| 20 | bored | 32 | 19 | 46 | 2 | drift |
| 21 | thinking | 31 | 34 | 46 | 0 | gaze(.7, -.8), mark "?" |
| 22 | dreamy | 31 | 34 | 46 | 0 | highlight 4 × 3 px |
| 23 | evil | 34 | 24 | 44 | 1 | pulse kontras |
| 24 | focused | 34 | 26 | 46 | 1 | |
| 25 | proud | 31 | 26 | 46 | -1 | |
| 26 | sulky | 32 | 22 | 46 | 2 | gaze(.8, .2) |
| 27 | tired | 34 | 17 | 46 | 5 | kontras ×0.7, drift |
| 28 | listening | 34 | 38 | 47 | 0 | pulse kontras |
| 29 | glitch | 31 | 36 | 46 | 0 | glitch page-shift |
| 30 | loading | 31 | 31 | 46 | 0 | ring, tebal 3 px |
| 31 | low battery | 31 | 17 | 46 | 3 | kontras ×0.4, pulse 0.5 Hz |
| 32 | dome | 31 | 36 | 46 | 0 | ρ .50/.50/.15/.15 |

Preset asimetris (bab 7.2 dokumen utama) di OLED:

| ID | Nama | Mata kiri | Mata kanan |
|---|---|---|---|
| 40, 41 | wink | `joy` (34 × 17) | `normal` (31 × 36) |
| 42 | curious | 29 × 34 | 36 × 42 |
| 43 | skeptical | h 36, lt .35, tilt .30 | h 41 |
| 44 | confused | h 36, tilt -.30 | h 26, tilt .30 |
| 45 | sneaky | h 36, lt .40, tilt .30 | h 36 |
| 46 | one-eyed-shock | h 44 | h 22, tilt .30 |

**Kalau layarmu 128 × 32** (varian 0,91"): pakai `S = 0.12`, `Rx = 8`, `Ry = 4`, dan buang `mark`, `zzz`, `hearts`.

---

## 7. Kecerahan sebagai pengganti warna

SSD1306 punya register kontras `0x81` (nilai 0 sampai 255):

```
kontras = 255 · dim · (1 + a·sin(2π·f·t))      dibatasi ke [10, 255]
```

| Mood | dim | pulse |
|---|---|---|
| Normal, happy, focused | 1.0 | – |
| Sleepy | 0.6 | – |
| Sleeping | 0.4 | – |
| Tired | 0.7 | – |
| Love, excited | 1.0 | a = 0.15, f = 1.5 Hz |
| Evil | 1.0 | a = 0.10, f = 1 Hz |
| Listening | 1.0 | a = 0.10, f = 2 Hz |
| Low battery | 0.4 | a = 0.30, f = 0.5 Hz |

Kirim perintah kontras hanya jika nilainya berubah lebih dari 4 unit (untuk menghemat bus I2C). Transisi kontras memakai spring yang sama dengan parameter lain.

---

## 8. Varian dua warna (kuning atas, biru bawah)

- 16 baris teratas (page 0 dan 1) kuning, sisanya biru. Batas itu tetap, tidak bisa diubah lewat perangkat lunak.
- Dengan pusat `cy = 32`, mata normal (h 36) mengisi baris 14–50, jadi hanya 2 baris atas yang kuning. Tidak mengganggu.
- Manfaatkan zona kuning untuk efek: taruh `zzz`, `mark`, `hearts`, dan `sweat` di zona itu (`y < 16`), dan biarkan mata di zona biru dengan menambahkan `y += 4`.

---

## 9. Pengiriman ke layar dan performa

**Bandwidth I2C** (1024 byte framebuffer penuh, sekitar 9 bit per byte):

| Clock | Waktu 1 frame penuh | Perkiraan fps maksimum |
|---|---|---|
| 100 kHz | sekitar 92 ms | 10 |
| 400 kHz | sekitar 23 ms | 40 |
| 800 kHz | sekitar 12 ms | 80 |
| 1 MHz | sekitar 9 ms | 100 |

Pakai `Wire.setClock(800000)`. Kebanyakan modul SSD1306 tahan sampai 800 kHz atau 1 MHz. Jika layar muncul noise, turunkan ke 400 kHz.

**Dirty page:** hanya kirim page yang berubah. Wilayah mata umumnya page 1 sampai 6 (baris 8–55), jadi 6 × 128 = 768 byte per frame. Bandingkan hash tiap page dengan frame sebelumnya (CRC8 atau XOR kolom) dan lewati page yang sama.

**Library yang disarankan**

- **U8g2** dengan mode `full buffer` (`U8G2_SSD1306_128X64_NONAME_F_HW_I2C`), tulis langsung ke `u8g2.getBufferPtr()`, lalu `updateDisplayArea(0, 1, 16, 6)` untuk kirim page 1–6.
- **Adafruit_SSD1306** juga bisa dengan `getBuffer()`, tapi tidak punya update sebagian, jadi kirim penuh.

**Budget CPU per frame (perkiraan kasar)**

| Langkah | Beban |
|---|---|
| Update spring (≈ 40 parameter, sub-step 5 ms) | < 0.1 ms |
| Rasterisasi 2 mata × ≈ 32 kolom | < 0.5 ms |
| Fx dan dither | < 0.3 ms |
| Kirim I2C (768 B, 800 kHz) | ≈ 9 ms |

Jalankan render di core 1, dan I2C di task yang sama (Wire tidak thread-safe). Sensor dan jaringan di core 0.

---

## 10. Melindungi OLED (burn-in dan umur)

OLED yang menyala sebagian besar waktu dengan bentuk tetap bisa mengalami burn-in.

- **Pixel shift:** geser seluruh gambar `(dx, dy)` sebesar ± 1 px setiap 60 detik (pola acak). Sudah sejalan dengan gerak lirik dan napas.
- **Tidur layar:** setelah `sleeping` lebih dari 3 menit, kirim `0xAE` (display off). Bangunkan dengan `0xAF` saat ada stimulus, lalu jalankan `wake up`.
- **Kontras:** jangan biarkan 255 terus-menerus. Untuk `normal` cukup 180–200 (`dim = 0.75`), naikkan ke 255 hanya saat `excited`, `love`, `surprised`.

---

## 11. Urutan update per frame (versi OLED)

```
1. baca sensor, update mood (v, a) dan stimulus         (dokumen utama bab 9, 10)
2. tentukan preset target atau sekuens aktif            (bab 8, 9)
3. update kedip, lirik (kuantisasi bulat), napas        (bab 4 di lampiran ini)
4. update spring dengan sub-step 5 ms
5. bersihkan framebuffer
6. rasterisasi kolom + dither untuk kedua mata          (bab 3)
7. gambar fx (bitmap kecil, checkerboard, tetes)        (bab 5)
8. terapkan pixel shift, hitung hash tiap page
9. kirim page yang berubah, atur kontras jika perlu     (bab 7, 9)
```

---

## 12. Kerangka kode: penulisan kolom ke framebuffer

```cpp
// Menulis satu kolom mata (yT..yB float) ke framebuffer SSD1306 (128x64)
inline void putColumn(uint8_t* buf, int x, float yT, float yB) {
  if (x < 0 || x > 127 || yB <= yT) return;
  static const uint8_t bayer[4][4] = {
    { 0, 8, 2,10}, {12, 4,14, 6}, { 3,11, 1, 9}, {15, 7,13, 5}
  };
  int   top = (int)ceilf(yT), bot = (int)floorf(yB);      // baris penuh: [top, bot)
  auto  set = [&](int y) { if (y >= 0 && y < 64) buf[(y >> 3) * 128 + x] |= 1 << (y & 7); };

  for (int y = top; y < bot; y++) set(y);
  if (top > 0 || yT != floorf(yT)) {                       // baris atas parsial
    float a = ceilf(yT) - yT;
    if (a * 16.f > bayer[x & 3][(top - 1) & 3]) set(top - 1);
  }
  float a = yB - floorf(yB);                               // baris bawah parsial
  if (a * 16.f > bayer[x & 3][bot & 3]) set(bot);
}

// Kontras (kecerahan), kirim hanya jika berubah > 4
inline void setContrast(uint8_t c) { display.ssd1306_command(0x81); display.ssd1306_command(c); }
```

Fungsi `column()` dan `spring()` di bab 12 dokumen utama dipakai tanpa perubahan: cukup panggil `putColumn(buf, x, yT, yB)` untuk tiap `x` dari `floor(x0)` sampai `ceil(x0 + w_eff)`.
