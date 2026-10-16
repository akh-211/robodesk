#include "../RoboSpriteClips.h"

#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace robosprite;

static int pixels(const uint8_t* f) { int n = 0; for (int i = 0; i < FrameBytes; ++i) n += __builtin_popcount(f[i]); return n; }

int main() {
  static_assert(StoreBytes == 12288, "store size");
  std::vector<uint8_t> store(StoreBytes);
  Library lib;
  assert(!lib.ready() && !lib.build(nullptr) && lib.frame(Clip::Heart, 0) == nullptr);
  assert(lib.build(store.data()) && lib.ready());
  assert(lib.frame(Clip::None, 0) == nullptr && lib.frame(Clip::Heart, Frames) == nullptr);
  for (uint8_t c = 0; c < ClipCount; ++c) {  // every clip draws, animates, and does not flood the sprite
    std::set<std::string> distinct; int lit = 0;
    for (uint8_t f = 0; f < Frames; ++f) {
      const uint8_t* fr = lib.frame(Clip(c), f);
      const int n = pixels(fr);
      if (n > 0) ++lit;
      assert(n < Size * Size * 3 / 4);
      distinct.insert(std::string(reinterpret_cast<const char*>(fr), FrameBytes));
    }
    assert(lit >= 9 && distinct.size() >= 4);
  }
  assert(clipFor("love") == Clip::Heart && clipFor("sleepy") == Clip::Zzz && clipFor("calm") == Clip::None && clipFor(nullptr) == Clip::None);

  { // halo covers every lit pixel plus a ring, and never clips outside the sprite
    uint8_t m[FrameBytes];
    const uint8_t* fr = lib.frame(Clip::Heart, 0);
    haloMask(fr, m);
    for (int i = 0; i < FrameBytes; ++i) assert((fr[i] & m[i]) == fr[i]);
    assert(pixels(m) > pixels(fr));
  }
  Player p;
  assert(!p.start(Clip::None, roboarb::Tier::Owner, 0));
  assert(p.start(Clip::Heart, roboarb::Tier::Activity, 1000) && p.active(1000) && p.clip() == Clip::Heart);
  assert(!p.start(Clip::Note, roboarb::Tier::Micro, 1200) && p.clip() == Clip::Heart);  // lower tier dropped while playing
  assert(p.start(Clip::Anger, roboarb::Tier::Owner, 1300) && p.clip() == Clip::Anger);  // owner interrupts
  assert(!p.start(Clip::Dizzy, roboarb::Tier::Emotion, 1400) && p.clip() == Clip::Anger);  // emotion does not cut an owner clip
  Player q;
  assert(q.start(Clip::Sparkle, roboarb::Tier::Activity, 0));
  assert(!q.active(HoldMs));                                      // lease expires on its own
  assert(!q.start(Clip::Note, roboarb::Tier::Activity, 3500));    // rate limit for routine and below
  assert(q.start(Clip::Note, roboarb::Tier::Activity, MinGapMs + 1));
  assert(q.frameAt(MinGapMs + 1 + 3 * FrameMs) == 3 && q.frameAt(MinGapMs + 1 + 13 * FrameMs) == 1);
  std::cout << "robo_sprite_clips_test OK\n";
  return 0;
}
