#pragma once
// Hand styles and held props for the OLED performer (RoboActor.h). Everything is DATA TABLES + tiny draw functions:
//   * new prop        = 1 draw function + 1 row in props()            (see docs/companion/ADD_PROP.md)
//   * new hand style  = 1 draw function + 1 row in handStyles()
// Draw functions only use Brush (circle/cap/line/px) in a hand-local frame: f.at(u, v) = u to the hand's right, v "up" along the
// hand direction. They are called twice per frame: halo pass (black, grown by p.gr) then white pass (p.c == 1, detail allowed).
// No frames are stored; hands float (no forearm). Shapes mirror docs/companion/PD-5.2_tangan_mic.html so a design made there
// can be ported almost 1:1.
#include <initializer_list>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace roboprops {
struct Pt { float x, y; };

// Minimal pixel sink: works with any GFX-like object that has drawPixel/drawLine (see canvasOf).
struct Canvas {
  void* ctx;
  void (*px)(void*, int, int, uint16_t);
  void (*line)(void*, int, int, int, int, uint16_t);
};
template <class G> inline Canvas canvasOf(G& g) {
  return Canvas{&g, [](void* c, int x, int y, uint16_t v) { static_cast<G*>(c)->drawPixel(x, y, v); },
                [](void* c, int x0, int y0, int x1, int y1, uint16_t v) { static_cast<G*>(c)->drawLine(x0, y0, x1, y1, v); }};
}

inline int rnd(float v) { return int(floorf(v + .5f)); }

// Brush draws in screen pixels; radii are scaled by K (the "bigger hands" factor).
struct Brush {
  Canvas cv; float K = 1.35f;
  void px(Pt p, uint16_t c) const { cv.px(cv.ctx, rnd(p.x), rnd(p.y), c); }
  void line(Pt a, Pt b, uint16_t c) const { cv.line(cv.ctx, rnd(a.x), rnd(a.y), rnd(b.x), rnd(b.y), c); }
  void circle(Pt p, float r, uint16_t c) const {
    r *= K; const int R = int(ceilf(r)) + 1; const float lim = r * r + r * .7f;
    for (int j = -R; j <= R; ++j) for (int i = -R; i <= R; ++i) if (float(i * i + j * j) <= lim) cv.px(cv.ctx, rnd(p.x + i), rnd(p.y + j), c);
  }
  void cap(Pt a, Pt b, float r, uint16_t c) const {
    const float dx = b.x - a.x, dy = b.y - a.y; const float m = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    const int n = m < 1.f ? 1 : int(m);
    for (int i = 0; i <= n; ++i) circle(Pt{a.x + dx * i / n, a.y + dy * i / n}, r, c);
  }
};

// Hand-local frame: origin = grip point, v = along the hand's "up" (sn, -cs), u = to the right (cs, sn).
struct Frame {
  float x, y, sn, cs, K;
  Pt at(float u, float v) const { return Pt{x + K * (u * cs + v * sn), y + K * (u * sn - v * cs)}; }
  Frame moved(float u, float v) const { const Pt p = at(u, v); return Frame{p.x, p.y, sn, cs, K}; }
};
struct Pass { uint16_t c; float gr; };   // halo pass: {0, 1.6}; white pass: {1, 0}
struct PropCtx { float t, len; int side; };

typedef void (*HandFn)(const Brush&, const Frame&, const Pass&, float open, int side, bool hold);
typedef void (*PropFn)(const Brush&, const Frame&, const Pass&, const PropCtx&);
struct HandStyle { const char* id; const char* name; HandFn draw; };
enum class Group : uint8_t { Mic, Tool, TwoHand };
struct PropDef {
  const char* id; const char* name; Group group;
  uint8_t hands;                      // 1 = one hand (a second hand may grip lower on the shaft), 2 = needs both hands
  float lenMin, lenDef, lenMax;       // shaft / string length multiplier the performer may animate between
  PropFn draw;
};

// ---------------------------------------------------------------- hand styles
inline void handBulat(const Brush& b, const Frame& f, const Pass& p, float op, int s, bool hold) {
  const float th = s * 1.3f;
  b.circle(f.at(0, 0), 5 + p.gr, p.c);
  b.circle(f.at(5.5f * sinf(th), 5.5f * cosf(th)), 2 + p.gr, p.c);
  if (hold) return;
  if (op > .25f) {
    const float d = 1.6f * (5 + 2 * op);
    for (int k = -1; k <= 1; ++k) { const float a = k * .55f; b.cap(f.at(3.2f * sinf(a), 3.2f * cosf(a)), f.at(d * sinf(a), d * cosf(a)), 1 + p.gr * .7f, p.c); }
  } else for (int k = -1; k <= 1; ++k) b.circle(f.at(k * 2.4f, 4.6f), 1.3f + p.gr * .7f, p.c);
}
inline void handMitten(const Brush& b, const Frame& f, const Pass& p, float op, int s, bool hold) {
  b.cap(f.at(0, 0), f.at(0, hold ? 1.f : 5 + 2 * op), 4.2f + p.gr, p.c);
  b.cap(f.at(s * 3.5f, 1), f.at(s * (6 + 2 * op), 3 + 3 * op), 1.6f + p.gr, p.c);
  if (p.c == 1 && !hold && op < .3f) { b.px(f.at(-1.5f, 5), 0); b.px(f.at(1.5f, 5), 0); }
}
inline const HandStyle* handStyles(unsigned& n) {
  static const HandStyle t[] = {{"bulat", "Bulat", handBulat}, {"mitten", "Mitten", handMitten}};
  n = sizeof t / sizeof t[0]; return t;
}

// ---------------------------------------------------------------- props (hand at f.at(0,0))
inline void micBola(const Brush& b, const Frame& f, const Pass& p, const PropCtx&) {
  b.cap(f.at(0, 0), f.at(0, 7), 1.2f + p.gr, p.c); b.circle(f.at(0, 11), 4.2f + p.gr, p.c);
  if (p.c == 1) { b.line(f.at(-3, 11), f.at(3, 11), 0); b.line(f.at(0, 8), f.at(0, 14), 0); }
}
inline void micRetro(const Brush& b, const Frame& f, const Pass& p, const PropCtx&) {
  b.cap(f.at(0, 0), f.at(0, 7), 1.1f + p.gr, p.c); b.cap(f.at(0, 9), f.at(0, 14), 3.3f + p.gr, p.c);
  if (p.c == 1) { for (int v = 9; v <= 14; v += 2) b.line(f.at(-2, float(v)), f.at(2, float(v)), 0); b.line(f.at(-3.4f, 7.4f), f.at(3.4f, 7.4f), 1); }
}
inline void micRamping(const Brush& b, const Frame& f, const Pass& p, const PropCtx&) {
  b.cap(f.at(0, 0), f.at(0, 10), 1.5f + p.gr, p.c); b.circle(f.at(0, 12.5f), 2.8f + p.gr, p.c);
  if (p.c == 1) { b.px(f.at(0, 12.5f), 0); b.px(f.at(0, 6), 0); }
}
inline void micKabel(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  b.cap(f.at(0, 0), f.at(0, 7), 1.2f + p.gr, p.c); b.circle(f.at(0, 11), 4.2f + p.gr, p.c);
  const Pt p0 = f.at(0, -2), p1 = f.at(-14, -12), p2{f.x + (x.side > 0 ? 16.f : -16.f), 72.f};
  for (int i = 0; i <= 24; ++i) {
    const float k = i / 24.f, a = (1 - k) * (1 - k), m = 2 * (1 - k) * k, e = k * k;
    const Pt q{a * p0.x + m * p1.x + e * p2.x, a * p0.y + m * p1.y + e * p2.y};
    if (p.c == 1) b.px(q, 1); else b.circle(q, p.gr * .5f, p.c);
  }
  if (p.c == 1) { b.line(f.at(-3, 11), f.at(3, 11), 0); b.line(f.at(0, 8), f.at(0, 14), 0); }
}

// ---- tools (len scales the shaft / string; the performer animates it, e.g. an umbrella rising above the eyes)
inline void payung(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  const float S = 12 * x.len; b.cap(f.at(0, 0), f.at(0, S), .8f + p.gr, p.c);
  for (float a = -1.5f; a <= 1.5f; a += .1f) b.cap(f.at(0, S), f.at(sinf(a) * 11, S + cosf(a) * 5.5f), .9f + p.gr, p.c);
  if (p.c == 1) for (float a : {-.9f, 0.f, .9f}) b.line(f.at(0, S), f.at(sinf(a) * 10.5f, S + cosf(a) * 5.2f), 0);
}
inline void balon(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  const float sx = sinf(x.t * 3) * 1.5f, Sl = 6 * x.len;
  for (int i = 0; i < 10; ++i) { const float k = i / 10.f, k2 = (i + 1) / 10.f; b.cap(f.at(sx * k * k, Sl * k), f.at(sx * k2 * k2, Sl * k2), .4f + p.gr, p.c); }
  b.circle(f.at(sx, Sl + 6), 5.2f + p.gr, p.c); b.cap(f.at(sx - 1.5f, Sl + .5f), f.at(sx + 1.5f, Sl + .5f), .8f + p.gr, p.c);
  if (p.c == 1) { b.px(f.at(sx - 2, Sl + 8), 0); b.px(f.at(sx - 2.5f, Sl + 7), 0); b.px(f.at(sx - 1.5f, Sl + 9), 0); }
}
inline void lightstick(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  const float T = 13 * x.len; b.cap(f.at(0, 0), f.at(0, T), 2.2f + p.gr, p.c); b.circle(f.at(0, T + 2), 2.5f + p.gr, p.c);
  if (p.c == 1) {
    for (float v = 3; v < T - 1; v += 3) b.px(f.at(0, v), 0);
    const float r = 4 + float(int(x.t * 8) % 3) * 1.5f; const Pt c = f.at(0, T + 2);
    for (int k = 0; k < 8; ++k) { const float a = k * 3.14159265f / 4; b.line(Pt{c.x + cosf(a) * r, c.y + sinf(a) * r}, Pt{c.x + cosf(a) * (r + 2), c.y + sinf(a) * (r + 2)}, uint16_t(((k + int(x.t * 6)) % 2) ? 1 : 0)); }
  }
}
inline void wand(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  const float S = 11 * x.len; b.cap(f.at(0, 0), f.at(0, S), .8f + p.gr, p.c);
  const Pt c = f.at(0, S + 4); b.circle(c, 2.2f + p.gr, p.c);
  static const float sp[8][3] = {{0, -1, 4.5f}, {0, 1, 4.5f}, {-1, 0, 4.5f}, {1, 0, 4.5f}, {-.7f, -.7f, 3}, {.7f, -.7f, 3}, {-.7f, .7f, 3}, {.7f, .7f, 3}};
  for (const auto& s : sp) b.cap(c, Pt{c.x + s[0] * s[2], c.y + s[1] * s[2]}, .5f + p.gr * .7f, p.c);
  if (p.c == 1 && int(x.t * 4) % 2) b.px(Pt{c.x + 8, c.y - 5}, 1);
}
inline void megafon(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  b.cap(f.at(0, 0), f.at(0, 3), 1.5f + p.gr, p.c);
  for (int k = 0; k <= 10; ++k) { const float v = 3.f + k, h = 2 + k * .45f; b.cap(f.at(-h, v), f.at(h, v), .9f + p.gr, p.c); }
  if (p.c == 1) {
    b.line(f.at(-6.5f, 13), f.at(6.5f, 13), 0);
    const float ph = fmodf(x.t * 3, 1.f); const Pt c = f.at(0, 15);
    for (int i = 0; i < 2; ++i) { const float r = 3 + fmodf(i + ph, 2.f) * 4; for (float a = -.9f; a <= .9f; a += .15f) b.px(Pt{c.x + sinf(a) * r * 1.2f, c.y - cosf(a) * r * .8f}, 1); }
  }
}
inline void bendera(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  const float T = 18 * x.len; b.cap(f.at(0, 0), f.at(0, T), .7f + p.gr, p.c);
  for (int u = 0; u <= 11; ++u) { const float w = sinf(x.t * 7 + u * .6f) * 1.4f * (u / 11.f); b.cap(f.at(u + 1.f, T - 7 + w), f.at(u + 1.f, T + w), .6f + p.gr, p.c); }
  if (p.c == 1) for (int u = 2; u <= 10; ++u) { const float w = sinf(x.t * 7 + u * .6f) * 1.4f * (u / 11.f); b.px(f.at(u + 1.f, T - 3.5f + w), 0); }
}
inline void kipas(const Brush& b, const Frame& f, const Pass& p, const PropCtx&) {
  b.cap(f.at(0, 0), f.at(0, 4), 1.2f + p.gr, p.c);
  for (float a = -1.05f; a <= 1.05f; a += .07f) b.cap(f.at(0, 4), f.at(sinf(a) * 13, 4 + cosf(a) * 13), .8f + p.gr, p.c);
  if (p.c == 1) for (float a = -.9f; a <= .91f; a += .3f) b.line(f.at(0, 5), f.at(sinf(a) * 12, 4 + cosf(a) * 12), 0);
}
inline void papan(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  const float S = 9 * x.len; b.cap(f.at(0, 0), f.at(0, S), .8f + p.gr, p.c);
  for (float v = S; v <= S + 10; v += 1) b.cap(f.at(-7, v), f.at(7, v), .6f + p.gr, p.c);
  if (p.c == 1) { static const float h[10][2] = {{-2, 7}, {2, 7}, {-3, 6}, {3, 6}, {-2, 5}, {0, 5}, {2, 5}, {-1, 4}, {1, 4}, {0, 3}}; for (const auto& q : h) b.px(f.at(q[0], S + q[1]), 0); }
}
inline void pompom(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  b.cap(f.at(0, 0), f.at(0, 4), 1.2f + p.gr, p.c); b.circle(f.at(0, 10), 4.4f + p.gr, p.c);
  for (int k = 0; k < 12; ++k) { const float a = k * .52f + x.t * 2; b.cap(f.at(sinf(a) * 4.5f, 10 + cosf(a) * 4.5f), f.at(sinf(a) * 7.2f, 10 + cosf(a) * 7.2f), .5f + p.gr * .6f, p.c); }
  if (p.c == 1) { b.px(f.at(-1.5f, 10), 0); b.px(f.at(1.5f, 11.5f), 0); b.px(f.at(0, 8.5f), 0); }
}
inline void buket(const Brush& b, const Frame& f, const Pass& p, const PropCtx&) {
  b.cap(f.at(0, 0), f.at(0, 7), 1.6f + p.gr, p.c); b.cap(f.at(0, 7), f.at(-5, 9.5f), .6f + p.gr, p.c); b.cap(f.at(0, 7), f.at(5, 9.5f), .6f + p.gr, p.c);
  static const float fl[3][2] = {{-3.4f, 11}, {0, 13.6f}, {3.4f, 11}};
  for (const auto& q : fl) b.circle(f.at(q[0], q[1]), 2.5f + p.gr, p.c);
  if (p.c == 1) for (const auto& q : fl) b.px(f.at(q[0], q[1]), 0);
}
inline void eskrim(const Brush& b, const Frame& f, const Pass& p, const PropCtx&) {
  for (int k = 0; k <= 9; ++k) { const float h = .4f + k * .38f; b.cap(f.at(-h, float(k)), f.at(h, float(k)), .7f + p.gr, p.c); }
  b.circle(f.at(0, 12), 4.4f + p.gr, p.c); b.circle(f.at(1.2f, 16.2f), 1.4f + p.gr, p.c);
  if (p.c == 1) { for (int v = 2; v <= 8; v += 2) { b.px(f.at(-.8f, float(v)), 0); b.px(f.at(.8f, v + 1.f), 0); } b.px(f.at(1.2f, 16.2f), 0); }
}
inline void cangkir(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  for (int v = 1; v <= 8; ++v) b.cap(f.at(-4, float(v)), f.at(4, float(v)), .7f + p.gr, p.c);
  b.circle(f.at(6, 4.5f), 2.6f + p.gr, p.c);
  if (p.c == 1) {
    b.circle(f.at(6, 4.5f), 1.2f, 0); b.line(f.at(-3, 7), f.at(3, 7), 0);
    for (int i = -1; i <= 1; ++i) for (int k = 0; k < 4; ++k) b.px(f.at(i * 2.6f + sinf(x.t * 4 + i + k * .9f) * 1.2f, 10 + k * 1.6f), 1);
  }
}
inline void kamera(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  for (int v = 0; v <= 8; ++v) b.cap(f.at(-7, float(v)), f.at(7, float(v)), .7f + p.gr, p.c);
  for (float v = 9; v <= 10.5f; v += .5f) b.cap(f.at(-3, v), f.at(3, v), .6f + p.gr, p.c);
  b.circle(f.at(0, 4), 3.4f + p.gr, p.c);
  if (p.c == 1) { b.circle(f.at(0, 4), 2.5f, 0); b.circle(f.at(0, 4), 1, 1); if (int(x.t * 2) % 2) b.circle(f.at(8.5f, 10), 1.2f, 1); }
}
inline void ponsel(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  for (int v = 0; v <= 11; ++v) b.cap(f.at(-3.6f, float(v)), f.at(3.6f, float(v)), .6f + p.gr, p.c);
  if (p.c == 1) { for (float v = 1.5f; v <= 9.5f; v += .5f) b.line(f.at(-2.5f, v), f.at(2.5f, v), 0); b.line(f.at(-1.6f, 8.4f), f.at(1.6f, 8.4f), 1); if (int(x.t * 3) % 2) b.circle(f.at(0, 5), .9f, 1); }
}
inline void lilin(const Brush& b, const Frame& f, const Pass& p, const PropCtx& x) {
  const float fl = sinf(x.t * 9) * .6f; b.cap(f.at(0, 0), f.at(0, 7), 1.3f + p.gr, p.c);
  b.circle(f.at(fl, 10.3f), 1.8f + p.gr, p.c); b.cap(f.at(fl, 10.3f), f.at(fl * 1.6f, 12.8f), .6f + p.gr, p.c);
  if (p.c == 1) { b.px(f.at(fl, 9.9f), 0); for (int v = 1; v < 7; v += 2) b.px(f.at(0, float(v)), 0); }
}
inline const PropDef* props(unsigned& n) {
  static const PropDef t[] = {
    {"mic_bola", "Mic bola", Group::Mic, 1, 1, 1, 1, micBola},
    {"mic_retro", "Mic retro", Group::Mic, 1, 1, 1, 1, micRetro},
    {"mic_ramping", "Mic ramping", Group::Mic, 1, 1, 1, 1, micRamping},
    {"mic_kabel", "Mic berkabel", Group::Mic, 1, 1, 1, 1, micKabel},
    {"payung", "Payung", Group::Tool, 1, 1, 2.7f, 3.6f, payung},
    {"balon", "Balon", Group::Tool, 1, 1, 2.8f, 4, balon},
    {"lightstick", "Lightstick", Group::Tool, 1, 1, 1.3f, 1.8f, lightstick},
    {"wand", "Tongkat bintang", Group::Tool, 1, 1, 1.2f, 1.6f, wand},
    {"megafon", "Megafon", Group::Tool, 1, 1, 1, 1, megafon},
    {"bendera", "Bendera", Group::Tool, 1, 1, 1.5f, 2.2f, bendera},
    {"kipas", "Kipas", Group::Tool, 1, 1, 1, 1, kipas},
    {"papan", "Papan hati", Group::Tool, 1, 1, 1.3f, 1.8f, papan},
    {"pompom", "Pom-pom", Group::Tool, 1, 1, 1, 1, pompom},
    {"buket", "Buket bunga", Group::Tool, 1, 1, 1, 1, buket},
    {"eskrim", "Es krim", Group::Tool, 1, 1, 1, 1, eskrim},
    {"cangkir", "Cangkir kopi", Group::Tool, 1, 1, 1, 1, cangkir},
    {"kamera", "Kamera", Group::Tool, 1, 1, 1, 1, kamera},
    {"ponsel", "Ponsel", Group::Tool, 1, 1, 1, 1, ponsel},
    {"lilin", "Lilin", Group::Tool, 1, 1, 1, 1, lilin},
  };
  n = sizeof t / sizeof t[0]; return t;
}

// ---------------------------------------------------------------- lookup + composition
inline const HandStyle* findHand(const char* id) {
  unsigned n; const HandStyle* t = handStyles(n);
  for (unsigned i = 0; id && i < n; ++i) if (!strcmp(t[i].id, id)) return &t[i];
  return nullptr;
}
inline int findProp(const char* id) {
  unsigned n; const PropDef* t = props(n);
  for (unsigned i = 0; id && i < n; ++i) if (!strcmp(t[i].id, id)) return int(i);
  return -1;
}

struct Held {
  const HandStyle* hand = nullptr;    // required
  const PropDef* prop = nullptr;      // null = empty hand
  float len = 1.f, t = 0.f, K = 1.35f;
  bool second = false;                // a second hand grips the same shaft lower down
};
constexpr float Grip2 = 9.f;

// Draws one floating hand (and what it holds). ang = tilt from straight up (clockwise +), open 0 = fist .. 1 = spread, side +1/-1.
inline void drawHeld(const Canvas& cv, const Held& h, float x, float y, float ang, float open, int side) {
  const Brush b{cv, h.K};
  const Frame f{x, y, sinf(ang), cosf(ang), h.K};
  const Frame f2 = f.moved(0, -Grip2);
  const bool hold = h.prop != nullptr;
  const PropCtx ctx{h.t, h.len, side};
  auto grip = [&](const Frame& g, const Pass& p) {
    b.circle(g.at(0, -.5f), 3.4f + p.gr, p.c);
    if (p.c == 1) for (float v : {-1.f, 1.2f}) b.line(g.at(-2.8f, v), g.at(2.8f, v), 0);
  };
  for (int pass = 0; pass < 2; ++pass) {
    const Pass p{uint16_t(pass), pass ? 0.f : 1.6f};
    if (hold && h.second) b.cap(f.at(0, 0), f.at(0, -Grip2), 1.2f + p.gr, p.c);
    h.hand->draw(b, f, p, hold ? 0.f : open, side, hold);
    if (hold) {
      h.prop->draw(b, f, p, ctx); grip(f, p);
      if (h.second) { h.hand->draw(b, f2, p, 0.f, -side, true); grip(f2, p); }
    }
  }
}
}  // namespace roboprops
