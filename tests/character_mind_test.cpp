#include <cassert>
#include <cstring>
#include <livingeyes/CharacterMind.h>

static bool bounded(float value) { return value >= 0.0f && value <= 1.0f; }

int main() {
  livingeyes::CharacterMind mind;
  mind.reset(1000);
  assert(!strcmp(mind.moodName(), "calm"));
  assert(!strcmp(mind.moodCauseName(), "none"));
  const auto defaults = mind.state();
  assert(bounded(defaults.energy) && bounded(defaults.curiosity));
  assert(bounded(defaults.affection) && bounded(defaults.boredom));
  assert(bounded(defaults.valence) && bounded(defaults.socialNeed) && bounded(defaults.trust));

  mind.eventAt(livingeyes::MindEvent::Presence, 1.0f, 2000);
  assert(mind.moodOverlayActive(2001));
  assert(!strcmp(mind.moodCauseName(), "presence"));
  assert(mind.state().interactions == 1);
  mind.tick(3800, true);
  assert(!mind.moodOverlayActive(3800));
  assert(!strcmp(mind.moodCauseName(), "none"));

  for (uint32_t i = 0; i < 5; ++i) mind.eventAt(livingeyes::MindEvent::Touch, 1.0f, 5000 + i * 100);
  assert(mind.moodOverlayActive(5401));
  assert(!strcmp(mind.moodName(), "sulking"));
  assert(!strcmp(mind.moodCauseName(), "sulking"));
  mind.tick(50401);
  assert(!mind.moodOverlayActive(50401));
  assert(!strcmp(mind.moodCauseName(), "none"));

  livingeyes::CharacterMindState restored;
  restored.energy = -5.0f;
  restored.curiosity = 8.0f;
  restored.affection = 0.3f;
  restored.boredom = 2.0f;
  restored.valence = -1.0f;
  restored.socialNeed = 3.0f;
  restored.trust = 0.6f;
  mind.restore(restored, 60000);
  const auto safe = mind.state();
  assert(safe.energy == 0.0f && safe.curiosity == 1.0f);
  assert(safe.affection == 0.3f && safe.boredom == 1.0f);
  assert(safe.valence == 0.0f && safe.socialNeed == 1.0f && safe.trust == 0.6f);
  assert(!mind.moodOverlayActive(60001));

  livingeyes::CharacterMind wrap;
  const uint32_t start = UINT32_MAX - 1000u;
  wrap.reset(start);
  wrap.eventAt(livingeyes::MindEvent::Wake, 1.0f, start + 10u);
  assert(wrap.moodOverlayActive(start + 100u));
  assert(!wrap.moodOverlayActive(start + 1700u));
  return 0;
}
