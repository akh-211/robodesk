# Emo Eye Engine: Formulasi untuk ESP32

Dokumen ini hanya berisi formulasi. Satu vektor parameter per mata menghasilkan semua bentuk, dan semua animasi berupa perubahan parameter itu terhadap waktu. Tidak ada aset gambar.

---

## 0. Konvensi

| Simbol | Arti |
|---|---|
| Kanvas referensi | 640 × 400. Semua angka px di dokumen ini memakai ukuran ini. |
| `S` | Faktor skala layar: `S = lebarLayar / 640`. Semua px dikalikan `S` saat render. |
| `dt` | Selisih waktu nyata dari `millis()`, dalam detik. |
| `s` | Sisi mata: `-1` kiri, `+1` kanan. |
| `cx, cy` | Pusat mata (`cx = 320 + s·gap/2 + ox`, `cy = 200 + y + oy`). |
| `x0, y0` | Pojok kiri atas mata: `x0 = cx - w/2`, `y0 = cy - h/2`. |
| `u` | Posisi horizontal ternormalisasi dalam mata: `u = (x - cx)/(w/2)`, rentang `[-1, 1]`. |
| `u_in` | Sisi dalam mata (dekat hidung): `u_in = -s·u`. Bernilai `+1` di sisi dalam, `-1` di sisi luar. |
| `t` | Waktu dalam detik sejak boot. |

---

## 1. Vektor parameter mata

```cpp
struct EyeP {
  float w, h;                 // lebar, tinggi (px)
  float rTL, rTR, rBL, rBR;   // radius sudut, rasio 0..1 dari min(w,h)/2
  float tilt;                 // kelopak atas miring: + marah, - sedih
  float lt;                   // kelopak atas menutup, 0..1 dari h
  float lb;                   // kelopak bawah naik, 0..1 dari h
  float cv;                   // lengkung: + busur ∩, - busur ∪
  float gap, y;               // jarak antar mata, offset vertikal
  float gx, gy;               // arah lirik, -1..1
  uint8_t shape;              // 0 rrect, 1 heart, 2 X, 3 spiral, 4 star, 5 ring
  uint32_t color;             // RGB888
};
```

Setiap preset punya dua salinan (`L` dan `R`). Preset simetris cukup mengisi satu, lalu `R = L`. Preset asimetris (wink, curious, skeptical) mengisi keduanya.

---

## 2. Rasterisasi bentuk mata (per kolom)

Untuk tiap kolom `x` dari `x0` sampai `x0 + w_eff`, hitung `yT` (tepi atas) dan `yB` (tepi bawah), lalu gambar satu garis vertikal. Sekitar 130 kolom per mata, jadi murah untuk ESP32.

### 2.1 Radius sudut

```
r_i = ρ_i · min(w, h) / 2                 untuk i ∈ {TL, TR, BL, BR}
klem: rTL + rTR ≤ w,  rBL + rBR ≤ w,  rTL + rBL ≤ h,  rTR + rBR ≤ h
```

### 2.2 Tepi rounded-rect

```
Tepi atas:
  x < x0 + rTL:       dx = x0 + rTL - x        → yT = y0 + rTL - √(rTL² - dx²)
  x > x0 + w - rTR:   dx = x - (x0 + w - rTR)  → yT = y0 + rTR - √(rTR² - dx²)
  lainnya:            yT = y0

Tepi bawah:
  x < x0 + rBL:       dx = x0 + rBL - x        → yB = y0 + h - rBL + √(rBL² - dx²)
  x > x0 + w - rBR:   dx = x - (x0 + w - rBR)  → yB = y0 + h - rBR + √(rBR² - dx²)
  lainnya:            yB = y0 + h
```

### 2.3 Kelopak dan lengkung

```
yLidTop = y0 + h · max(0,  lt + 0.5·tilt·u_in + 0.5·|tilt|)
yLidBot = y0 + h · (1 - lb)

yT' = max(yT, yLidTop) + cv · h · u²
yB' = min(yB, yLidBot) + cv · h · u²

gambar garis (x, yT') → (x, yB')   hanya jika yB' > yT'
```

Efek tiap parameter:

| Parameter | Efek |
|---|---|
| `tilt > 0` | Sisi dalam kelopak turun: marah. |
| `tilt < 0` | Sisi luar kelopak turun: sedih. |
| `lt` | Kelopak atas merata turun: ngantuk, tidur. |
| `lb` besar + `cv > 0` | Mata berbentuk ^ ^: senang, tertawa. |
| `cv < 0` | Mata tertutup berbentuk ◡: tidur santai. |

### 2.4 Anti-aliasing

Pada kolom tepi dan piksel ujung `yT'`, `yB'`, alpha = bagian pecahan dari koordinat (`frac(yT')`, `1 - frac(yB')`). Campurkan warna mata dengan latar berdasarkan alpha itu.

---

## 3. Lapisan visual (agar terlihat seperti Emo)

Urutan gambar tiap frame, dari belakang ke depan:

1. **Latar** hitam `#04070A`.
2. **Glow**: bentuk yang sama digambar `k = 1..3` kali dengan ukuran `+2k px` dan warna `lerp(latar, warna, 0.25/k)`.
3. **Badan mata** (bab 2) dengan warna penuh.
4. **Gradasi vertikal** (opsional): warna dikalikan `1 - 0.18·(y - yT')/(yB' - yT')`, jadi bagian bawah sedikit lebih gelap.
5. **Highlight**: rrect kecil di `(x0 + 0.22w, yT' + 0.14h)`, ukuran `0.16w × 0.12h`, alpha 0.5. Ikut mengecil bersama `h_eff` saat kedip, dan ikut bergeser `0.3·(ox)` saat lirik agar terasa mengkilap.
6. **Efek tambahan** (bab 6): air mata, blush, zzz, dan sebagainya.

**Kotak bezel Emo** (opsional): layar dipotong dengan rrect radius `0.12·H` dan diberi vignette. Piksel dengan jarak ke pusat `d > 0.75` digelapkan `1 - 1.6·(d - 0.75)`.

**Gaya piksel** (opsional): kuantisasi koordinat ke grid 4 px agar terlihat seperti panel LED.

---

## 4. Dinamika gerakan

### 4.1 Spring per parameter

Setiap parameter numerik `p` (w, h, ρ, tilt, lt, lb, cv, gap, y, gx, gy, warna RGB) punya `v` (kecepatan) dan target `p*`:

```
a = k·(p* - p) - c·v
v += a·dt
p += v·dt

k = 220           (kekakuan default)
c = 2·ζ·√k        (redaman)
```

Nilai `ζ`:

| ζ | Karakter |
|---|---|
| 0.3 | Membal kuat (kaget, senang meledak) |
| 0.5 | Membal ringan (default ekspresi ceria) |
| 0.8 | Cepat menetap (marah) |
| 1.0 | Tanpa overshoot (lirikan, sedih) |
| 1.3 | Lambat dan berat (ngantuk, capek) |

Tiap preset punya `ζ` sendiri (tabel 7.4). Transisi antar bentuk (mis. rrect ke hati) memakai urutan: kecilkan `w` dan `h` ke 20%, ganti `shape`, lalu tumbuhkan lagi dengan spring `ζ = 0.4`.

### 4.2 Kedip

```
durasi tutup = 70 ms,   durasi buka = 120 ms      (dikali faktor mood, lihat 4.2.1)
menutup:  B = (t/70)²
membuka:  τ = (t - 70)/120,   B = (1 - τ)²
h_eff = max(4, h · (1 - B))
w_eff = w · (1 + 0.25·B)              (squash & stretch)
```

Highlight ikut `h_eff`, glow ikut `w_eff`.

**4.2.1 Variasi kedip menurut mood:**

| Kondisi | Interval | Durasi |
|---|---|---|
| Normal | acak 2.5–6 s | ×1 |
| Senang, excited | acak 1.5–4 s | ×0.8 |
| Marah, fokus | acak 4–9 s | ×0.8 |
| Ngantuk, capek | acak 1.5–3 s | ×2.2 |
| Bosan | acak 3–5 s | ×1.6 |

Peluang kedip ganda 15% (jeda 180 ms). Peluang kedip asimetris (satu mata telat 30 ms) 10%.

### 4.3 Lirik dan saccade

Target lirik `(lx, ly) ∈ [-1, 1]`:

```
ox = lx · 55·S,   oy = ly · 35·S
smoothing:    g += (g* - g) · (1 - e^(-dt/0.06))          cepat, seperti saccade
jitter mikro: ± 0.03 acak tiap 200–600 ms
```

Kopling lirik ke bentuk:

```
w_i *= 1 + 0.06·lx·s            mata di sisi arah lirik sedikit lebih besar
h   *= 1 - 0.10·lx²             mata menyempit di ujung lirikan
lt  += 0.12·max(0,  ly)         melirik ke bawah menutup kelopak sedikit
h   *= 1 + 0.08·max(0, -ly)     melirik ke atas melebarkan mata
```

### 4.4 Napas dan idle

```
y += 2·S · sin(2π·t/4)             napas 4 detik
w *= 1 + 0.008·sin(2π·t/4)
```

### 4.5 Rotasi lirik melingkar

Dipakai untuk mata pusing dan memutar bola mata:

```
gx = A · cos(ω·t),   gy = A · sin(ω·t)       A = 0.9,  ω = 2π·f
```

---

## 5. Bentuk khusus

| Shape | Formula |
|---|---|
| **1 Heart** | Piksel menyala jika `(X² + Y² - 1)³ - X²·Y³ ≤ 0`, dengan `X = (x - cx)/Sh`, `Y = (cy - y)/Sh + 0.1`, `Sh = 0.55·h`. Denyut: `Sh = Sh₀·(1 + 0.08·sin(6t))`. |
| **2 X (mati)** | Dua garis tebal `0.16·h` dari `(cx ± a, cy ± a)`, `a = 0.4·h`. |
| **3 Spiral (pusing)** | `r(θ) = b·θ`, `θ ∈ [0, 4π]`, `b = 0.45·h/(4π)`, tebal `0.08·h`, diputar `φ = 2π·1.2·t`. |
| **4 Star / sparkle** | Superellipse `|X|ⁿ + |Y|ⁿ ≤ 1`, `n = 0.55` (bintang 4 ujung) sampai `n = 2` (lingkaran). Denyut: `S·(1 + 0.12·sin(6t))`. |
| **5 Ring (loading)** | Busur 270°, radius `0.4·h`, tebal `0.12·h`, mulai di `θ₀ = 2π·1.2·t`, alpha turun linear sepanjang busur. |

---

## 6. Efek tambahan (fx)

| Kode | Formula |
|---|---|
| `shake(A, f)` | `ox += A·sin(2π·f·t)`. Tertawa: A=3, f=14. Marah besar: A=2, f=22. |
| `tremble` | `ox += 1.5·noise(t·20)`, `oy += 1.5·noise(t·20 + 7)`. Takut. |
| `tear` | Tetes dari `(cx ± 0.25w, yB')`: `y_tear = yB' + 140·(τ mod 1.4)`, radius `0.06w·(1 - τ/1.4)`, alpha `1 - τ/1.4`, dengan `τ = t + fase`. Dua tetes per mata dengan fase berbeda. |
| `blush` | Dua elips di bawah mata, pusat `(cx, yB' + 0.18h)`, ukuran `0.7w × 0.22h`, warna `#FF6FA0`, alpha `0.35` (fade-in 300 ms). |
| `zzz` | Tiga huruf Z: ke-i di `(x0 + w + 20 + 22i, cy - 20 - 22i - 12·(t mod 3))`, skala `1 + 0.4i`, alpha `1 - (t mod 3)/3`. |
| `sweat` | Tetes keringat di sisi luar atas mata, jatuh `50px/s`, reset tiap 1.2 s. |
| `mark` | Tanda `!` (kaget) atau `?` (bingung) di atas mata, muncul dengan spring `ζ = 0.3`, hilang setelah 1.2 s. |
| `hearts` | Hati kecil (`S = 10..20`) naik dari mata: `y = cy - 60·τ`, `x = cx + 18·sin(3τ + fase)`, alpha `1 - τ`, `τ ∈ [0, 1.5]`. |
| `pulse(a, f)` | `w, h *= 1 + a·sin(2π·f·t)`. Mode mendengarkan: a=0.04, f=2. |
| `dim(k)` | Warna dikali `k`. Mengantuk k=0.6, baterai lemah k=0.5. |
| `glitch` | Tiap 80–300 ms selama 60 ms: geser tiap baris acak `±8px` dan pisahkan kanal warna `±3px`. |
| `drift` | Lirik bergeser lambat: `gx = 0.4·sin(2π·t/7)`, `gy = 0.2·sin(2π·t/5)`. Bosan. |

---

## 7. Preset

Semua angka di kanvas referensi 640 × 400. `ρ` ditulis `TL/TR/BL/BR`, atau satu angka bila keempatnya sama.

### 7.1 Preset simetris (`L = R`)

| ID | Nama | w | h | ρ | tilt | lt | lb | cv | gap | y | shape | fx | warna |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | normal | 130 | 150 | .35 | 0 | 0 | 0 | 0 | 190 | 0 | 0 | – | `#6FE7FF` |
| 2 | happy | 130 | 120 | .45 | 0 | 0 | .55 | .30 | 190 | 8 | 0 | – | `#8CFFE0` |
| 3 | laugh | 130 | 100 | .45 | 0 | 0 | .62 | .34 | 195 | 10 | 0 | shake(3,14) | `#8CFFE0` |
| 4 | joy (^ ^) | 140 | 70 | .5 | 0 | 0 | .70 | .45 | 190 | 8 | 0 | – | `#8CFFE0` |
| 5 | love | 130 | 150 | .35 | 0 | 0 | 0 | 0 | 200 | 0 | 1 | hearts, pulse | `#FF5C9A` |
| 6 | angry | 135 | 130 | .25 | .55 | 0 | 0 | 0 | 200 | 5 | 0 | – | `#FF5A3C` |
| 7 | furious | 140 | 115 | .18/.18/.30/.30 | .80 | .10 | 0 | 0 | 185 | 5 | 0 | shake(2,22) | `#FF2E4D` |
| 8 | sad | 125 | 135 | .35 | -.55 | 0 | 0 | 0 | 190 | 8 | 0 | dim(.85) | `#5FA8FF` |
| 9 | cry | 125 | 120 | .35 | -.60 | .15 | 0 | 0 | 190 | 10 | 0 | tear, tremble | `#5FA8FF` |
| 10 | surprised | 150 | 185 | .50 | 0 | 0 | 0 | 0 | 210 | -5 | 0 | mark(!) | `#6FE7FF` |
| 11 | scared | 120 | 170 | .50 | -.25 | 0 | 0 | 0 | 230 | 0 | 0 | tremble, sweat | `#B8C8FF` |
| 12 | sleepy | 135 | 60 | .30 | .25 | .30 | 0 | 0 | 190 | 14 | 0 | dim(.6) | `#4FB4CC` |
| 13 | sleeping | 135 | 150 | .35 | 0 | .92 | 0 | -.10 | 190 | 14 | 0 | zzz, dim(.5) | `#4FB4CC` |
| 14 | suspicious | 135 | 90 | .20 | .30 | 0 | .30 | 0 | 190 | 8 | 0 | – | `#6FE7FF` |
| 15 | smug | 135 | 95 | .35/.35/.20/.20 | .20 | .25 | 0 | -.15 | 190 | 5 | 0 | – | `#6FE7FF` |
| 16 | excited | 130 | 130 | .5 | 0 | 0 | 0 | 0 | 195 | -4 | 4 (n=.55) | pulse(.06,3) | `#FFD95E` |
| 17 | dizzy | 130 | 130 | – | 0 | 0 | 0 | 0 | 190 | 0 | 3 | rotasi 4.5 | `#C08CFF` |
| 18 | dead | 130 | 130 | – | 0 | 0 | 0 | 0 | 190 | 0 | 2 | – | `#9AA5B1` |
| 19 | shy | 110 | 120 | .40 | 0 | .10 | .30 | .15 | 185 | 8 | 0 | blush, gaze(-.6,.7) | `#FFA8C8` |
| 20 | bored | 135 | 80 | .30 | 0 | .35 | 0 | 0 | 190 | 10 | 0 | drift | `#6FE7FF` |
| 21 | thinking | 130 | 140 | .35 | 0 | .10 | 0 | 0 | 190 | 0 | 0 | gaze(.7,-.8), mark(?) | `#6FE7FF` |
| 22 | dreamy | 130 | 140 | .40 | 0 | 0 | .20 | .10 | 190 | -2 | 0 | highlight ×2 (besar) | `#B8A0FF` |
| 23 | evil | 140 | 100 | .15 | .75 | .10 | .25 | 0 | 185 | 6 | 0 | pulse(.03,1) | `#FF2E4D` |
| 24 | focused | 140 | 110 | .20 | .20 | .10 | 0 | 0 | 190 | 3 | 0 | – | `#6FE7FF` |
| 25 | proud | 130 | 110 | .40 | 0 | .10 | .35 | .20 | 190 | -6 | 0 | – | `#8CFFE0` |
| 26 | sulky | 135 | 90 | .30 | .35 | .25 | 0 | -.10 | 190 | 8 | 0 | gaze(.8,.2) | `#7FA8D0` |
| 27 | tired | 140 | 70 | .30 | 0 | .35 | 0 | -.12 | 190 | 20 | 0 | dim(.7), drift | `#5FA0B8` |
| 28 | listening | 140 | 160 | .50 | 0 | 0 | 0 | 0 | 195 | -2 | 0 | pulse(.04,2) | `#6FE7FF` |
| 29 | glitch | 130 | 150 | .10 | 0 | 0 | 0 | 0 | 190 | 0 | 0 | glitch | `#6FE7FF` |
| 30 | loading | 130 | 130 | – | 0 | 0 | 0 | 0 | 190 | 0 | 5 | – | `#6FE7FF` |
| 31 | low battery | 130 | 70 | .30 | 0 | .20 | 0 | 0 | 190 | 12 | 0 | dim(.5), pulse(.05,.5) | `#FFB020` |
| 32 | dome (kartun) | 130 | 150 | .50/.50/.15/.15 | 0 | 0 | 0 | 0 | 190 | 0 | 0 | – | `#6FE7FF` |

### 7.2 Preset asimetris (mata kiri dan kanan berbeda)

Kolom yang tidak disebut memakai nilai `normal`.

| ID | Nama | Mata kiri | Mata kanan | fx |
|---|---|---|---|---|
| 40 | wink kanan | `joy` | `normal` | – |
| 41 | wink kiri | `normal` | `joy` | – |
| 42 | curious | w 120, h 140, ρ .35 | w 150, h 175, ρ .45 | gaze(.3,-.3) |
| 43 | skeptical | h 150, lt .35, tilt .30 | h 170 (terbuka lebar) | satu "alis" naik |
| 44 | confused | h 150, tilt -.30 | h 110, tilt .30 | mark(?), gaze wobble ±.3 @1 Hz |
| 45 | sneaky | lt .40, tilt .30 (mata menyipit) | normal | gaze(.9,0) |
| 46 | one-eyed-shock | h 185 (kaget) | h 90, tilt .30 (curiga) | – |
| 47 | sleepy-half | h 60, lt .30 | h 100, lt .10 | asimetris karena setengah bangun |
| 48 | flirty | `joy` (kedip lambat) | `love` | hearts |
| 49 | dizzy-half | spiral | `normal` (juling) | gaze berputar |

### 7.3 Warna tema per mood

Transisi warna memakai spring RGB yang sama dengan parameter lain. Kecerahan (`brightness`) bisa diatur global 30–100%.

| Mood | Warna | Catatan |
|---|---|---|
| Netral | `#6FE7FF` | Cyan khas Emo |
| Senang | `#8CFFE0` | Cyan kehijauan |
| Cinta | `#FF5C9A` | Merah muda |
| Marah | `#FF5A3C` / `#FF2E4D` | Oranye-merah |
| Sedih | `#5FA8FF` | Biru dingin |
| Takut | `#B8C8FF` | Pucat |
| Excited | `#FFD95E` | Kuning hangat |
| Pusing | `#C08CFF` | Ungu |
| Ngantuk | `#4FB4CC` × 0.6 | Redup |
| Baterai lemah | `#FFB020` | Amber |

### 7.4 Timing spring per preset

| Preset | ζ | k | Durasi transisi kira-kira |
|---|---|---|---|
| normal | 0.7 | 220 | 350 ms |
| happy, laugh, joy | 0.45 | 260 | 300 ms |
| excited | 0.30 | 300 | 250 ms |
| surprised | 0.30 | 400 | 150 ms |
| scared | 0.35 | 350 | 200 ms |
| angry, furious, evil | 0.80 | 320 | 200 ms |
| sad, cry, sulky | 1.00 | 140 | 600 ms |
| sleepy, sleeping, tired | 1.30 | 90 | 900 ms |
| suspicious, smug | 0.90 | 200 | 400 ms |
| shy | 0.60 | 180 | 450 ms |
| love | 0.40 | 240 | 400 ms |

---

## 8. Sekuens animasi (keyframe)

Format: `waktu(ms) → target`. Antar keyframe memakai spring dengan `ζ` yang disebut. Semua sekuens berakhir kembali ke `normal` kecuali disebut lain.

| Sekuens | Timeline |
|---|---|
| **wake up** | 0 → `sleeping`; 400 → lt .70 (ζ 1.3); 900 → lt .40, h ×1.1 (menggeliat); 1300 → kedip pelan (durasi ×2.5); 1900 → lt .10; 2200 → kedip ×2 cepat; 2600 → `normal` |
| **go to sleep** | 0 → `sleepy`; 1500 → kedip pelan; 2500 → lt .6; 3500 → kedip pelan; 4500 → `sleeping` |
| **greeting (hai)** | 0 → `happy` (ζ .3); 150 → y -10; 300 → y +4; 450 → y -6; 600 → y 0; 800 → wink kanan; 1200 → `normal` |
| **laugh** | 0 → `laugh`; 0..1200 → `shake(3,14)`, y ± 6 pada 6 Hz; 1200 → `happy`; 1800 → `normal` |
| **startle (kaget)** | 0 → h 200, gap +20, ζ .3 (60 ms); 200 → `surprised`; 900 → kedip cepat; 1200 → `suspicious` (lirik ke arah suara) |
| **sneeze** | 0 → lt .6, h ×0.8 (400 ms, tarik napas); 400 → w ×1.25, h ×0.4 (tutup, 120 ms); 520 → h ×1.15 (overshoot); 800 → `normal` |
| **eye roll** | 0 → lt .30; 0..600 → gaze melingkar satu putaran (bab 4.5, A = 1, `ω = 2π/0.6`); 700 → `bored` |
| **nod (ya)** | oy = `12·S·sin(2π·2·t)` selama 800 ms, `ζ = 0.6` |
| **shake (tidak)** | ox = `20·S·sin(2π·2.5·t)` selama 800 ms; tilt +0.2 |
| **dizzy** | 0..2500 → gaze melingkar (A = .9, f = 1.5); 2500 → `dizzy` (spiral) 1.5 s; 4000 → kedip ×3; 4600 → `normal` |
| **cry** | 0 → `sad`; 600 → `cry`; 600..4000 → tear loop; 4000 → kedip pelan ×2; 5000 → `sad`; 6500 → `normal` |
| **love burst** | 0 → `surprised`; 250 → `love`; 250..3000 → hearts + pulse; 3000 → `happy`; 4000 → `normal` |
| **poke (disentuh)** | 0 → `surprised` (200 ms); 300 → `shy`; 1500 → `happy`; 2500 → `normal` |
| **look around** | 0 → lx -1; 700 → lx 0.9 (ζ 1); 1500 → ly -0.7; 2100 → kembali (0, 0) dan kedip |
| **sulk (ngambek)** | 0 → `angry`; 400 → `sulky`; 1500 → kedip lambat; 3000 → gaze berpaling, lt .30 |
| **shy peek** | 0 → `shy` dengan gaze menjauh; 800 → gaze balik ke depan sebentar (200 ms); 1000 → menjauh lagi |
| **boot** | 0 → layar hitam; 200 → garis horizontal tipis (h 6, w 260); 500 → mekar jadi `normal` (ζ .4); 900 → kedip |
| **error** | 0 → `glitch`; 400 → `dizzy`; 1800 → `dead` sebentar (400 ms); 2200 → `normal` |

---

## 9. Mood engine (valence × arousal)

Dua sumbu, `v` (valence, buruk sampai baik) dan `a` (arousal, tenang sampai bergairah), keduanya dalam `[-1, 1]`.

### 9.1 Dinamika

```
tiap frame:   v += (v0 - v)·dt/τ,   a += (a0 - a)·dt/τ
              v0 = 0.2 (sedikit ceria),  a0 = 0,  τ = 20 s
stimulus:     v += Δv,   a += Δa       (bab 10), lalu klem ke [-1, 1]
```

### 9.2 Prototipe dalam bidang (v, a)

| Prototipe | v | a |
|---|---|---|
| normal | 0.1 | 0.0 |
| happy | 0.8 | 0.4 |
| excited | 0.7 | 0.9 |
| angry | -0.7 | 0.8 |
| scared | -0.5 | 0.9 |
| sad | -0.7 | -0.4 |
| sleepy | 0.0 | -0.8 |
| bored | -0.2 | -0.5 |
| smug | 0.4 | 0.1 |
| suspicious | -0.3 | 0.3 |

### 9.3 Pencampuran

Bobot tiap prototipe `i`:

```
w_i = exp( -((v - v_i)² + (a - a_i)²) / (2·σ²) ),    σ = 0.45
P   = Σ w_i·P_i / Σ w_i           (untuk semua parameter numerik)
shape, fx = milik prototipe dengan w_i terbesar
```

Karena parameter linear, dua mood yang berdekatan otomatis menghasilkan ekspresi campuran yang halus (mis. sedikit senang + sedikit ngantuk).

### 9.4 Kepribadian

Sepuluh persen variasi acak per perangkat, ditetapkan sekali saat boot dan disimpan:

| Sifat | Pengaruh |
|---|---|
| `curiosity` (0.5–1.5) | Frekuensi lirik dikali ini. |
| `energy` (0.7–1.3) | `k` spring dikali ini, interval idle dibagi ini. |
| `sensitivity` (0.5–1.5) | `Δv`, `Δa` dari stimulus dikali ini. |
| `shyness` (0–1) | Peluang `shy peek` muncul saat ada orang mendekat. |

---

## 10. Stimulus dan perilaku

### 10.1 Peta stimulus ke respons

| Stimulus | Δv | Δa | Respons |
|---|---|---|---|
| Sentuh sekali | +0.1 | +0.2 | `poke` |
| Sentuh 3× cepat | -0.3 | +0.4 | `sulk` |
| Sentuh tahan > 1.5 s (elus) | +0.5 | -0.2 | `love burst` |
| Suara keras mendadak | -0.3 | +0.7 | `startle`, lalu lirik ke arah suara |
| Tepuk dua kali / wake word | +0.2 | +0.4 | `greeting`, lalu `listening` |
| Diangkat (IMU naik) | +0.3 | +0.6 | `excited` |
| Diguncang | -0.2 | +0.8 | `dizzy` |
| Jatuh bebas (IMU ≈ 0 g) | -0.6 | +1.0 | `scared` (startle) |
| Terbalik > 2 s | -0.3 | +0.3 | `confused` |
| Objek dekat (< 20 cm) | +0.1 | +0.3 | `curious`, mata mengikuti objek |
| Cahaya turun mendadak | 0 | -0.4 | `sleepy` |
| Cahaya naik | +0.1 | +0.3 | `wake up` |
| Baterai < 20% | -0.2 | -0.3 | `low battery` bergantian dengan `tired` |
| Baterai < 5% | -0.4 | -0.6 | `go to sleep` |
| Terlalu lama dipegang tanpa gerak | 0 | -0.2 | `bored` |
| Wi-Fi / sensor error | -0.3 | +0.3 | `error` |

Beri cooldown 3 detik untuk respons yang sama agar tidak berulang terus.

### 10.2 Idle timeline

| Waktu tanpa interaksi | Perilaku |
|---|---|
| 0–10 s | `normal`, kedip dan lirik acak (bab 4.2, 4.3). |
| 10–20 s | `look around` (peluang 30% per 5 s). |
| 20 s | Mood turun ke `bored` (`a -= 0.3`). |
| 45 s | `sleepy` (`a -= 0.5`), menguap: lt naik lalu turun pelan 1.5 s. |
| 90 s | `go to sleep`, lalu `sleeping`. |
| Stimulus apa pun | `wake up`, lalu respons stimulus itu. |

### 10.3 Idle acak

Tiap 8–20 detik pilih satu perilaku dengan bobot: kedip ganda (30%), `look around` (30%), wink (10%), `smug` sesaat (10%), `thinking` sesaat (10%), `nod` (10%). Batalkan jika sedang ada sekuens lain.

---

## 11. Catatan implementasi ESP32

**Render**

- Gunakan sprite RGB565 (TFT_eSPI atau LovyanGFX) dan kirim hanya dirty rect: gabungan bounding box mata pada frame ini dan frame sebelumnya. Target 40–60 fps.
- Layar 240 × 320: `S = 0.5` (potret) atau `S = 0.5` dengan `gap` diperkecil (lanskap). Layar OLED 128 × 64 monokrom: `S = 0.2`, buang glow dan gradasi, pakai dithering untuk anti-aliasing.
- Sprite 240 × 160 RGB565 memakai sekitar 77 KB. Aman untuk ESP32 biasa (PSRAM tidak wajib).

**Kinerja**

- Pra-hitung tabel `sqrt(r² - dx²)` untuk radius yang dipakai, atau hitung sekali per frame karena hanya ada 4 sudut per mata.
- Spring memakai `float`. ESP32 punya FPU, jadi aman. Total sekitar 20 parameter × 2 mata, sangat ringan.
- Jalankan engine di core 1, dan I/O sensor dan jaringan di core 0.

**Stabilitas**

- Selalu pakai `dt` nyata dan klem `dt ≤ 0.05` agar spring tidak meledak saat frame lambat.
- Jika `|p - p*| < 0.001` dan `|v| < 0.001`, langsung set `p = p*` dan `v = 0` supaya tidak ada getaran sisa.
- Untuk ekspresi yang berpindah shape, jangan interpolasi antar shape secara langsung. Pakai urutan kecil, ganti, besar (bab 4.1).

**Urutan update per frame**

```
1. baca sensor, tambahkan stimulus ke mood (bab 9, 10)
2. tentukan preset target (blend, atau sekuens yang sedang aktif)
3. update target lirik, kedip, napas (bab 4)
4. update spring semua parameter
5. hitung EyeP akhir per mata (kopling lirik, kedip, fx)
6. rasterisasi kolom (bab 2) dan efek (bab 3, 6) ke sprite
7. push dirty rect ke layar
```

---

## 12. Kerangka fungsi inti

Hanya inti formula bab 2 dan 4.1, untuk satu mata:

```cpp
// spring
inline void spring(float &p, float &v, float tgt, float k, float z, float dt) {
  float c = 2.f * z * sqrtf(k);
  v += (k * (tgt - p) - c * v) * dt;
  p += v * dt;
}

// satu kolom mata: hitung yT', yB'
bool column(const EyeP &e, float cx, float cy, float B, int s, float x,
            float &yT, float &yB) {
  float w = e.w * (1.f + 0.25f * B), h = fmaxf(4.f, e.h * (1.f - B));
  float x0 = cx - w / 2, y0 = cy - h / 2;
  float m  = 0.5f * fminf(w, h);
  float rTL = e.rTL*m, rTR = e.rTR*m, rBL = e.rBL*m, rBR = e.rBR*m;

  yT = y0; yB = y0 + h;
  if (x < x0 + rTL)        { float d = x0 + rTL - x;       yT = y0 + rTL - sqrtf(rTL*rTL - d*d); }
  else if (x > x0+w-rTR)   { float d = x - (x0 + w - rTR); yT = y0 + rTR - sqrtf(rTR*rTR - d*d); }
  if (x < x0 + rBL)        { float d = x0 + rBL - x;       yB = y0 + h - rBL + sqrtf(rBL*rBL - d*d); }
  else if (x > x0+w-rBR)   { float d = x - (x0 + w - rBR); yB = y0 + h - rBR + sqrtf(rBR*rBR - d*d); }

  float u   = (x - cx) / (w / 2);
  float uin = -s * u;
  float lidTop = y0 + h * fmaxf(0.f, e.lt + 0.5f*e.tilt*uin + 0.5f*fabsf(e.tilt));
  float lidBot = y0 + h * (1.f - e.lb);
  float sag = e.cv * h * u * u;

  yT = fmaxf(yT, lidTop) + sag;
  yB = fminf(yB, lidBot) + sag;
  return yB > yT;
}
```

---

## 13. Ringkasan cara kerja

1. Mood `(v, a)` dan stimulus menentukan **preset target**, yaitu satu vektor `EyeP` per mata.
2. **Spring** menggerakkan parameter saat ini menuju target, dengan karakter membal atau berat sesuai emosi.
3. **Kedip, lirik, napas** ditumpuk di atas parameter sebagai modulasi, jadi mata selalu terlihat hidup walau diam.
4. **Rasterisasi kolom** mengubah parameter menjadi bentuk, lalu lapisan glow, highlight, dan fx memberi tampilan khas Emo.
5. **Sekuens keyframe** dipakai untuk gerakan yang harus terjadi berurutan (bangun tidur, bersin, menangis).
