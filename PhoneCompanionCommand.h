#pragma once

#include <stddef.h>
#include <stdint.h>

// Compact, strictly typed commands accepted only inside the authenticated
// phone bridge envelope. No strings, URLs, or arbitrary dashboard actions.
namespace PhoneCompanionCommand {
enum class Operation : uint8_t {
  SetIntensity = 1,
  StartActivity = 2,
  PauseActivity = 3,
  ResumeActivity = 4,
  CancelActivity = 5,
  SetQuietHours = 6
};

struct Command {
  Operation operation = Operation::SetIntensity;
  uint8_t intensity = 0;
  uint8_t activityId = 0;
  bool quietEnabled = false;
  bool hasQuietTimes = false;
  uint16_t quietStartMin = 0;
  uint16_t quietEndMin = 0;
};

inline uint16_t read16(const uint8_t* bytes) {
  return uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
}

inline bool decode(const uint8_t* bytes, size_t size, Command& out) {
  if (!bytes || !size) return false;
  Command next{};
  next.operation = Operation(bytes[0]);
  switch (next.operation) {
    case Operation::SetIntensity:
      if (size != 2 || bytes[1] > 2) return false;
      next.intensity = bytes[1];
      break;
    case Operation::StartActivity:
      if (size != 2 || bytes[1] < 1 || bytes[1] > 13) return false;
      next.activityId = bytes[1];
      break;
    case Operation::PauseActivity:
    case Operation::ResumeActivity:
    case Operation::CancelActivity:
      if (size != 1) return false;
      break;
    case Operation::SetQuietHours:
      if ((size != 2 && size != 6) || bytes[1] > 1) return false;
      next.quietEnabled = bytes[1] != 0;
      if(size==6){next.hasQuietTimes=true;next.quietStartMin = read16(bytes + 2);next.quietEndMin = read16(bytes + 4);if (next.quietStartMin > 1439 || next.quietEndMin > 1439) return false;}
      break;
    default:
      return false;
  }
  out = next;
  return true;
}
}  // namespace PhoneCompanionCommand
