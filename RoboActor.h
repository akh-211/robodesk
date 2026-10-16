#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "RoboArbiter.h"
#include "RoboProps.h"

// EMO-style performer: the eyes always stay on screen. The performer only (1) moves the whole face a little (sway, bounce, hop,
// nod, shake) through faceOffset(), (2) pushes a small floating hand (RoboProps.h: style + held prop such as a mic), or waving/clapping hands, up from the bottom edge, and
// (3) asks for an eye emotion now and then (takeEyeCue). Nothing is stored as frames, every move is spring-smoothed so any
// duration works, and nothing loops visibly: each move gets random amplitude/speed/mirror/phase, the next move is drawn from
// a weighted pool that skips the last two, joints carry layered noise, and cues/props appear at random times. A dance
// soft-locks to the real beat when beat() is fed, otherwise it drifts its own tempo. Playback is leased through RoboArbiter.
namespace roboactor {
enum class Act : uint8_t { None, Sing, Dance, Wave, Love, Happy, Laugh, Sleepy, Sad, Question, Angry, Dizzy };
enum Move : uint8_t {
  Rest, SingMic, SingSwing, SingHigh, SingSway, SingPoint, DSway, DBounce, DArms, DHop, DClap, DDisco, DShuffle,
  WHello, WRaise, WBye, LoveSway, HappyHop, LaughShake, SleepNod, SadDroop, AskTilt, AngryShake, DizzyWobble, MoveCount
};
// Corner prop ids match robosprite::Clip order (Heart, Sparkle, Note, Zzz, Drop, Question, Anger, Dizzy); 255 = none.
constexpr uint8_t NoProp = 255;

constexpr float Pi = 3.14159265f, TwoPi = 6.2831853f;
constexpr uint32_t OutMs = 450, MaxOpenMs = 120000;
constexpr float HiddenY = 82.f;  // a hand parked below the screen

struct Rng {
  uint32_t s = 2463534242u;
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float uni() { return float(next() >> 8) / 16777216.f; }
  float range(float a, float b) { return a + (b - a) * uni(); }
  bool chance(float p) { return uni() < p; }
};

struct Spring {
  float x = 0, v = 0;
  void step(float tgt, float dt, float k) {
    const int n = dt > .03f ? 3 : 1;
    const float h = dt / n, c = 2.f * sqrtf(k) * .8f;
    for (int i = 0; i < n; ++i) { v += (k * (tgt - x) - c * v) * h; x += v * h; }
  }
};

// Hand: palm centre in screen pixels, ang = tilt from straight up (clockwise +), open 0 = fist .. 1 = fingers spread.
struct Hand { float x = 64, y = HiddenY, ang = 0, open = 0; };
struct Pose { float fx = 0, fy = 0; Hand h[2]; bool mic = false; };
struct Var { float A = 1, S = 1, mir = 1, ph = 0; bool hold = false; };   // hold: hand 0 carries a prop, so keep it raised

inline float absf(float v) { return v < 0 ? -v : v; }
inline float hash01(uint32_t n) { n ^= n >> 16; n *= 0x7feb352dU; n ^= n >> 15; n *= 0x846ca68bU; n ^= n >> 16; return float(n >> 8) / 16777216.f; }

// t = seconds in this move, bt = beat counter, voice 0..1 (sing only).
inline void applyMove(Move m, float t, float bt, const Var& v, float voice, Pose& p) {
  const float W = t * v.S * 3.2f, B = TwoPi * bt, A = v.A, ph = v.ph, mir = v.mir;
  auto hand = [&](int i, float x, float y, float ang, float open) { p.h[i].x = x; p.h[i].y = y; p.h[i].ang = ang; p.h[i].open = open; };
  const int F = mir > 0 ? 1 : 0, O = 1 - F;                         // featured / other hand
  const float fxs = mir > 0 ? 1.f : -1.f;
  switch (m) {
    case SingMic: p.mic = true; hand(0, 70.f + 4.f * sinf(W * .5f + ph), 56.f - 4.f * voice, -.55f + .15f * sinf(W), 0); p.fx = .06f * A * sinf(W * .5f); p.fy = -.06f * voice; break;
    case SingSwing: p.mic = true; hand(0, 64.f + 22.f * A * sinf(W * .6f), 55.f - 2.f * voice, -.5f + .4f * sinf(W * .6f), 0);
      hand(1, 114.f, 52.f - 6.f * (.5f + .5f * sinf(W * 1.3f)), .45f + .25f * sinf(W * 2.6f), 1.f); p.fx = .1f * A * sinf(W * .6f); break;
    case SingHigh: p.mic = true; hand(0, 72.f, 46.f - 3.f * voice, -.3f, 0); p.fy = -.12f - .04f * voice; p.fx = .04f * sinf(W * .4f); break;
    case SingSway: p.mic = true; hand(0, 66.f, 54.f - 3.f * voice, -.5f, 0); hand(1, 14.f, 58.f - 8.f * (.5f + .5f * sinf(W * .7f)), -.3f - .2f * sinf(W * 1.4f), 1.f); p.fx = .18f * A * sinf(W * .5f + ph); break;
    case SingPoint: p.mic = true; hand(0, 54.f, 58.f - 2.f * voice, -.5f, 0); hand(1, 108.f, 44.f + 2.f * sinf(W * 3.f), .9f + .08f * sinf(W * 4.f), .4f); p.fx = .08f * fxs * sinf(W * .3f); break;
    case DSway: p.fx = .22f * A * sinf(B * .5f); p.fy = .03f * (.5f - .5f * cosf(B));
      hand(0, 18.f, 56.f - 9.f * (.5f + .5f * sinf(B * .5f)), -.35f + .2f * sinf(B), 1.f); hand(1, 110.f, 56.f - 9.f * (.5f - .5f * sinf(B * .5f)), .35f - .2f * sinf(B), 1.f); break;
    case DBounce: p.fy = .12f * A * (.5f - .5f * cosf(B));
      hand(0, 26.f, 60.f - 6.f * (.5f + .5f * sinf(B)), -.3f, 0); hand(1, 102.f, 60.f - 6.f * (.5f - .5f * sinf(B)), .3f, 0); break;
    case DArms: hand(0, 16.f, 44.f + 5.f * sinf(B + ph), -.45f + .35f * sinf(B * 2.f), 1.f); hand(1, 112.f, 44.f + 5.f * sinf(B + ph + 1.6f), .45f - .35f * sinf(B * 2.f), 1.f); p.fy = .03f * sinf(B); break;
    case DHop: { const float j = absf(sinf(Pi * bt)); p.fy = -.18f * A * j; hand(0, 18.f, 58.f - 14.f * j, -.4f, 1.f); hand(1, 110.f, 58.f - 14.f * j, .4f, 1.f); break; }
    case DClap: { const float c = .5f + .5f * cosf(B); hand(0, 64.f - 3.f - 9.f * c, 52.f, .5f, 1.f); hand(1, 64.f + 3.f + 9.f * c, 52.f, -.5f, 1.f); p.fy = .05f * (1.f - c); break; }
    case DDisco: { const float w = cosf(B * .25f); hand(F, 106.f, 56.f - 18.f * (.5f + .5f * w), .6f, .5f); hand(O, 22.f, 56.f - 18.f * (.5f - .5f * w), -.6f, .5f); p.fx = .14f * w * fxs; break; }
    case DShuffle: p.fx = .3f * A * sinf(B * .5f); hand(0, 22.f, 60.f - 4.f * sinf(B), -.2f, 0); hand(1, 106.f, 60.f + 4.f * sinf(B), .2f, 0); break;
    case WHello: hand(1, 104.f, 44.f, .5f * sinf(W * 5.f) * A, 1.f); p.fx = -.08f; break;
    case WRaise: hand(1, 100.f, 38.f - 1.5f * sinf(W * 2.f), .05f * sinf(W * 6.f), 1.f); p.fy = -.04f; break;
    case WBye: hand(1, 104.f, 46.f, .55f * sinf(W * 2.4f), 1.f); p.fx = -.06f; break;
    case LoveSway: p.fx = .2f * sinf(W * .6f + ph); p.fy = .05f * sinf(W * 1.2f); break;
    case HappyHop: p.fy = -.2f * A * absf(sinf(W * .9f)); break;
    case LaughShake: p.fy = .08f * sinf(W * 5.f); p.fx = .06f * sinf(W * 7.f); break;
    case SleepNod: { const float k = sinf(W * .55f + ph), dip = k > 0 ? k * k * k : .05f * k; p.fy = .2f * dip; p.fx = .04f * sinf(W * .3f); break; }
    case SadDroop: p.fy = .15f + .02f * sinf(W * .4f); p.fx = .03f * sinf(W * .3f); break;
    case AskTilt: p.fx = .18f * mir; p.fy = -.05f + .02f * sinf(W * .5f); break;
    case AngryShake: p.fx = .07f * sinf(W * 14.f); p.fy = .02f * sinf(W * 9.f); break;
    case DizzyWobble: p.fx = .35f * sinf(W * .8f); p.fy = .1f * sinf(W * 1.6f); break;
    default: break;
  }
  if (v.hold && p.h[0].y >= HiddenY) { p.h[0].x = 74.f + 5.f * sinf(W * .45f + ph) * A; p.h[0].y = 56.f + 2.f * sinf(W * .8f); p.h[0].ang = -.3f + .18f * sinf(W * .6f + ph); p.h[0].open = 0; }
}

struct Opt { Move m; uint8_t w; };
inline const Opt* poolFor(Act a, unsigned& n) {
  static const Opt sing[] = {{SingMic, 4}, {SingSwing, 3}, {SingHigh, 2}, {SingSway, 3}, {SingPoint, 2}};
  static const Opt dance[] = {{DSway, 3}, {DBounce, 3}, {DArms, 3}, {DHop, 2}, {DClap, 2}, {DDisco, 2}, {DShuffle, 2}};
  static const Opt wave[] = {{WHello, 4}, {WRaise, 2}, {WBye, 1}};
  static const Opt love[] = {{LoveSway, 1}}, happy[] = {{HappyHop, 1}}, laugh[] = {{LaughShake, 1}}, sleepy[] = {{SleepNod, 1}}, sad[] = {{SadDroop, 1}},
                   ask[] = {{AskTilt, 1}}, angry[] = {{AngryShake, 1}}, dizzy[] = {{DizzyWobble, 1}};
#define ROBOACT_POOL(arr) do { n = sizeof(arr) / sizeof(arr[0]); return arr; } while (0)
  switch (a) {
    case Act::Sing: ROBOACT_POOL(sing);
    case Act::Dance: ROBOACT_POOL(dance);
    case Act::Wave: ROBOACT_POOL(wave);
    case Act::Love: ROBOACT_POOL(love);
    case Act::Happy: ROBOACT_POOL(happy);
    case Act::Laugh: ROBOACT_POOL(laugh);
    case Act::Sleepy: ROBOACT_POOL(sleepy);
    case Act::Sad: ROBOACT_POOL(sad);
    case Act::Question: ROBOACT_POOL(ask);
    case Act::Angry: ROBOACT_POOL(angry);
    case Act::Dizzy: ROBOACT_POOL(dizzy);
    default: n = 0; return nullptr;
  }
#undef ROBOACT_POOL
}

inline uint8_t propFor(Act a) {
  switch (a) {
    case Act::Love: return 0;
    case Act::Happy: return 1;
    case Act::Sing: case Act::Dance: case Act::Laugh: return 2;
    case Act::Sleepy: return 3;
    case Act::Sad: return 4;
    case Act::Question: return 5;
    case Act::Angry: return 6;
    case Act::Dizzy: return 7;
    default: return NoProp;
  }
}
// Which RoboProps.h props an activity may carry (weighted ids, never the same twice in a row) and how often it carries one at all.
// To give an activity new props, edit only this table; new props themselves live in RoboProps.h.
struct PropOpt { const char* id; uint8_t w; };
inline const PropOpt* propPoolFor(Act a, unsigned& n, float& chance) {
  static const PropOpt sing[] = {{"mic_bola", 3}, {"mic_retro", 3}, {"mic_ramping", 2}, {"mic_kabel", 2}, {"lightstick", 1}, {"pompom", 1}};
  static const PropOpt dance[] = {{"lightstick", 3}, {"pompom", 3}, {"kipas", 2}, {"bendera", 2}};
  static const PropOpt wave[] = {{"bendera", 2}, {"balon", 2}, {"papan", 2}};
  static const PropOpt love[] = {{"buket", 3}, {"papan", 2}, {"balon", 2}};
  static const PropOpt happy[] = {{"balon", 3}, {"pompom", 2}, {"wand", 1}};
  static const PropOpt sad[] = {{"payung", 1}};
  static const PropOpt ask[] = {{"kamera", 1}};
  static const PropOpt angry[] = {{"megafon", 1}};
  static const PropOpt sleepy[] = {{"cangkir", 1}};
#define ROBOACT_PROPS(arr, ch) do { n = sizeof(arr) / sizeof(arr[0]); chance = (ch); return arr; } while (0)
  switch (a) {
    case Act::Sing: ROBOACT_PROPS(sing, 1.f);
    case Act::Dance: ROBOACT_PROPS(dance, .8f);
    case Act::Wave: ROBOACT_PROPS(wave, .7f);
    case Act::Love: ROBOACT_PROPS(love, .7f);
    case Act::Happy: ROBOACT_PROPS(happy, .6f);
    case Act::Sad: ROBOACT_PROPS(sad, .8f);
    case Act::Question: ROBOACT_PROPS(ask, .5f);
    case Act::Angry: ROBOACT_PROPS(angry, .6f);
    case Act::Sleepy: ROBOACT_PROPS(sleepy, .6f);
    default: n = 0; chance = 0; return nullptr;
  }
#undef ROBOACT_PROPS
}
// Emotion name (RoboEmotionMap.h) -> act; None = this emotion stays a plain eye expression.
inline Act actForEmotion(const char* e) {
  if (!e) return Act::None;
  struct M { const char* n; Act a; };
  static const M map[] = {{"love", Act::Love}, {"affectionate", Act::Love}, {"happy", Act::Happy}, {"excited", Act::Happy}, {"proud", Act::Happy},
                          {"laugh", Act::Laugh}, {"sleepy", Act::Sleepy}, {"sad", Act::Sad}, {"anxious", Act::Sad}, {"lonely", Act::Sad},
                          {"curious", Act::Question}, {"thinking", Act::Question}, {"annoyed", Act::Angry}, {"dizzy", Act::Dizzy}};
  for (const M& m : map) if (!strcmp(e, m.n)) return m.a;
  return Act::None;
}

class Actor {
 public:
  void seed(uint32_t s) { rng_.s = s ? s : 1u; }

  // Starts (or morphs into) an act if the arbiter admits the tier. durMs 0 = open-ended until stop() (capped at MaxOpenMs).
  bool start(Act a, roboarb::Tier tier, uint32_t now, uint32_t durMs = 0) {
    if (a == Act::None) return false;
    if (!roboarb::admits(roboarb::decide(tier, tracker_.running(now)))) return false;
    if (tier >= roboarb::Tier::Routine && int32_t(now - nextOkAt_) < 0) return false;
    const bool fresh = state_ == State::Off;
    act_ = a; tier_ = tier; state_ = State::Run; lastAt_ = fresh ? 0 : lastAt_; t_ = 0; moveT0_ = 0;
    until_ = durMs ? now + durMs : 0; last_[0] = last_[1] = Rest;
    tracker_.start(tier, now, (durMs ? durMs : MaxOpenMs) + OutMs + 200u);
    interval_ = intervalTarget_ = rng_.range(470.f, 560.f); bt_ = 0; lastBeatAt_ = 0; np1_ = rng_.range(0, TwoPi); np2_ = rng_.range(0, TwoPi);
    gateOn_ = false; gateEnd_ = 0; cue_ = nullptr; cueAt_ = now + uint32_t(rng_.range(600.f, 1800.f));
    propId_ = propFor(a); propVis_ = false; propAt_ = rng_.range(.2f, .9f); propLeft_ = rng_.chance(.5f);
    move_ = Rest; moveEnd_ = 0;
    propSel_ = forcedProp_ >= 0 ? forcedProp_ : pickProp(a);
    { unsigned pn = 0; const roboprops::PropDef* pd = roboprops::props(pn);
      const bool has = propSel_ >= 0 && unsigned(propSel_) < pn;
      holdSecond_ = has && pd[propSel_].hands == 1 && rng_.chance(.2f);   // sometimes both hands grip the shaft
      lenSp_ = Spring(); lenSp_.x = has ? pd[propSel_].lenMin : 1.f; lenTgt_ = has ? pd[propSel_].lenDef : 1.f; }
    var_.hold = propSel_ >= 0;
    if (fresh) resetSprings();
    return true;
  }
  // Probabilistic reaction used for emotions so the robot never answers every single feeling the same way.
  bool react(Act a, roboarb::Tier tier, uint32_t now, float level) {
    if (a == Act::None || level < .4f) return false;
    if (!rng_.chance(tier <= roboarb::Tier::Owner ? .7f : .35f + .4f * level)) return false;
    return start(a, tier, now, uint32_t(rng_.range(2300.f, 3900.f) * (.8f + .4f * level)));
  }
  void stop(uint32_t now) { if (state_ == State::Run) beginOut(now); }
  bool active(uint32_t now) {
    if (state_ != State::Off && !tracker_.running(now).active) finish(now);
    return state_ != State::Off;
  }
  Act act() const { return act_; }
  Move move() const { return move_; }
  roboarb::Tier tier() const { return tier_; }
  void beat(uint32_t now, uint32_t intervalMs) {
    if (state_ != State::Run || act_ != Act::Dance) return;
    if (intervalMs >= 300 && intervalMs <= 1000) intervalTarget_ = float(intervalMs);
    lastBeatAt_ = now;
    bt_ -= (bt_ - roundf(bt_)) * .4f;
  }
  void setVoice(uint32_t now, float level) { voice_ = level < 0 ? 0 : (level > 1 ? 1 : level); voiceAt_ = now; }
  uint8_t propId() const { return propId_; }
  bool propVisible() const { return state_ == State::Run && propId_ != NoProp && propVis_; }
  int propX() const { return propLeft_ ? 0 : 96; }
  uint8_t propFrame(uint32_t now) const { return uint8_t((now / 100u) % 12u); }
  // Whole-face offset in -1..1 for Character::moveFace while acting; false once the act has fully ended.
  bool faceOffset(float& x, float& y) const { x = j_[FX].x; y = j_[FY].x; return state_ != State::Off; }
  // Eye emotion to apply now and then ("happy", "excited", ...); returns once per cue, then nullptr until the next one.
  const char* takeEyeCue() {
    if (state_ != State::Run || !cue_) return nullptr;
    const char* c = cue_; cue_ = nullptr; return c;
  }
  float cueLevel() const { return cueLevel_; }

  void update(uint32_t now) {
    if (state_ == State::Off) return;
    if (!lastAt_) lastAt_ = now - 33u;
    float dt = float(int32_t(now - lastAt_)) * .001f;
    if (dt <= 0) return;
    if (dt > .1f) dt = .1f;
    lastAt_ = now; t_ += dt;
    if (state_ == State::Run && until_ && int32_t(now - until_) >= 0) beginOut(now);
    if (state_ == State::Out && uint32_t(now - outAt_) >= OutMs) { finish(now); return; }
    // tempo: follows real beats while they keep coming, otherwise drifts on its own so it never locks to a metronome
    const bool locked = lastBeatAt_ && uint32_t(now - lastBeatAt_) < 5000u;
    if (!locked) intervalTarget_ = 520.f + 50.f * sinf(t_ * .13f + np1_) + 25.f * sinf(t_ * .31f + np2_);
    interval_ += (intervalTarget_ - interval_) * (dt * 2.f > 1.f ? 1.f : dt * 2.f);
    bt_ += dt * 1000.f / interval_;
    if (state_ == State::Run && int32_t(now - moveEnd_) >= 0) pickMove(now);
    const float vo = (state_ == State::Run && act_ == Act::Sing) ? voice(now) : 0.f;
    Pose p;
    if (state_ == State::Run) applyMove(move_, t_ - moveT0_, bt_, var_, vo, p);
    drive(p, dt);
    if (state_ == State::Run && int32_t(now - cueAt_) >= 0 && (act_ == Act::Sing || act_ == Act::Dance || act_ == Act::Wave)) {
      static const char* const sing[] = {"happy", "happy", "excited", "love"};
      static const char* const dance[] = {"excited", "happy", "happy"};
      static const char* const wave[] = {"happy", "excited"};
      const char* const* set = act_ == Act::Sing ? sing : act_ == Act::Dance ? dance : wave;
      const unsigned n = act_ == Act::Sing ? 4u : act_ == Act::Dance ? 3u : 2u;
      cue_ = set[rng_.next() % n]; cueLevel_ = rng_.range(.5f, .9f); cueAt_ = now + uint32_t(rng_.range(2500.f, 6500.f));
    }
    if (state_ == State::Run && propId_ != NoProp && propAt_ <= t_) {
      propVis_ = !propVis_;
      propAt_ = t_ + (propVis_ ? rng_.range(1.8f, 3.2f) : rng_.range(1.5f, 5.f));
      if (propVis_) propLeft_ = rng_.chance(.5f);
    }
  }

  // Draws the hands (and mic) over whatever the eyes engine already put in the buffer. Black halo first so a white hand stays
  // readable on top of a white eye.
  template <class G> void render(G& g, uint32_t now) {
    update(now);
    if (state_ == State::Off) return;
    const roboprops::Canvas cv = roboprops::canvasOf(g);
    unsigned np = 0; const roboprops::PropDef* pt = roboprops::props(np);
    roboprops::Held held; held.hand = handStyle_; held.t = float(now) * .001f;
    for (int i = 0; i < 2; ++i) {
      const float hx = j_[HX0 + i * 4].x, hy = j_[HY0 + i * 4].x, ang = j_[HA0 + i * 4].x, op = j_[HO0 + i * 4].x;
      if (hy > 70.f) continue;                                    // parked below the screen
      const bool holds = i == 0 && state_ == State::Run && propSel_ >= 0 && unsigned(propSel_) < np;
      if (i == 1 && holdSecond_ && state_ == State::Run && propSel_ >= 0) continue;   // hand 1 is busy gripping the shaft
      held.prop = holds ? &pt[propSel_] : nullptr; held.len = lenSp_.x < .5f ? .5f : lenSp_.x; held.second = holds && holdSecond_;
      roboprops::drawHeld(cv, held, hx, hy, ang, op, i ? -1 : 1);
    }
  }
  // Hand style / held prop (ids from RoboProps.h). setProp(nullptr) = automatic choice per act; false if the id is unknown.
  bool setHandStyle(const char* id) { const roboprops::HandStyle* h = roboprops::findHand(id); if (h) handStyle_ = h; return h != nullptr; }
  bool setProp(const char* id) { if (!id) { forcedProp_ = -1; return true; } const int p = roboprops::findProp(id); if (p >= 0) forcedProp_ = int8_t(p); return p >= 0; }
  const char* heldPropId() const { unsigned n; const roboprops::PropDef* t = roboprops::props(n); return propSel_ >= 0 && unsigned(propSel_) < n ? t[propSel_].id : nullptr; }
  const char* handStyleId() const { return handStyle_->id; }

 private:
  enum State : uint8_t { Off, Run, Out };
  enum J : uint8_t { FX, FY, HX0, HY0, HA0, HO0, HX1, HY1, HA1, HO1, JN };

  void resetSprings() {
    for (int i = 0; i < JN; ++i) j_[i] = Spring();
    for (int i = 0; i < 2; ++i) { j_[HX0 + i * 4].x = i ? 104.f : 24.f; j_[HY0 + i * 4].x = HiddenY; }
  }
  void beginOut(uint32_t now) { state_ = State::Out; outAt_ = now; propVis_ = false; }
  void finish(uint32_t now) {
    state_ = State::Off; tracker_.clear(); propVis_ = false;
    for (int i = 0; i < JN; ++i) j_[i] = Spring();
    nextOkAt_ = now + uint32_t(rng_.range(8000.f, 22000.f));
  }
  // Weighted prop for an activity (table above); -1 = hands stay empty this time. Skips the previous prop when there is a choice.
  int8_t pickProp(Act a) {
    unsigned n = 0; float chance = 0; const PropOpt* pool = propPoolFor(a, n, chance);
    if (!n || !rng_.chance(chance)) return -1;
    int idx[12]; unsigned w[12]; unsigned c = 0, total = 0;
    for (unsigned i = 0; i < n && c < 12; ++i) {
      const int k = roboprops::findProp(pool[i].id); if (k < 0) continue;
      if (n > 1 && k == lastProp_) continue;
      idx[c] = k; w[c] = pool[i].w; total += pool[i].w; ++c;
    }
    if (!c) return -1;
    unsigned r = rng_.next() % (total ? total : 1u), i = 0;
    while (i + 1 < c && r >= w[i]) { r -= w[i]; ++i; }
    lastProp_ = int8_t(idx[i]); return lastProp_;
  }
  void pickMove(uint32_t now) {
    unsigned n = 0; const Opt* pool = poolFor(act_, n);
    if (!n) { moveEnd_ = now + 1000u; return; }
    unsigned total = 0;
    for (unsigned i = 0; i < n; ++i) if (n <= 2 || (pool[i].m != last_[0] && pool[i].m != last_[1])) total += pool[i].w;
    float pickv = rng_.uni() * float(total ? total : 1);
    Move pick = pool[0].m;
    for (unsigned i = 0; i < n; ++i) { if (n > 2 && (pool[i].m == last_[0] || pool[i].m == last_[1])) continue; pickv -= pool[i].w; pick = pool[i].m; if (pickv < 0) break; }
    last_[1] = last_[0]; last_[0] = pick; move_ = pick; moveT0_ = t_;
    var_.hold = propSel_ >= 0;
    if (var_.hold) { unsigned pn = 0; const roboprops::PropDef* pd = roboprops::props(pn);
      if (unsigned(propSel_) < pn) { const float lo = pd[propSel_].lenDef * .85f, hi = pd[propSel_].lenDef * 1.15f; lenTgt_ = rng_.range(lo < pd[propSel_].lenMin ? pd[propSel_].lenMin : lo, hi > pd[propSel_].lenMax ? pd[propSel_].lenMax : hi); } }
    var_.A = rng_.range(.75f, 1.2f); var_.S = rng_.range(.85f, 1.2f); var_.mir = rng_.chance(.5f) ? 1.f : -1.f; var_.ph = rng_.range(0, TwoPi);
    float lenMs;
    if (act_ == Act::Dance) { static const uint8_t beats[] = {2, 4, 4, 8, 8, 16}; lenMs = float(beats[rng_.next() % 6u]) * interval_; }
    else if (act_ == Act::Sing) lenMs = rng_.range(3500.f, 7500.f);
    else if (act_ == Act::Wave) lenMs = 60000.f;
    else lenMs = rng_.range(2200.f, 3600.f);
    moveEnd_ = now + uint32_t(lenMs);
  }
  // Syllable-gated voice envelope: phrases of 2-5 s separated by breaths, each syllable with its own loudness.
  float voice(uint32_t now) {
    if (voiceAt_ && uint32_t(now - voiceAt_) < 300u) return voice_;
    if (t_ >= gateEnd_) { gateOn_ = !gateOn_; gateEnd_ = t_ + (gateOn_ ? rng_.range(2.f, 5.5f) : rng_.range(.5f, 1.4f)); sylRate_ = rng_.range(3.5f, 6.5f); sylBase_ = t_; }
    if (!gateOn_) return 0;
    const float ph = (t_ - sylBase_) * sylRate_; const uint32_t idx = uint32_t(ph); const float f = ph - float(idx);
    return (.35f + .65f * hash01(idx * 2654435761u + uint32_t(sylRate_ * 977.f))) * powf(sinf(Pi * f), .8f);
  }
  float noise(float k) const { return .6f * sinf(1.31f * t_ + k * 1.7f + np1_) + .4f * sinf(2.73f * t_ + k * 3.1f + np2_); }
  void drive(Pose p, float dt) {
    const bool run = state_ == State::Run;
    p.fx += .012f * noise(1); p.fy += .01f * noise(2);                    // layered noise so no pose repeats exactly
    for (int i = 0; i < 2; ++i) {
      if (!run || p.h[i].y >= HiddenY) { p.h[i].y = HiddenY; p.h[i].x = j_[HX0 + i * 4].x; }
      else { p.h[i].x += 1.2f * noise(3 + i); p.h[i].y += 1.2f * noise(5 + i); p.h[i].ang += .05f * noise(7 + i); }
      j_[HX0 + i * 4].step(p.h[i].x, dt, 70.f); j_[HY0 + i * 4].step(p.h[i].y, dt, 70.f);
      j_[HA0 + i * 4].step(p.h[i].ang, dt, 90.f); j_[HO0 + i * 4].step(p.h[i].open, dt, 60.f);
    }
    j_[FX].step(p.fx, dt, 40.f); j_[FY].step(p.fy, dt, 40.f);
    lenSp_.step(lenTgt_, dt, 14.f);
  }

  roboarb::Tracker tracker_;
  Rng rng_;
  Spring j_[JN];
  Var var_;
  Act act_ = Act::None;
  roboarb::Tier tier_ = roboarb::Tier::Micro;
  State state_ = Off;
  Move move_ = Rest, last_[2] = {Rest, Rest};
  uint32_t lastAt_ = 0, until_ = 0, outAt_ = 0, moveEnd_ = 0, nextOkAt_ = 0, lastBeatAt_ = 0, voiceAt_ = 0, cueAt_ = 0;
  float t_ = 0, moveT0_ = 0, bt_ = 0, interval_ = 520.f, intervalTarget_ = 520.f, np1_ = 0, np2_ = 0, voice_ = 0, cueLevel_ = .6f;
  const char* cue_ = nullptr;
  bool gateOn_ = false; float gateEnd_ = 0, sylRate_ = 5.f, sylBase_ = 0;
  const roboprops::HandStyle* handStyle_ = roboprops::findHand("bulat");
  int8_t propSel_ = -1, forcedProp_ = -1, lastProp_ = -1; bool holdSecond_ = false; Spring lenSp_; float lenTgt_ = 1.f;
  uint8_t propId_ = NoProp; bool propVis_ = false, propLeft_ = false; float propAt_ = 0;
};
}  // namespace roboactor
