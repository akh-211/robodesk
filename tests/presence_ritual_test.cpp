#include "../PresenceRitual.h"
#include <cassert>

int main() {
  companion::PresenceRitual ritual;
  ritual.reset(0, false);
  assert(ritual.update(true, 100, true) == companion::PresenceEvent::None);
  assert(ritual.update(false, 200, true) == companion::PresenceEvent::None);
  assert(ritual.update(true, 300, true) == companion::PresenceEvent::None);
  assert(ritual.update(true, 549, true) == companion::PresenceEvent::None);
  assert(ritual.update(true, 550, true) == companion::PresenceEvent::Arrived);
  assert(ritual.present());
  assert(ritual.pending(false, 551) == companion::PresenceEvent::None);
  assert(ritual.pending(true, 552) == companion::PresenceEvent::Arrived); // Busy/opt-out did not consume it.
  ritual.acknowledge(552);
  assert(ritual.pending(true, 553) == companion::PresenceEvent::None);
  assert(ritual.update(false, 600, true) == companion::PresenceEvent::None);
  assert(ritual.present());
  assert(ritual.update(false, 2599, true) == companion::PresenceEvent::None);
  assert(ritual.update(false, 2600, true) == companion::PresenceEvent::None);
  assert(!ritual.present());
  assert(ritual.update(true, 1800550, false) == companion::PresenceEvent::None);
  assert(ritual.update(true, 1800800, true) == companion::PresenceEvent::Returned);
  assert(ritual.update(false, 1810000, true) == companion::PresenceEvent::None);
  assert(ritual.update(false, 1812000, true) == companion::PresenceEvent::None);
  assert(ritual.update(true, 3600800, true) == companion::PresenceEvent::None);
  assert(ritual.update(true, 3601050, true) == companion::PresenceEvent::Returned);
  assert(ritual.update(true, 3601051, true) == companion::PresenceEvent::Returned);
  ritual.acknowledge(3601051);
  assert(ritual.update(true, 3601052, true) == companion::PresenceEvent::None);

  companion::PresenceRitual wrap;
  const uint32_t start = UINT32_MAX - 100;
  wrap.reset(start, false);
  assert(wrap.update(true, start + 250u, true) == companion::PresenceEvent::None);
  assert(wrap.update(true, start + 500u, true) == companion::PresenceEvent::Arrived);
  assert(wrap.pending(true, start + 501u) == companion::PresenceEvent::Arrived);
  assert(wrap.pending(true, start + 5501u) == companion::PresenceEvent::None); // Expired unqueued greeting.
  return 0;
}
