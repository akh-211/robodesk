#include "../PreferenceLearner.h"
#include <cassert>
#include <cstring>

struct FakePreferences {
  uint8_t payload[companion::PreferenceLearner::PayloadSize]{};
  size_t size = 0;
  bool failReadBegin = false, failWriteBegin = false, failWrite = false;
  bool begin(const char*, bool readOnly) { return !(readOnly ? failReadBegin : failWriteBegin); }
  size_t getBytesLength(const char*) const { return size; }
  size_t getBytes(const char*, void* out, size_t length) {
    if (size != length) return 0;
    memcpy(out, payload, length);
    return length;
  }
  size_t putBytes(const char*, const void* in, size_t length) {
    if (failWrite) return 0;
    memcpy(payload, in, length);
    size = length;
    return length;
  }
  void end() {}
  bool clear() { size = 0; memset(payload, 0, sizeof(payload)); return true; }
};

int main() {
  companion::PreferenceLearner learner;
  assert(!learner.enabled());
  assert(!learner.recordFeedback(6, true, 800));
  assert(learner.setEnabled(true));
  assert(learner.dirty());
  assert(learner.recordFeedback(6, true, 800));
  assert(learner.recordFeedback(6, false, 800));
  assert(learner.score(6) == 0);
  assert(learner.scheduleProbability(6, learner.timeBucket(4)) == 0);
  assert(learner.favorites(6) == 1 && learner.skips(6) == 1);
  assert(learner.timeBucket(4) == 2);
  assert(!learner.recordFeedback(0, true, 800));
  assert(!learner.recordFeedback(10, true, 800));
  assert(!learner.recordFeedback(6, true, 1440));
  assert(learner.recordFeedback(6, true, 1440, false));
  assert(learner.recordFeedback(6, true, 800));
  assert(learner.scheduleProbability(6, learner.timeBucket(4)) == 2);
  assert(learner.scheduleProbability(6, 2) == 0); // No routine bias before repeated evidence in this time bucket.
  assert(learner.timeBucket(4) == 3); // Invalid clock never adds routine evidence.

  uint8_t payload[companion::PreferenceLearner::PayloadSize]{};
  assert(!learner.encode(payload, sizeof(payload) - 1));
  assert(learner.encode(payload, sizeof(payload)));
  companion::PreferenceLearner restored;
  assert(restored.restore(payload, sizeof(payload)));
  assert(restored.enabled() && !restored.dirty());
  assert(restored.favorites(6) == 3 && restored.skips(6) == 1);
  assert(restored.timeBucket(4) == 3);

  uint8_t corrupt[companion::PreferenceLearner::PayloadSize];
  memcpy(corrupt, payload, sizeof(corrupt));
  corrupt[10] ^= 1;
  assert(!restored.restore(corrupt, sizeof(corrupt)));
  corrupt[10] = payload[10];
  corrupt[4] = 2;
  assert(!restored.restore(corrupt, sizeof(corrupt)));
  assert(restored.favorites(6) == 3); // Failed restore is atomic.

  for (unsigned i = 1; i < 20; ++i) learner.recordFeedback(6, true, 800);
  assert(learner.favorites(6) == companion::PreferenceLearner::MaxEvidence);
  assert(learner.timeBucket(4) == companion::PreferenceLearner::MaxEvidence);
  assert(learner.setEnabled(false));
  assert(!learner.setEnabled(false)); // no duplicate transition
  learner.markSaved();
  assert(!learner.dirty());
  learner.resetLearned();
  assert(!learner.enabled() && learner.score(6) == 0 && learner.timeBucket(4) == 0);
  assert(learner.dirty());

  FakePreferences nvs;
  companion::PreferenceLearnerStore<FakePreferences> store(nvs);
  assert(store.save(learner));
  assert(!learner.dirty() && nvs.size == companion::PreferenceLearner::PayloadSize);
  companion::PreferenceLearner fromNvs;
  assert(store.load(fromNvs) && !fromNvs.enabled());
  assert(fromNvs.score(6) == 0 && fromNvs.timeBucket(4) == 0);
  assert(fromNvs.setEnabled(true));
  nvs.failWrite = true;
  assert(!store.save(fromNvs) && fromNvs.dirty());
  nvs.failWrite = false;
  assert(store.clear() && nvs.size == 0);
  return 0;
}
