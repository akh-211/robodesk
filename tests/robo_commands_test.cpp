#include "../RoboCommands.h"
#include <cassert>
#include <iostream>
#include <cstring>
using namespace robocmd;

static Parsed p(const char* t) { Parsed r; parse(t, r); return r; }
static bool none(const char* t) { Parsed r; return !parse(t, r) && r.kind == Kind::None; }

int main() {
  assert(p("/timer 5").kind == Kind::Timer && p("/timer 5").minutes == 5);
  assert(p("Timer 10 menit").minutes == 10 && p("set timer 25 minutes").minutes == 25 && p("5 minute timer").minutes == 5);
  assert(p("  /TIMER   45  ").minutes == 45);
  assert(none("timer 0") && none("timer 181") && none("timer") && none("timer lima") && none("buatkan timer 5 menit untuk masak") && none("5 menit"));
  assert(p("/fokus").kind == Kind::PomodoroStart && p("mulai fokus").kind == Kind::PomodoroStart && p("start focus session").kind == Kind::PomodoroStart);
  assert(p("jeda fokus").kind == Kind::PomodoroPause && p("/focus pause").kind == Kind::PomodoroPause);
  assert(p("lanjut fokus").kind == Kind::PomodoroResume && p("resume focus").kind == Kind::PomodoroResume);
  assert(p("stop fokus").kind == Kind::PomodoroStop && p("selesai fokus").kind == Kind::PomodoroStop);
  assert(none("fokus") && none("fokus jeda stop") && none("saya perlu fokus belajar"));   // plain "fokus" alone is conversation
  assert(p("jeda inisiatif").kind == Kind::PauseInitiative && p("resume initiative").kind == Kind::ResumeInitiative && none("inisiatif"));
  assert(none("jeda") && none("lanjut") && none("lanjut jeda inisiatif"));
  assert(p("status").kind == Kind::Status && p("/status").kind == Kind::Status && p("cek status").kind == Kind::Status && p("Check status!").kind == Kind::Status);
  assert(none("status robot sekarang bagaimana"));
  assert(p("/senang").kind == Kind::Expression && !strcmp(p("/senang").emotion, "happy") && !strcmp(p("/sedih").emotion, "sad"));
  assert(!strcmp(p("tunjukkan malu").emotion, "shy") && !strcmp(p("show laugh").emotion, "laugh") && !strcmp(p("/menolak").emotion, "disagree"));
  assert(none("senang") && none("aku senang sekali hari ini") && none("tunjukkan sesuatu") && none("/tidakada"));
  assert(p("/help").kind == Kind::Help && p("bantuan").kind == Kind::Help && none("tolong bantuan saya"));
  assert(none("") && none("   ") && none("!!!") && none("halo robot apa kabar hari ini ya"));
  assert(none("a b c d e f g") && none("pneumonoultramicroscopic"));                       // token and length bounds
  assert(p("/joget").kind == Kind::Perform && !strcmp(p("/joget").emotion, "dance") && !strcmp(p("/nyanyi").emotion, "sing") && !strcmp(p("angkat tangan").emotion, "wave"));
  assert(!strcmp(p("tunjukkan joget").emotion, "dance") && !strcmp(p("/diam").emotion, "stop") && none("joget") && none("nyanyi") && none("aku mau nyanyi lagu baru"));
  assert(strlen(helpText()) < 96);  // runRobotCommand reply buffer is 96 bytes
  // Near-miss suggestions (offline chat): only a suggestion, and only when the whole phrase reduces to one command.
  auto sg = [](const char* t) { Parsed r; return suggest(t, r) ? r : Parsed{}; };
  assert(sg("tolong buat alarm lima menit ya").kind == Kind::Timer && sg("tolong buat alarm lima menit ya").minutes == 5);
  assert(sg("timer lima belas menit").minutes == 15 && sg("pasang timer dua puluh lima menit").minutes == 25 && sg("timer empat puluh lima menit").minutes == 45);
  assert(sg("timer sepuluh menit").minutes == 10 && sg("timer 20 menit dong").minutes == 20);
  assert(sg("pomodoro dong").kind == Kind::PomodoroStart && sg("hentikan fokus").kind == Kind::PomodoroStop && sg("lanjutkan fokus").kind == Kind::PomodoroResume);
  assert(sg("setatus").kind == Kind::Status && sg("cek statuz").kind == Kind::Status && sg("tunjukkan sedihh").kind == Kind::Expression);
  assert(!strcmp(sg("tolong tunjukkan kaget").emotion, "surprised") && sg("jeda inisiatip").kind == Kind::PauseInitiative);
  Parsed none_; assert(!suggest("timer", none_) && sg("timer lima").minutes == 5 && !suggest("tolong ceritakan dongeng", none_) && !suggest("timer 500 menit", none_));
  assert(!suggest("aku lagi fokus belajar matematika hari ini", none_) && !suggest("", none_) && !suggest("kapan timer lima menit selesai berbunyi", none_));
  char d[40]; describe(sg("alarm lima menit"), d, sizeof d); assert(!strcmp(d, "/timer 5"));
  describe(sg("tunjukkan kaget"), d, sizeof d); assert(!strcmp(d, "/kaget"));
  describe(p("stop fokus"), d, sizeof d); assert(!strcmp(d, "stop fokus") && p(d).kind == Kind::PomodoroStop);
  assert(isYes("ya") && isYes("Iya!") && isYes("oke") && !isYes("ya tentu saja") && !isYes("tidak") && !isYes("lanjut"));
  assert(isNo("tidak") && isNo("batal") && !isNo("ya") && !isNo("tidak mau"));
  std::cout << "PASS: chat/voice command parser (timer, focus, initiative, status, expressions, help), exact-phrase rule and bounds\n";
  return 0;
}
