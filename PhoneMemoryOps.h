#pragma once
#include "RoboBrain.h"
#include "PhoneSettingsProtocol.h"

// S3 side of the BLE memory/reminder kinds (docs/mobile/BLE_SETTINGS_PROTOCOL.md section 14).
// Every mutation goes through the same RoboBrain paths as the dashboard and Gemini tools:
// manualRemember / forget / clearMemory (memory) and executeTool create_reminder /
// cancel_reminder (reminders), so the privacy filter, slot quotas, key de-duplication and
// persistence are shared. Text from the phone is untrusted data; it is validated by
// PhoneSettingsProtocol::decode* before it reaches this file and is never interpreted.
namespace PhoneMemoryOps {
namespace ps = PhoneSettingsProtocol;

inline const companion::Fact* nextFact(const companion::Memory& m, uint32_t after) {
  const companion::Fact* best = nullptr;
  for (const auto& f : m.facts) if (f.id > after && (!best || f.id < best->id)) best = &f;
  return best;
}
inline size_t encodeFact(const companion::Fact& f, uint8_t* out) {
  const uint8_t flags = uint8_t((f.pinned ? 1 : 0) | (unsigned(f.source) << 1));
  return ps::encodeMemoryEntry(out, f.id, f.timestamp, f.createdDay, uint8_t(f.kind), flags, f.key, f.value);
}
// CRC-32 over all facts in ascending id order, independent of how the list is windowed.
inline uint32_t memoryRevision(const companion::Memory& m) {
  uint32_t c = ~0u; uint8_t entry[ps::MemoryEntryMax]; uint32_t after = 0;
  while (const companion::Fact* f = nextFact(m, after)) { c = ps::crc32Update(c, entry, encodeFact(*f, entry)); after = f->id; }
  return ~c;
}
// Window of whole entries with id > after that fits DocMax; `more` = 1 when entries remain.
inline size_t buildMemoryDoc(const companion::Memory& m, uint32_t after, uint8_t* doc) {
  size_t used = ps::MemoryListHeader; uint8_t count = 0, more = 0; uint8_t entry[ps::MemoryEntryMax];
  while (const companion::Fact* f = nextFact(m, after)) {
    const size_t n = encodeFact(*f, entry);
    if (used + n > ps::DocMax) { more = 1; break; }
    memcpy(doc + used, entry, n); used += n; ++count; after = f->id;
  }
  doc[0] = ps::ListSchema; ps::put32(doc + 1, memoryRevision(m)); doc[5] = uint8_t(m.count()); doc[6] = count; doc[7] = more;
  return used;
}

inline const companion::Reminder* nextReminder(const companion::State& s, uint32_t after) {
  const companion::Reminder* best = nullptr;
  for (const auto& r : s.reminders) if (r.id > after && (!best || r.id < best->id)) best = &r;
  return best;
}
// Timer due is a millis() deadline; it is reported as remaining seconds and left out of the
// revision (stable = true) so an unchanged list keeps its revision while a timer runs.
inline size_t encodeReminder(const companion::Reminder& r, uint32_t now, bool stable, uint8_t* out) {
  uint32_t due = r.due;
  if (r.schedule == companion::Schedule::Daily) due = 0;
  else if (r.schedule == companion::Schedule::Timer) { const int32_t left = int32_t(r.due - now); due = stable || left <= 0 ? 0 : uint32_t(left + 999) / 1000u; }
  return ps::encodeReminderEntry(out, r.id, uint8_t(r.schedule), r.minute, due, r.text);
}
inline uint32_t reminderRevision(const companion::State& s) {
  uint32_t c = ~0u; uint8_t entry[ps::ReminderEntryFixed + ps::ReminderTextMax]; uint32_t after = 0;
  while (const companion::Reminder* r = nextReminder(s, after)) { c = ps::crc32Update(c, entry, encodeReminder(*r, 0, true, entry)); after = r->id; }
  return ~c;
}
inline size_t buildReminderDoc(const companion::State& s, uint32_t now, uint8_t* doc) {
  size_t used = ps::ReminderListHeader; uint8_t count = 0; uint32_t after = 0;
  while (const companion::Reminder* r = nextReminder(s, after)) { used += encodeReminder(*r, now, false, doc + used); ++count; after = r->id; }
  doc[0] = ps::ListSchema; ps::put32(doc + 1, reminderRevision(s)); doc[5] = count;
  return used;
}
inline bool remindersFull(const companion::State& s) { for (const auto& r : s.reminders) if (!r.id) return false; return true; }

inline size_t revisionBody(uint32_t id, uint32_t revision, bool withId, uint8_t* out) {
  size_t n = 0; if (withId) { ps::put32(out, id); n = 4; } ps::put32(out + n, revision); return n + 4;
}

// op = PhoneSettingsProtocol::Link* (5..11). p/n = the BLE payload (session already stripped).
// doc: DocMax+1 scratch for list ops (nullptr -> NoMemory). out/outCap: reply body area.
inline uint8_t handle(RoboBrain& brain, uint8_t op, uint32_t session, const uint8_t* p, size_t n, uint32_t now,
                      uint8_t* doc, uint8_t* out, size_t outCap, size_t& body) {
  body = 0;
  auto& memory = brain.memory();
  auto& state = brain.companionState();
  uint8_t status = ps::Unsupported;
  if (op == ps::LinkMemoryList || op == ps::LinkReminderList) {
    const bool memoryList = op == ps::LinkMemoryList;
    if (n != (memoryList ? 5u : 1u)) return ps::BadRequest;
    if (!doc) return ps::NoMemory;
    const size_t length = memoryList ? buildMemoryDoc(memory, ps::get32(p), doc) : buildReminderDoc(state, now, doc);
    body = ps::encodePage(doc, length, p[memoryList ? 4 : 0], out, outCap, &status);
    return status;
  }
  if (op == ps::LinkMemoryAdd) {
    ps::MemoryAdd add;
    if (!ps::decodeMemoryAdd(p, n, add)) return ps::BadRequest;
    char key[ps::MemoryKeyMax + 1], value[ps::MemoryValueMax + 1];
    memcpy(key, add.key, add.keyLength); key[add.keyLength] = 0; memcpy(value, add.value, add.valueLength); value[add.valueLength] = 0;
    if (!brain.memoryEnabled()) return ps::Disabled;
    if (companion::sensitive(key) || companion::sensitive(value)) return ps::ContentRejected;
    if (memory.findKey(key) < 0 && memory.count() >= companion::Memory::Capacity) return ps::Full;
    // A failed save must not leave the phone-visible state changed: restore the prior fact (or empty slot).
    const int prior = memory.findKey(key); const companion::Fact priorFact = prior >= 0 ? memory.facts[prior] : companion::Fact();
    if (!brain.manualRemember(key, value, livingeyes::SemanticMemory::kindName(livingeyes::SemanticMemoryKind(add.kind)), now)) {
      const int slot = memory.findKey(key); if (slot >= 0) memory.facts[slot] = prior >= 0 ? priorFact : companion::Fact();
      return ps::StorageFailed;
    }
    const int slot = memory.findKey(key);
    body = revisionBody(slot >= 0 ? memory.facts[slot].id : 0, memoryRevision(memory), true, out);
    return ps::Ok;
  }
  if (op == ps::LinkMemoryDelete) {
    if (n != 4 || !ps::get32(p)) return ps::BadRequest;
    int slot = -1;
    for (unsigned i = 0; i < companion::Memory::Capacity; ++i) if (memory.facts[i].id == ps::get32(p)) { slot = int(i); break; }
    if (slot < 0) return ps::NotFound;
    const companion::Fact doomed = memory.facts[slot]; char key[sizeof(doomed.key)]; memcpy(key, doomed.key, sizeof(key));
    if (!brain.forget(key, now)) { memory.facts[slot] = doomed; return ps::StorageFailed; }  // keep RAM and flash consistent
    body = revisionBody(0, memoryRevision(memory), false, out);
    return ps::Ok;
  }
  if (op == ps::LinkMemoryClear) {
    const uint8_t token = ps::decodeMemoryClear(p, n, session);
    if (token != ps::Ok) return token;
    if (!brain.clearMemory(now)) return ps::StorageFailed;
    body = revisionBody(0, memoryRevision(memory), false, out);
    return ps::Ok;
  }
  if (op == ps::LinkReminderAdd) {
    ps::ReminderAdd add;
    if (!ps::decodeReminderAdd(p, n, add)) return ps::BadRequest;
    char text[ps::ReminderTextMax + 1];
    memcpy(text, add.text, add.textLength); text[add.textLength] = 0;
    if (companion::sensitive(text)) return ps::ContentRejected;
    if (remindersFull(state)) return ps::Full;
    if (add.schedule != ps::ScheduleTimer && brain.epoch() < 1700000000u) return ps::TimeNotSet;
    JsonDocument args; args["text"] = text;
    if (add.schedule == ps::ScheduleTimer) { args["kind"] = "timer"; args["seconds"] = add.value; }
    else if (add.schedule == ps::ScheduleOnce) { args["kind"] = "once"; args["epoch"] = add.value; }
    else { args["kind"] = "daily"; args["minute"] = add.minute; }
    char json[320], result[96]; serializeJson(args, json, sizeof(json));
    if (!brain.executeTool("create_reminder", json, 0, result, sizeof(result))) return ps::Unsupported;
    JsonDocument reply;
    if (deserializeJson(reply, result)) return ps::StorageFailed;
    const char* st = reply["status"] | "";
    if (!strcmp(st, "completed")) { body = revisionBody(reply["id"] | 0u, reminderRevision(state), true, out); return ps::Ok; }
    return !strcmp(st, "storage_failed") ? ps::StorageFailed : ps::BadRequest;  // past once-time or rejected value
  }
  if (op == ps::LinkReminderDelete) {
    if (n != 4 || !ps::get32(p)) return ps::BadRequest;
    JsonDocument args; args["id"] = ps::get32(p);
    char json[48], result[64]; serializeJson(args, json, sizeof(json));
    if (!brain.executeTool("cancel_reminder", json, 0, result, sizeof(result))) return ps::Unsupported;
    JsonDocument reply;
    if (deserializeJson(reply, result)) return ps::StorageFailed;
    const char* st = reply["status"] | "";
    if (!strcmp(st, "completed")) { body = revisionBody(0, reminderRevision(state), false, out); return ps::Ok; }
    return !strcmp(st, "not_found") ? ps::NotFound : ps::StorageFailed;
  }
  return status;
}
}  // namespace PhoneMemoryOps
