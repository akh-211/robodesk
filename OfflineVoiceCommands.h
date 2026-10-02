#pragma once

#include <stdint.h>

namespace offline_voice {

enum CommandId : int {
  Happy = 0,
  Curious,
  Shy,
  Love,
  Sad,
  Surprised,
  Thinking,
  Agree,
  Disagree,
  Wink,
  Laugh,
  Sleepy,
  Status,
  PauseInitiative,
  ResumeInitiative,
  Timer5,
  Timer10,
  Timer25,
  Timer45,
  PomodoroStart,
  PomodoroPause,
  PomodoroResume,
  PomodoroStop,
  CommandCount
};

struct Command {
  int id;
  const char* phrase;
  const char* expression;
};

// Keep phrases short and distinct: ESP-SR's Arduino wrapper applies its
// bundled English G2P before passing commands to MultiNet.
static constexpr Command kCommands[] = {
  {Happy, "Show happy", "happy"},
  {Curious, "Look curious", "curious"},
  {Shy, "Look shy", "shy"},
  {Love, "Show love", "love"},
  {Sad, "Look sad", "sad"},
  {Surprised, "Act surprised", "surprised"},
  {Thinking, "Start thinking", "thinking"},
  {Agree, "Show agreement", "agree"},
  {Disagree, "Show disagreement", "disagree"},
  {Wink, "Wink", "wink"},
  {Laugh, "Laugh", "laugh"},
  {Sleepy, "Look sleepy", "sleepy"},
  {Status, "Check status", nullptr},
  {PauseInitiative, "Pause initiative", nullptr},
  {ResumeInitiative, "Resume initiative", nullptr},
  {Timer5, "Start five minute timer", nullptr},
  {Timer10, "Start ten minute timer", nullptr},
  {Timer25, "Start twenty five minute timer", nullptr},
  {Timer45, "Start forty five minute timer", nullptr},
  {PomodoroStart, "Start focus session", nullptr},
  {PomodoroPause, "Pause focus session", nullptr},
  {PomodoroResume, "Resume focus session", nullptr},
  {PomodoroStop, "Stop focus session", nullptr},
};

static constexpr unsigned kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

inline const Command* commandForId(int id) {
  if (id < 0 || id >= CommandCount) return nullptr;
  const Command& command = kCommands[static_cast<unsigned>(id)];
  return command.id == id ? &command : nullptr;
}

}  // namespace offline_voice
