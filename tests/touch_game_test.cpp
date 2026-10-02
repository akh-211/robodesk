#include "../TouchGame.h"
#include <cassert>
#include <cstring>
#include <cstdint>

int main() {
  companion::TouchGame wired;
  assert(!companion::touchGameTouchGate(true, true, false));
  assert(companion::touchGameTouchGate(true, false, false));
  assert(wired.start(50, companion::touchGameTouchGate(true, false, false)));
  // Firmware calls update after debounced press; an in-progress session must
  // allow that held input through until release feeds its duration.
  assert(companion::touchGameTouchGate(true, true, wired.active()));
  assert(wired.update(100, companion::touchGameTouchGate(true, true, wired.active())) == companion::TouchGameState::Active);
  assert(wired.press(120, 200, companion::touchGameTouchGate(true, false, wired.active())) == companion::TouchGameState::Active);
  assert(wired.step() == 1);
  assert(!companion::touchGameTouchGate(false, true, wired.active()));
  assert(wired.update(250, companion::touchGameTouchGate(false, true, wired.active())) == companion::TouchGameState::Cancelled);

  companion::TouchGame game;
  assert(!game.start(100, false));
  assert(game.start(100, true));
  assert(!game.start(101, true));
  assert(game.press(120, 300, true) == companion::TouchGameState::Active);
  assert(game.step() == 1);
  assert(game.press(800, 1400, true) == companion::TouchGameState::Active);
  assert(game.step() == 2);
  assert(game.press(150, 1700, true) == companion::TouchGameState::Completed);
  assert(std::strcmp(companion::touchGameStateName(game.state()), "completed") == 0);
  assert(game.start(2000, true));
  assert(game.press(800, 2100, true) == companion::TouchGameState::Failed);

  assert(game.start(3000, true));
  assert(game.update(3100, false) == companion::TouchGameState::Cancelled);
  assert(game.start(4000, true));
  assert(game.update(4000 + companion::TouchGame::SessionLimitMs, true) == companion::TouchGameState::TimedOut);

  assert(game.start(UINT32_MAX - 1000, true));
  assert(game.update(2000, true) == companion::TouchGameState::Active);
  assert(game.update(uint32_t(UINT32_MAX - 1000 + companion::TouchGame::SessionLimitMs), true) == companion::TouchGameState::TimedOut);
  return 0;
}
