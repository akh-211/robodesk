#include <assert.h>
#include "PhoneCompanionCommand.h"

using PhoneCompanionCommand::Command;

int main() {
  Command command{};
  for (uint8_t level = 0; level <= 2; ++level) {
    const uint8_t wire[] = {1, level};
    assert(PhoneCompanionCommand::decode(wire, sizeof(wire), command));
    assert(command.operation == PhoneCompanionCommand::Operation::SetIntensity);
    assert(command.intensity == level);
  }
  const uint8_t badIntensity[] = {1, 3};
  assert(!PhoneCompanionCommand::decode(badIntensity, sizeof(badIntensity), command));
  for (uint8_t id = 1; id <= 13; ++id) {
    const uint8_t wire[] = {2, id};
    assert(PhoneCompanionCommand::decode(wire, sizeof(wire), command));
    assert(command.activityId == id);
  }
  const uint8_t badActivity[] = {2, 14};
  assert(!PhoneCompanionCommand::decode(badActivity, sizeof(badActivity), command));
  const uint8_t pause[] = {3}, resume[] = {4}, cancel[] = {5};
  assert(PhoneCompanionCommand::decode(pause, sizeof(pause), command));
  assert(command.operation == PhoneCompanionCommand::Operation::PauseActivity);
  assert(PhoneCompanionCommand::decode(resume, sizeof(resume), command));
  assert(command.operation == PhoneCompanionCommand::Operation::ResumeActivity);
  assert(PhoneCompanionCommand::decode(cancel, sizeof(cancel), command));
  assert(command.operation == PhoneCompanionCommand::Operation::CancelActivity);
  const uint8_t quietEnabled[] = {6, 1};
  assert(PhoneCompanionCommand::decode(quietEnabled, sizeof(quietEnabled), command));
  assert(command.quietEnabled && !command.hasQuietTimes);
  const uint8_t quietWindow[] = {6, 0, 0x28, 0x05, 0xa4, 0x01}; // 1320–420
  assert(PhoneCompanionCommand::decode(quietWindow, sizeof(quietWindow), command));
  assert(command.hasQuietTimes && command.quietStartMin == 1320 && command.quietEndMin == 420);
  const uint8_t invalidQuiet[] = {6, 1, 0xa0, 0x05, 0xa4, 0x01}; // 1440–420
  assert(!PhoneCompanionCommand::decode(invalidQuiet, sizeof(invalidQuiet), command));
  const uint8_t trailing[] = {3, 0};
  assert(!PhoneCompanionCommand::decode(trailing, sizeof(trailing), command));
}
