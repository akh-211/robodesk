#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "RoboArbiter.h"

// EMO-style corner reactions (hearts, sparkles, notes, Zzz...) drawn over the eyes. Frames are 32x32 1-bit sprites generated
// once at boot into a caller-provided PSRAM block, so the flash image stays unchanged. Playback is leased through RoboArbiter.
namespace robosprite {
constexpr uint8_t Size = 32, Frames = 12, Stride = Size / 8, ClipCount = 8;
constexpr uint16_t FrameBytes = Stride * Size;                              // 128
constexpr uint32_t StoreBytes = uint32_t(ClipCount) * Frames * FrameBytes;  // 12288
constexpr uint16_t FrameMs = 100, HoldMs = 3000, MinGapMs = 5000;
enum class Clip : uint8_t { Heart, Sparkle, Note, Zzz, Drop, Question, Anger, Dizzy, None };

namespace detail {
struct Canvas {
  uint8_t* b;
  void set(int x, int y) { if (unsigned(x) < Size && unsigned(y) < Size) b[y * Stride + (x >> 3)] |= uint8_t(0x80u >> (x & 7)); }
  void hline(int x0, int x1, int y) { for (int x = x0; x <= x1; ++x) set(x, y); }
  void line(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { set(x0, y0); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
  }
  void disc(int cx, int cy, int r) { for (int y = -r; y <= r; ++y) for (int x = -r; x <= r; ++x) if (x * x + y * y <= r * r) set(cx + x, cy + y); }
  void thick(int x, int y) { set(x, y); set(x + 1, y); set(x, y + 1); }
};
inline float absf(float v) { return v < 0 ? -v : v; }
inline void sparkle(Canvas& c, int cx, int cy, float radius) {
  if (radius < 1.f) return;
  const int R = int(radius) + 1;
  for (int y = -R; y <= R; ++y) for (int x = -R; x <= R; ++x) if (sqrtf(absf(float(x))) + sqrtf(absf(float(y))) <= sqrtf(radius)) c.set(cx + x, cy + y);
}
inline void zed(Canvas& c, int x, int y, int s) { c.hline(x, x + s, y); c.line(x + s, y, x, y + s); c.hline(x, x + s, y + s); }

inline void draw(Clip clip, uint8_t f, uint8_t* out) {
  memset(out, 0, FrameBytes);
  Canvas c{out};
  const float t = float(f) / Frames, p = 6.2831853f * t;
  switch (clip) {
    case Clip::Heart: {
      const float s = 0.8f + 0.2f * sinf(p);
      for (int y = 0; y < Size; ++y) for (int x = 0; x < Size; ++x) {
        const float u = (float(x) - 15.5f) / (11.f * s), v = -(float(y) - 16.f) / (11.f * s) + 0.15f, a = u * u + v * v - 1.f;
        if (a * a * a - u * u * v * v * v <= 0.f) c.set(x, y);
      }
      break;
    }
    case Clip::Sparkle: {
      sparkle(c, 15, 16, 2.f + 12.f * sinf(3.1415927f * t));
      float t2 = t + 0.5f; if (t2 >= 1.f) t2 -= 1.f;
      sparkle(c, 6, 7, 1.f + 5.f * sinf(3.1415927f * t2));
      break;
    }
    case Clip::Note: {
      const int cy = 25 - int(4.f * absf(sinf(p / 2.f)));
      c.disc(11, cy, 4); c.line(14, cy, 14, cy - 16); c.line(15, cy, 15, cy - 16); c.line(15, cy - 16, 22, cy - 10); c.line(15, cy - 15, 22, cy - 9);
      break;
    }
    case Clip::Zzz: {
      for (int k = 0; k < 3; ++k) {
        float q = t + float(k) / 3.f; if (q >= 1.f) q -= 1.f;
        if (q < 0.9f) zed(c, 3 + int(q * 16.f), 26 - int(q * 22.f), 4 + int(q * 7.f));
      }
      break;
    }
    case Clip::Drop: {
      if (t > 0.85f) break;
      const int y = 3 + int(t * 26.f);
      for (int dy = 0; dy <= 7; ++dy) c.hline(16 - dy / 2, 16 + dy / 2, y + dy);
      c.disc(16, y + 10, 4);
      break;
    }
    case Clip::Question: {
      const int cx = 16 + int(lroundf(2.f * sinf(p)));
      for (int a = 200; a >= -70; a -= 8) { const float r = float(a) * 0.017453293f; c.thick(cx + int(lroundf(7.f * cosf(r))), 10 - int(lroundf(7.f * sinf(r)))); }
      c.line(cx + 2, 16, cx, 20); c.line(cx + 3, 16, cx + 1, 20); c.disc(cx, 26, 2);
      break;
    }
    case Clip::Anger: {
      const int o = 1 + int(lroundf(2.f + 2.f * sinf(p)));
      for (int sy = -1; sy <= 1; sy += 2) for (int sx = -1; sx <= 1; sx += 2) {
        const int cx = 16 + sx * (3 + o), cy = 16 + sy * (3 + o);
        c.line(cx, cy, cx + sx * 7, cy); c.line(cx, cy, cx, cy + sy * 7);
      }
      break;
    }
    case Clip::Dizzy: {
      for (int i = 0; i < 44; ++i) { const float a = float(i) * 0.5f + p, r = float(i) * 0.33f; c.thick(16 + int(lroundf(r * cosf(a))), 16 + int(lroundf(r * sinf(a)))); }
      break;
    }
    case Clip::None: break;
  }
}
}  // namespace detail

class Library {
 public:
  bool build(uint8_t* store) {
    if (!store) return false;
    store_ = store;
    for (uint8_t c = 0; c < ClipCount; ++c) for (uint8_t f = 0; f < Frames; ++f) detail::draw(Clip(c), f, store_ + (uint32_t(c) * Frames + f) * FrameBytes);
    return true;
  }
  bool ready() const { return store_ != nullptr; }
  const uint8_t* frame(Clip c, uint8_t f) const { return store_ && uint8_t(c) < ClipCount && f < Frames ? store_ + (uint32_t(uint8_t(c)) * Frames + f) * FrameBytes : nullptr; }

 private:
  uint8_t* store_ = nullptr;
};

// 1 px black halo around lit pixels (8-neighborhood). Drawn under the sprite so it stays readable over the eyes without a solid box.
inline void haloMask(const uint8_t* in, uint8_t* out) {
  memset(out, 0, FrameBytes);
  for (int y = 0; y < Size; ++y) for (int x = 0; x < Size; ++x) {
    if (!(in[y * Stride + (x >> 3)] & (0x80u >> (x & 7)))) continue;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
      const int px = x + dx, py = y + dy;
      if (unsigned(px) < Size && unsigned(py) < Size) out[py * Stride + (px >> 3)] |= uint8_t(0x80u >> (px & 7));
    }
  }
}

// Emotion name (RoboEmotionMap.h) -> corner clip; None = this emotion has no corner reaction.
inline Clip clipFor(const char* e) {
  if (!e) return Clip::None;
  struct M { const char* n; Clip c; };
  static const M map[] = {{"love", Clip::Heart}, {"affectionate", Clip::Heart}, {"happy", Clip::Sparkle}, {"excited", Clip::Sparkle}, {"proud", Clip::Sparkle},
                          {"laugh", Clip::Note}, {"agree", Clip::Note}, {"sleepy", Clip::Zzz}, {"sad", Clip::Drop}, {"anxious", Clip::Drop},
                          {"lonely", Clip::Drop}, {"curious", Clip::Question}, {"thinking", Clip::Question}, {"annoyed", Clip::Anger}, {"dizzy", Clip::Dizzy}};
  for (const M& m : map) if (!strcmp(e, m.n)) return m.c;
  return Clip::None;
}

// One corner slot. Starts only if RoboArbiter admits the incoming tier; low tiers are rate-limited so beats never pile up.
class Player {
 public:
  bool start(Clip c, roboarb::Tier tier, uint32_t now) {
    if (c == Clip::None) return false;
    if (!roboarb::admits(roboarb::decide(tier, tracker_.running(now)))) return false;
    if (tier >= roboarb::Tier::Routine && started_ && uint32_t(now - startedAt_) < MinGapMs) return false;
    clip_ = c; startedAt_ = now; started_ = true; tracker_.start(tier, now, HoldMs);
    return true;
  }
  bool active(uint32_t now) { return tracker_.running(now).active; }
  Clip clip() const { return clip_; }
  uint8_t frameAt(uint32_t now) const { return uint8_t((uint32_t(now - startedAt_) / FrameMs) % Frames); }
  void stop() { tracker_.clear(); }

 private:
  roboarb::Tracker tracker_;
  Clip clip_ = Clip::None;
  uint32_t startedAt_ = 0;
  bool started_ = false;
};
}  // namespace robosprite
