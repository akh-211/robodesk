#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Shared text command parser for the phone chat window (works offline, never reaches Gemini) and the local
// voice commands. Pure policy: it only recognizes a command; the firmware executes it.
// A leading '/' accepts short forms ("/timer 5", "/senang"); without it the WHOLE message must match a known
// phrase exactly, so ordinary conversation is never hijacked.
namespace robocmd {
enum class Kind : uint8_t { None, Expression, Status, PauseInitiative, ResumeInitiative, Timer, PomodoroStart, PomodoroPause, PomodoroResume, PomodoroStop, Help, Perform };

struct Parsed {
  Kind kind = Kind::None;
  char emotion[16] = {};
  uint16_t minutes = 0;
};

namespace detail {
constexpr unsigned MaxWords = 10, MaxWordLen = 15, StrictWords = 6;
struct Words { char w[MaxWords][MaxWordLen + 1]; unsigned n = 0; bool slash = false; bool ok = true; };

inline Words split(const char* text, unsigned limit = StrictWords) {
  Words out{};
  if (!text) { out.ok = false; return out; }
  while (*text == ' ' || *text == '\t') ++text;
  if (*text == '/') { out.slash = true; ++text; }
  unsigned len = 0;
  bool inWord = false;
  for (size_t i = 0; text[i]; ++i) {
    char c = text[i];
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (keep) {
      if (!inWord) { if (out.n >= limit) { out.ok = false; return out; } inWord = true; len = 0; ++out.n; }
      if (len >= MaxWordLen) { out.ok = false; return out; }
      out.w[out.n - 1][len++] = c; out.w[out.n - 1][len] = 0;
    } else inWord = false;   // any punctuation or space ends a word
  }
  return out;
}
inline bool is(const char* a, const char* b) { return !strcmp(a, b); }
inline bool in(const char* a, const char* const* set, unsigned n) { for (unsigned i = 0; i < n; ++i) if (is(a, set[i])) return true; return false; }
#define ROBOCMD_IN(word, ...) ([&] { static const char* const s_[] = {__VA_ARGS__}; return detail::in(word, s_, sizeof(s_) / sizeof(s_[0])); }())
inline const char* emotionFor(const char* w) {
  struct E { const char* word; const char* emotion; };
  static const E t[] = {{"senang", "happy"}, {"happy", "happy"}, {"penasaran", "curious"}, {"curious", "curious"}, {"malu", "shy"}, {"shy", "shy"},
                        {"cinta", "love"}, {"love", "love"}, {"sedih", "sad"}, {"sad", "sad"}, {"kaget", "surprised"}, {"surprised", "surprised"},
                        {"berpikir", "thinking"}, {"thinking", "thinking"}, {"setuju", "agree"}, {"agree", "agree"}, {"menolak", "disagree"}, {"disagree", "disagree"},
                        {"kedip", "wink"}, {"wink", "wink"}, {"tertawa", "laugh"}, {"laugh", "laugh"}, {"ngantuk", "sleepy"}, {"sleepy", "sleepy"}};
  for (const E& e : t) if (is(w, e.word)) return e.emotion;
  return nullptr;
}
inline const char* performFor(const char* w) {   // full-body acts (RoboActor.h): sing / dance / wave, or "stop" to end the current one
  struct E { const char* word; const char* act; };
  static const E t[] = {{"nyanyi", "sing"}, {"bernyanyi", "sing"}, {"sing", "sing"}, {"joget", "dance"}, {"berjoget", "dance"}, {"menari", "dance"}, {"dance", "dance"},
                        {"lambai", "wave"}, {"melambai", "wave"}, {"wave", "wave"}, {"diam", "stop"}, {"cukup", "stop"}};
  for (const E& e : t) if (is(w, e.word)) return e.act;
  return nullptr;
}
inline bool number(const char* w, unsigned& v) {
  if (!*w) return false;
  v = 0;
  for (; *w; ++w) { if (*w < '0' || *w > '9') return false; v = v * 10 + unsigned(*w - '0'); if (v > 100000) return false; }
  return true;
}
}  // namespace detail

inline bool parse(const char* text, Parsed& out) {
  using namespace detail;
  out = Parsed{};
  const Words s = split(text);
  if (!s.ok || !s.n) return false;
  unsigned minutes = 0; bool timerWord = false, numberSeen = false, extraWord = false;
  bool focus = false, initiative = false, pause = false, resume = false, stop = false, start = false;
  for (unsigned i = 0; i < s.n; ++i) {
    const char* w = s.w[i]; unsigned v = 0;
    if (number(w, v)) { if (numberSeen) return false; numberSeen = true; minutes = v; }
    else if (is(w, "timer")) timerWord = true;
    else if (ROBOCMD_IN(w, "fokus", "focus")) focus = true;
    else if (ROBOCMD_IN(w, "inisiatif", "initiative")) initiative = true;
    else if (ROBOCMD_IN(w, "jeda", "pause")) pause = true;
    else if (ROBOCMD_IN(w, "lanjut", "lanjutkan", "resume")) resume = true;
    else if (ROBOCMD_IN(w, "stop", "selesai", "berhenti")) stop = true;
    else if (ROBOCMD_IN(w, "mulai", "start")) start = true;
    else if (!ROBOCMD_IN(w, "set", "atur", "buat", "menit", "min", "minute", "minutes", "sesi", "session", "a")) extraWord = true;
  }
  // Timer: "timer 5", "/timer 5", "timer 5 menit", "set timer 10 minutes", "5 minute timer"
  if (timerWord && numberSeen && !extraWord && !focus && !initiative && !pause && !resume && !stop) {
    if (minutes < 1 || minutes > 180) return false;
    out.kind = Kind::Timer; out.minutes = uint16_t(minutes); return true;
  }
  if (numberSeen || timerWord) return false;
  // Focus (pomodoro): a bare "/fokus" starts; without the slash an action word is required
  if (focus && !initiative && !extraWord) {
    const unsigned actions = unsigned(pause) + unsigned(resume) + unsigned(stop);
    if (actions > 1 || (actions && start)) return false;
    if (pause) out.kind = Kind::PomodoroPause;
    else if (resume) out.kind = Kind::PomodoroResume;
    else if (stop) out.kind = Kind::PomodoroStop;
    else if (start || s.slash) out.kind = Kind::PomodoroStart;
    else return false;
    return true;
  }
  // Initiative on/off
  if (initiative && !focus && !extraWord && (pause != resume) && !stop && !start) { out.kind = pause ? Kind::PauseInitiative : Kind::ResumeInitiative; return true; }
  // Single-word commands
  if (s.n == 1) {
    const char* w = s.w[0];
    if (is(w, "status")) { out.kind = Kind::Status; return true; }
    if (ROBOCMD_IN(w, "help", "bantuan")) { out.kind = Kind::Help; return true; }
    if (s.slash) if (const char* e = emotionFor(w)) { out.kind = Kind::Expression; strcpy(out.emotion, e); return true; }
    if (s.slash) if (const char* e = performFor(w)) { out.kind = Kind::Perform; strcpy(out.emotion, e); return true; }
    return false;
  }
  if (s.n == 2 && !s.slash && ROBOCMD_IN(s.w[0], "cek", "check") && is(s.w[1], "status")) { out.kind = Kind::Status; return true; }
  if (s.n == 2 && ROBOCMD_IN(s.w[0], "tunjukkan", "tampilkan", "show", "look")) {
    if (const char* e = emotionFor(s.w[1])) { out.kind = Kind::Expression; strcpy(out.emotion, e); return true; }
    if (const char* e = performFor(s.w[1])) { out.kind = Kind::Perform; strcpy(out.emotion, e); return true; }
  }
  if (s.n == 2 && ROBOCMD_IN(s.w[0], "angkat", "lambaikan") && is(s.w[1], "tangan")) { out.kind = Kind::Perform; strcpy(out.emotion, "wave"); return true; }
  return false;
}

// ---- Near-match suggestions (offline chat): never executed directly, the robot asks "Maksud kamu ...?" and waits for "ya".
namespace detail {
inline bool near1(const char* a, const char* b) {   // edit distance <= 1
  const size_t la = strlen(a), lb = strlen(b);
  if (la > lb + 1 || lb > la + 1) return false;
  size_t i = 0; while (a[i] && a[i] == b[i]) ++i;
  if (!a[i] && !b[i]) return true;
  if (la == lb) return !strcmp(a + i + 1, b + i + 1);
  return la > lb ? !strcmp(a + i + 1, b + i) : !strcmp(a + i, b + i + 1);
}
inline int numberWord(const char* w) {   // -1: not a number word; "puluh"/"belas" are handled by the caller
  struct N { const char* w; int v; };
  static const N t[] = {{"satu", 1}, {"dua", 2}, {"tiga", 3}, {"empat", 4}, {"lima", 5}, {"enam", 6}, {"tujuh", 7}, {"delapan", 8}, {"sembilan", 9}, {"sepuluh", 10}};
  for (const N& n : t) if (is(w, n.w)) return n.v;
  return -1;
}
inline const char* vocab(const char* w) {   // returns the vocabulary word the (>=5 letters) token is one typo away from
  static const char* const v[] = {"timer", "fokus", "focus", "status", "inisiatif", "initiative", "lanjut", "lanjutkan", "resume", "pause", "mulai", "start", "menit", "minute", "minutes",
                                  "selesai", "berhenti", "senang", "happy", "penasaran", "curious", "cinta", "sedih", "kaget", "surprised", "berpikir", "thinking", "setuju", "agree",
                                  "menolak", "disagree", "tertawa", "laugh", "ngantuk", "sleepy", "tunjukkan", "tampilkan"};
  if (strlen(w) < 5) return w;
  for (const char* c : v) if (is(w, c)) return w;
  for (const char* c : v) if (near1(w, c)) return c;
  return w;
}
}  // namespace detail

// Rewrites a loose phrase ("tolong buat alarm lima belas menit ya", "setatus", "pomodoro dong") into the strict form and parses it.
// Call only after parse() failed. True = out holds the command the user PROBABLY meant.
inline bool suggest(const char* text, Parsed& out) {
  using namespace detail;
  out = Parsed{};
  const Words s = split(text, MaxWords);
  if (!s.ok || !s.n) return false;
  char buf[96] = "/"; size_t len = 1;
  int num = 0; bool inNum = false;
  auto flush = [&] { if (inNum) { len += size_t(snprintf(buf + len, sizeof(buf) - len, " %d", num)); inNum = false; num = 0; } };
  for (unsigned i = 0; i < s.n && len < sizeof(buf) - 24; ++i) {
    const char* w = s.w[i]; unsigned v = 0; int nw = numberWord(w);
    if (number(w, v)) { flush(); num = int(v); inNum = true; continue; }
    if (nw > 0) { if (inNum && num >= 10 && num % 10 == 0 && nw < 10) num += nw; else if (inNum) { flush(); num = nw; } else num = nw; inNum = true; continue; }
    if (is(w, "puluh") && inNum && num >= 1 && num <= 9) { num *= 10; continue; }
    if (is(w, "belas") && inNum && num >= 1 && num <= 9) { num += 10; continue; }
    flush();
    if (ROBOCMD_IN(w, "tolong", "dong", "ya", "yah", "kak", "bisa", "bisakah", "mau", "ingin", "aku", "saya", "please", "can", "you", "the", "nya", "buatkan", "buatin", "pasang", "pasangkan", "nyalakan", "untuk", "selama", "durasi", "robot", "aili", "coba", "lagi", "cek", "check")) continue;
    if (ROBOCMD_IN(w, "alarm", "pengingat", "hitung", "countdown")) w = "timer";
    else if (is(w, "pomodoro")) w = "fokus";
    else if (ROBOCMD_IN(w, "hentikan", "matikan", "akhiri")) w = "stop";
    else if (ROBOCMD_IN(w, "mulailah", "jalankan", "aktifkan")) w = "mulai";
    else if (is(w, "jedakan")) w = "jeda";
    else if (ROBOCMD_IN(w, "setatus", "statuss")) w = "status";
    else w = vocab(w);
    len += size_t(snprintf(buf + len, sizeof(buf) - len, "%s%s", len > 1 ? " " : "", w));
  }
  flush();
  return parse(buf, out);
}

// The strict form of a command, for "Maksud kamu ...?".
inline void describe(const Parsed& p, char* out, size_t cap) {
  switch (p.kind) {
    case Kind::Timer: snprintf(out, cap, "/timer %u", unsigned(p.minutes)); return;
    case Kind::PomodoroStart: snprintf(out, cap, "/fokus"); return;
    case Kind::PomodoroPause: snprintf(out, cap, "jeda fokus"); return;
    case Kind::PomodoroResume: snprintf(out, cap, "lanjut fokus"); return;
    case Kind::PomodoroStop: snprintf(out, cap, "stop fokus"); return;
    case Kind::PauseInitiative: snprintf(out, cap, "jeda inisiatif"); return;
    case Kind::ResumeInitiative: snprintf(out, cap, "lanjut inisiatif"); return;
    case Kind::Status: snprintf(out, cap, "/status"); return;
    case Kind::Help: snprintf(out, cap, "/help"); return;
    case Kind::Perform: snprintf(out, cap, "/%s", !strcmp(p.emotion, "sing") ? "nyanyi" : !strcmp(p.emotion, "dance") ? "joget" : !strcmp(p.emotion, "wave") ? "lambai" : "diam"); return;
    case Kind::Expression: {
      const char* word = "senang";
      static const char* const ids[] = {"senang", "penasaran", "malu", "cinta", "sedih", "kaget", "berpikir", "setuju", "menolak", "kedip", "tertawa", "ngantuk"};
      for (const char* id : ids) { const char* e = detail::emotionFor(id); if (e && !strcmp(e, p.emotion)) { word = id; break; } }
      snprintf(out, cap, "/%s", word); return;
    }
    default: snprintf(out, cap, "?"); return;
  }
}

// Answer to a pending "Maksud kamu ...?" question.
inline bool isYes(const char* text) { const detail::Words s = detail::split(text); return s.ok && s.n == 1 && ROBOCMD_IN(s.w[0], "ya", "iya", "yes", "y", "yap", "ok", "oke", "betul", "boleh"); }
inline bool isNo(const char* text) { const detail::Words s = detail::split(text); return s.ok && s.n == 1 && ROBOCMD_IN(s.w[0], "tidak", "nggak", "ngga", "gak", "enggak", "no", "n", "batal", "jangan"); }

inline const char* helpText() {
  return "Coba: /timer 5, /fokus, jeda fokus, /status, /senang /sedih /cinta /joget /nyanyi";
}
}  // namespace robocmd
