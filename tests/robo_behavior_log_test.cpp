#include "../RoboBehaviorLog.h"

#include <cassert>
#include <cstring>
#include <iostream>

// Formats are a contract with tools/behavior_log_report.py (tests/test_behavior_log_report.py feeds it these lines).
int main() {
  char line[64];
  robobeh::touch(line, sizeof(line), "head", 87);
  assert(!strcmp(line, "LEV,BEH,TOUCH,zone=head,latMs=87\n"));
  robobeh::react(line, sizeof(line), "double", 107);
  assert(!strcmp(line, "LEV,BEH,REACT,id=double,var=107\n"));
  robobeh::sleep(line, sizeof(line), "awake", "drowsy");
  assert(!strcmp(line, "LEV,BEH,SLEEP,from=awake,to=drowsy\n"));
  robobeh::sound(line, sizeof(line), "sleepy", true);
  assert(!strcmp(line, "LEV,BEH,SOUND,cue=sleepy,spont=1\n"));
  robobeh::sound(line, sizeof(line), "affection", false);
  assert(!strcmp(line, "LEV,BEH,SOUND,cue=affection,spont=0\n"));
  robobeh::idle(line, sizeof(line), 2, 4);
  assert(!strcmp(line, "LEV,BEH,IDLE,act=2,life=4\n"));
  // Null names never crash and stay parseable.
  robobeh::touch(line, sizeof(line), nullptr, 0);
  assert(!strcmp(line, "LEV,BEH,TOUCH,zone=?,latMs=0\n"));
  // Truncation stays NUL-terminated inside the buffer.
  char tiny[12];
  assert(robobeh::sleep(tiny, sizeof(tiny), "lightsleep", "deepsleep") > int(sizeof(tiny)));
  assert(strlen(tiny) == sizeof(tiny) - 1);
  std::cout << "robo_behavior_log_test passed\n";
  return 0;
}
