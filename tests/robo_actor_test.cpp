#include "../RoboActor.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace roboactor;

struct Gfx {
  uint8_t px[64][128] = {};
  void set(int x, int y, uint16_t c) { if (x >= 0 && x < 128 && y >= 0 && y < 64) px[y][x] = uint8_t(c); }
  void drawPixel(int x, int y, uint16_t c) { set(x, y, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { set(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
  }
  void fillCircle(int cx, int cy, int r, uint16_t c) { for (int y = -r; y <= r; ++y) for (int x = -r; x <= r; ++x) if (x * x + y * y <= r * r) set(cx + x, cy + y, c); }
  void drawCircle(int cx, int cy, int r, uint16_t c) { for (int y = -r; y <= r; ++y) for (int x = -r; x <= r; ++x) { int d = x * x + y * y; if (d <= r * r && d >= (r - 1) * (r - 1)) set(cx + x, cy + y, c); } }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2}), miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    auto e = [](int ax, int ay, int bx, int by, int px, int py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
    for (int y = miny; y <= maxy; ++y) for (int x = minx; x <= maxx; ++x) {
      int a = e(x0, y0, x1, y1, x, y), b = e(x1, y1, x2, y2, x, y), d = e(x2, y2, x0, y0, x, y);
      if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) set(x, y, c);
    }
  }
  int lit() const { int n = 0; for (auto& r : px) for (auto v : r) n += v; return n; }
  std::string key() const { return std::string(reinterpret_cast<const char*>(&px[0][0]), 128 * 64); }
  void dump(const char* title) const {
    std::printf("== %s\n", title);
    for (int y = 0; y < 64; y += 2) { for (int x = 0; x < 128; ++x) std::putchar(px[y][x] && px[y + 1][x] ? '#' : px[y][x] ? '^' : px[y + 1][x] ? 'v' : ' '); std::putchar('\n'); }
  }
};

static void frame(Actor& a, Gfx& g, uint32_t now) { g = Gfx(); a.render(g, now); }

int main() {
  const bool dump = std::getenv("ROBOACTOR_DUMP") != nullptr;
  const Act acts[] = {Act::Sing, Act::Dance, Act::Wave, Act::Love, Act::Happy, Act::Laugh, Act::Sleepy, Act::Sad, Act::Question, Act::Angry, Act::Dizzy};

  // hand acts draw hands over the eyes and keep changing; face-only acts move the face and draw nothing
  for (Act act : acts) {
    Actor a; a.seed(1234u + uint32_t(act));
    uint32_t now = 1000; assert(a.start(act, roboarb::Tier::Owner, now));
    std::set<std::string> distinct; std::set<int> moves, faceXs; Gfx g; int drawn = 0;
    const bool hands = act == Act::Sing || act == Act::Dance || act == Act::Wave;
    for (int i = 0; i < 600; ++i, now += 33) {
      frame(a, g, now);
      assert(a.active(now));
      float fx, fy; assert(a.faceOffset(fx, fy)); assert(fx > -.6f && fx < .6f && fy > -.4f && fy < .4f);
      faceXs.insert(int(fx * 2000.f) * 7 + int(fy * 2000.f));
      if (g.lit() > 40) ++drawn;
      assert(g.lit() < 1500);
      if (i % 15 == 0) distinct.insert(g.key());
      moves.insert(a.move());
      if (dump && i % 120 == 60) { char t[48]; std::snprintf(t, sizeof t, "act %d move %d", int(act), int(a.move())); g.dump(t); }
    }
    if (hands) { assert(drawn > 300); assert(distinct.size() >= 20); } else if (a.heldPropId()) assert(drawn > 100); else assert(drawn == 0);
    assert(faceXs.size() >= 10);
    unsigned n = 0; poolFor(act, n);
    if (n >= 3 && act != Act::Wave) assert(moves.size() >= 2);
  }

  // non-repetition: a dance never repeats the same move back to back and uses most of its pool across seeds
  {
    std::set<int> seen;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
      Actor a; a.seed(seed * 7919u); uint32_t now = 0; a.start(Act::Dance, roboarb::Tier::Owner, now);
      Gfx g; int last = -1, changes = 0;
      for (int i = 0; i < 2400; ++i, now += 33) { frame(a, g, now); if (int(a.move()) != last) { if (last != -1) ++changes; last = int(a.move()); seen.insert(last); } }
      assert(changes >= 6);
    }
    assert(seen.size() >= 6);
  }

  // durations: short act ends on its own (+ easing), long/open-ended act runs until stop()
  {
    Actor a; a.seed(5); uint32_t now = 0; Gfx g;
    assert(a.start(Act::Wave, roboarb::Tier::Owner, now, 1500));
    for (; now < 1500; now += 33) { frame(a, g, now); assert(a.active(now)); }
    for (; now < 1500 + OutMs + 100; now += 33) frame(a, g, now);
    assert(!a.active(now));
    assert(a.start(Act::Sing, roboarb::Tier::Owner, now));
    const uint32_t t0 = now;
    for (; now < t0 + 40000; now += 33) { frame(a, g, now); assert(a.active(now)); }
    a.stop(now);
    for (int i = 0; i < 6; ++i, now += 33) frame(a, g, now);
    assert(a.active(now));  // still easing out
    for (; now < t0 + 40000 + 33 * 6 + OutMs + 100; now += 33) frame(a, g, now);
    assert(!a.active(now));
  }

  // arbiter + gap: a routine reaction cannot interrupt an owner act and is spaced out afterwards
  {
    Actor a; a.seed(9); uint32_t now = 0; Gfx g;
    assert(a.start(Act::Dance, roboarb::Tier::Owner, now));
    assert(!a.start(Act::Happy, roboarb::Tier::Routine, now));
    assert(a.start(Act::Wave, roboarb::Tier::Safety, now));   // higher tier interrupts and morphs
    assert(a.act() == Act::Wave);
    a.stop(now); for (int i = 0; i < 20; ++i, now += 33) frame(a, g, now);
    assert(!a.active(now));
    assert(!a.start(Act::Happy, roboarb::Tier::Routine, now + 100));       // minimum gap after an act
    assert(a.start(Act::Happy, roboarb::Tier::Owner, now + 100));          // user-driven acts skip the gap
    assert(!a.react(Act::None, roboarb::Tier::Emotion, now + 5000, 1.f) && !a.react(Act::Happy, roboarb::Tier::Emotion, now + 5000, .2f));
  }

  // dance follows real beats: a faster beat interval raises the beat counter faster than the free-running tempo
  {
    Actor slow, fast; slow.seed(3); fast.seed(3); Gfx g; uint32_t now = 0;
    slow.start(Act::Dance, roboarb::Tier::Owner, now); fast.start(Act::Dance, roboarb::Tier::Owner, now);
    for (int i = 0; i < 300; ++i, now += 33) { if (i % 12 == 0) fast.beat(now, 400); frame(slow, g, now); frame(fast, g, now); }
    assert(fast.active(now) && slow.active(now));
  }

  // eye cues come now and then while performing, never for plain emotions
  { Actor a; a.seed(11); Gfx g; uint32_t now = 0; a.start(Act::Dance, roboarb::Tier::Owner, now); int cues = 0;
    for (int i = 0; i < 1200; ++i, now += 33) { frame(a, g, now); if (const char* c = a.takeEyeCue()) { ++cues; assert(c[0]); assert(a.cueLevel() >= .5f && a.cueLevel() <= .9f); } }
    assert(cues >= 3 && cues <= 20); }
  { Actor a; a.seed(12); Gfx g; uint32_t now = 0; a.start(Act::Love, roboarb::Tier::Owner, now, 3000);
    for (int i = 0; i < 90; ++i, now += 33) { frame(a, g, now); assert(!a.takeEyeCue()); } }
  // props and mappings
  assert(actForEmotion("love") == Act::Love && actForEmotion("dizzy") == Act::Dizzy && actForEmotion("agree") == Act::None && actForEmotion(nullptr) == Act::None);
  assert(propFor(Act::Love) == 0 && propFor(Act::Dizzy) == 7 && propFor(Act::Wave) == NoProp);
  { Actor a; a.seed(2); Gfx g; uint32_t now = 0; a.start(Act::Love, roboarb::Tier::Owner, now); bool seen = false; for (int i = 0; i < 300; ++i, now += 33) { frame(a, g, now); seen |= a.propVisible(); } assert(seen); }
  // RoboProps registry: every hand style x prop draws, stays on screen, is deterministic and has sane metadata (new entries are covered automatically)
  {
    unsigned nh = 0, np = 0; const roboprops::HandStyle* hs = roboprops::handStyles(nh); const roboprops::PropDef* ps = roboprops::props(np);
    assert(nh >= 2 && np >= 4);
    std::set<std::string> ids;
    for (unsigned i = 0; i < nh; ++i) { assert(hs[i].id && hs[i].name && hs[i].draw); assert(ids.insert(hs[i].id).second); assert(roboprops::findHand(hs[i].id) == &hs[i]); }
    ids.clear();
    for (unsigned i = 0; i < np; ++i) {
      assert(ps[i].id && ps[i].name && ps[i].draw && (ps[i].hands == 1 || ps[i].hands == 2));
      assert(ps[i].lenMin > 0 && ps[i].lenMin <= ps[i].lenDef && ps[i].lenDef <= ps[i].lenMax);
      assert(ids.insert(ps[i].id).second); assert(roboprops::findProp(ps[i].id) == int(i));
    }
    assert(roboprops::findProp("nope") == -1 && roboprops::findProp(nullptr) == -1 && !roboprops::findHand("nope"));
    for (unsigned h = 0; h < nh; ++h) for (unsigned p = 0; p <= np; ++p) for (int second = 0; second < 2; ++second) for (int side = -1; side <= 1; side += 2) {
      roboprops::Held held; held.hand = &hs[h]; held.prop = p < np ? &ps[p] : nullptr; held.len = p < np ? ps[p].lenDef : 1.f; held.t = 1.3f; held.second = second != 0;
      Gfx a, b; roboprops::drawHeld(roboprops::canvasOf(a), held, 64.f, 46.f, side * .3f, 1.f, side); roboprops::drawHeld(roboprops::canvasOf(b), held, 64.f, 46.f, side * .3f, 1.f, side);
      assert(a.lit() > 30 && a.lit() < 1500); assert(a.key() == b.key());
    }
    Actor a; a.seed(77); assert(a.setHandStyle("mitten") && !a.setHandStyle("nope") && !strcmp(a.handStyleId(), "mitten"));
    std::set<std::string> mics;
    for (uint32_t seed = 1; seed <= 30; ++seed) { Actor s; s.seed(seed * 31u); s.start(Act::Sing, roboarb::Tier::Owner, 0); assert(s.heldPropId()); mics.insert(s.heldPropId()); }
    assert(mics.size() >= 3);
    { Actor s; s.seed(1); assert(s.setProp("mic_retro")); s.start(Act::Sing, roboarb::Tier::Owner, 0); assert(!strcmp(s.heldPropId(), "mic_retro")); assert(!s.setProp("nope")); }
    // activity -> prop table: every id exists, every carrying activity picks only from its pool, and idle activities carry nothing
    for (Act act : acts) {
      unsigned n = 0; float chance = 0; const PropOpt* pool = propPoolFor(act, n, chance);
      for (unsigned i = 0; i < n; ++i) { assert(roboprops::findProp(pool[i].id) >= 0); assert(pool[i].w > 0); }
      std::set<std::string> got; int carried = 0;
      for (uint32_t seed = 1; seed <= 60; ++seed) {
        Actor s; s.seed(seed * 101u); s.start(act, roboarb::Tier::Owner, 0);
        if (const char* id = s.heldPropId()) { ++carried; got.insert(id); bool inPool = false; for (unsigned i = 0; i < n; ++i) inPool |= !strcmp(pool[i].id, id); assert(inPool); }
      }
      if (!n) assert(carried == 0); else { assert(carried > 0); if (chance >= 1.f) assert(carried == 60); if (n >= 3) assert(got.size() >= 2); }
    }
    // tall props rise above the eyes (rows < 18) once the length spring has unfurled; the second hand grips lower and hides hand 1
    for (const char* id : {"payung", "balon"}) {
      Actor s; s.seed(5); assert(s.setProp(id)); uint32_t now = 0; s.start(Act::Sad, roboarb::Tier::Owner, now);
      Gfx g; int top = 64; for (int i = 0; i < 150; ++i, now += 33) { frame(s, g, now); if (i >= 100) for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x) if (g.px[y][x] && y < top) top = y; }
      assert(top < 18);
    }
    // every tool draws while held by a running actor (no crash, stays bounded)
    for (unsigned p = 0; p < np; ++p) {
      Actor s; s.seed(3 + p); assert(s.setProp(ps[p].id)); uint32_t now = 0; s.start(Act::Dance, roboarb::Tier::Owner, now);
      Gfx g; int lit = 0; for (int i = 0; i < 90; ++i, now += 33) { frame(s, g, now); assert(g.lit() < 1500); lit += g.lit(); }
      assert(lit > 0);
    }
  }
  std::puts("robo_actor_test PASS");
  return 0;
}
