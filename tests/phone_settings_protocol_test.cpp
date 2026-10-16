#include "PhoneBridgeProtocol.h"
#include "PhoneSettingsProtocol.h"
#include "RoboBehaviorWire.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ps = PhoneSettingsProtocol;
using Bytes = std::vector<uint8_t>;
using Section = std::map<std::string, std::string>;

static Bytes hex(const std::string& s) {
  assert(s.size() % 2 == 0);
  Bytes out;
  for (size_t i = 0; i < s.size(); i += 2) out.push_back(uint8_t(std::stoul(s.substr(i, 2), nullptr, 16)));
  return out;
}
static std::map<std::string, Section> loadFixture() {
  std::ifstream in("tests/fixtures/ble_settings_v1.txt");
  assert(in && "run from the repository root");
  std::map<std::string, Section> out; std::string line, current;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '[') { current = line.substr(1, line.size() - 2); out[current]; continue; }
    const size_t eq = line.find('='); assert(eq != std::string::npos);
    out[current][line.substr(0, eq)] = line.substr(eq + 1);
  }
  return out;
}
static Bytes fixtureDoc() {
  std::string doc = "{\"schema\":1,\"robotName\":\"" + std::string(620, 'R') + "\"}";
  return Bytes(doc.begin(), doc.end());
}
static Bytes pageOf(const Bytes& doc, uint8_t page) {
  Bytes out(ps::PageHeader + ps::PageData); uint8_t status = 0xff;
  const size_t n = ps::encodePage(doc.data(), doc.size(), page, out.data(), out.size(), &status);
  assert(n && status == ps::Ok); out.resize(n); return out;
}
static Bytes response(uint32_t counter, uint8_t kind, uint8_t status, const Bytes& body = {}) {
  Bytes out(ps::ResponseHeader); ps::writeResponseHeader(out.data(), counter, kind, status);
  out.insert(out.end(), body.begin(), body.end()); return out;
}
static bool contains(const uint8_t* p, size_t n, const char* needle) {
  const size_t k = strlen(needle);
  for (size_t i = 0; i + k <= n; ++i) if (!memcmp(p + i, needle, k)) return true;
  return false;
}
struct TagFixture { Bytes wire; };
static bool verifyTag(const uint8_t* prefix, size_t size, const uint8_t* mac, size_t macSize, void* context) {
  const auto* f = static_cast<const TagFixture*>(context);
  return macSize == 32 && size + 32 == f->wire.size() && !memcmp(prefix, f->wire.data(), size) && !memcmp(mac, f->wire.data() + size, 32);
}
static bool zeroAge(uint8_t kind, uint32_t* age, void*) { if (!ps::isRequestKind(kind)) return false; *age = 0; return true; }

int main() {
  const auto fixture = loadFixture();
  const Bytes doc = fixtureDoc();
  assert(doc.size() == 647 && ps::pageCount(doc.size()) == 2 && ps::MaxPages == 9);

  // 1. Every fixture envelope has the canonical v2 header around its payload;
  //    every phone request passes the firmware pre-validation unchanged.
  for (const auto& [name, f] : fixture) {
    const Bytes payload = hex(f.at("payload")), wire = hex(f.at("envelope"));
    const uint8_t direction = uint8_t(std::stoul(f.at("direction"))), kind = uint8_t(std::stoul(f.at("kind")));
    const uint32_t counter = uint32_t(std::stoul(f.at("counter"))), session = uint32_t(std::stoul(f.at("session"), nullptr, 16));
    assert(wire.size() == PhoneBridgeProtocol::HeaderSize + payload.size() + PhoneBridgeProtocol::MacSize);
    assert(payload.size() <= PhoneBridgeProtocol::MaxPayload && wire.size() <= PhoneBridgeProtocol::MaxMessage);
    assert(!memcmp(wire.data(), "RDB2", 4) && wire[4] == 2 && wire[5] == direction && wire[6] == kind && wire[7] == 0);
    assert(PhoneBridgeProtocol::read32le(wire.data() + 8) == session && PhoneBridgeProtocol::read32le(wire.data() + 12) == counter);
    assert(PhoneBridgeProtocol::read32le(wire.data() + 16) == 0 && PhoneBridgeProtocol::read16le(wire.data() + 20) == payload.size());
    assert(!memcmp(wire.data() + 22, payload.data(), payload.size()));
    if (direction == PhoneBridgeProtocol::PhoneToRobot) {
      assert(ps::isRequestKind(kind) && ps::validateRequest(kind, payload.data(), payload.size(), session) == ps::Ok);
    } else {
      assert(kind == ps::KindResponse && payload.size() >= ps::ResponseHeader);
    }
  }
  std::puts("PASS: BLE settings fixture envelopes, limits and request validation");

  // 2. The C++ encoders produce the fixture payloads byte-for-byte.
  assert(hex(fixture.at("settings_set_page0").at("payload")) == pageOf(doc, 0));
  assert(hex(fixture.at("settings_set_page1").at("payload")) == pageOf(doc, 1));
  assert(hex(fixture.at("response_get_page0").at("payload")) == response(1, ps::KindSettingsGet, ps::Ok, pageOf(doc, 0)));
  assert(hex(fixture.at("response_set_page1_applied").at("payload")) == response(3, ps::KindSettingsSet, ps::Ok, {1, 1}));
  assert(hex(fixture.at("response_secret_ok").at("payload")) == response(4, ps::KindSecretSet, ps::Ok));
  assert(hex(fixture.at("response_ota_accepted").at("payload")) == response(11, ps::KindCommand, ps::Ok));
  assert(hex(fixture.at("response_ota_update_busy").at("payload")) == response(11, ps::KindCommand, ps::UpdateBusy));
  {
    ps::StatusSnapshot st;
    st.c3Version = 20261009; st.s3Version = 20261008; st.c3UptimeMs = 123456; st.s3UptimeMs = 120000;
    st.c3HeapFree = 48000; st.c3HeapMinFree = 30000; st.c3HeapLargest = 24000; st.s3InternalFree = 200000; st.s3InternalMinFree = 150000;
    st.linkFlags = 3; st.linkAgeMs = 12; st.linkRetries = 4; st.linkCrcErrors = 1; st.linkDisconnects = 2;
    st.wifiState = 1; st.wifiRssi = -61; st.bleFlags = 0x0b; st.configFlags = 0x03;
    st.otaState = ps::OtaFlashingS3; st.otaError = ps::OtaErrNone; st.otaPercent = 42; st.otaTargetVersion = 20261010;
    st.wifiActiveSlot = 2; st.pomodoroState = 1; st.pomodoroRemainingSec = 1234; st.activityId = 5; st.activityState = 1;
    st.gameState = 1; st.learningEnabled = 1; st.irProfileCount = 2; st.irLearnState = 1; st.integrationBusy = 0;
    st.moodId = ps::moodId("sleepy"); st.moodCause = 9; st.sleepState = 2;
    assert(st.moodId == 7 && ps::moodId("calm") == 1 && ps::moodId("sulking") == 10 && ps::moodId("bogus") == 0 && ps::moodId(nullptr) == 0);
    assert(ps::StatusSize == 79 && ps::StatusSizeV3 == 76 && ps::StatusSizeV2 == 65 && ps::StatusSchema == 4 && ps::PageHeader + ps::StatusSize <= ps::PageData);
    assert(ps::LinkInfoSizeV2 == 25 && ps::LinkInfoSizeV3 == 28 && ps::ResponseHeader + ps::LinkInfoSizeV3 <= 512);
    Bytes body(ps::PageHeader + ps::StatusSize);
    assert(ps::encodeStatusPage(st, body.data(), body.size()) == body.size());
    assert(hex(fixture.at("response_status").at("payload")) == response(8, ps::KindStatusSnapshot, ps::Ok, body));
    ps::PageView v; assert(ps::decodePage(body.data(), body.size(), v) && v.count == 1);
    ps::StatusSnapshot back; assert(ps::decodeStatus(v.data, v.dataSize, back) && back.wifiRssi == -61 && back.s3InternalMinFree == 150000);
    assert(back.otaState == ps::OtaFlashingS3 && back.otaPercent == 42 && back.otaTargetVersion == 20261010 && back.otaError == 0);
    assert(back.wifiActiveSlot == 2 && back.pomodoroState == 1 && back.pomodoroRemainingSec == 1234 && back.activityId == 5 && back.activityState == 1);
    assert(back.gameState == 1 && back.learningEnabled == 1 && back.irProfileCount == 2 && back.irLearnState == 1 && back.integrationBusy == 0);
    assert(back.moodId == 7 && back.moodCause == 9 && back.sleepState == 2);
    // Schema 3 (76 B) decodes with the mood fields zero; a schema-4 document shorter than 79 B is rejected.
    const Bytes v3 = hex(fixture.at("response_status_schema3").at("payload"));
    ps::PageView p3; assert(ps::decodePage(v3.data() + ps::ResponseHeader, v3.size() - ps::ResponseHeader, p3) && p3.dataSize == ps::StatusSizeV3 && p3.data[0] == 3);
    ps::StatusSnapshot s3; s3.moodId = 5; assert(ps::decodeStatus(p3.data, p3.dataSize, s3) && s3.moodId == 0 && s3.sleepState == 0 && s3.irLearnState == 1);
    Bytes asV4(v.data, v.data + ps::StatusSizeV3); asV4[0] = 4; assert(!ps::decodeStatus(asV4.data(), asV4.size(), s3));
    assert(!memcmp(v.data + 1, p3.data + 1, ps::StatusSizeV3 - 1));
    // Schema 2 (65 B) decodes with the schema-3 fields zero; a schema-3 document shorter than 76 B is rejected.
    const Bytes v2 = hex(fixture.at("response_status_schema2").at("payload"));
    ps::PageView p2; assert(ps::decodePage(v2.data() + ps::ResponseHeader, v2.size() - ps::ResponseHeader, p2) && p2.dataSize == ps::StatusSizeV2 && p2.data[0] == 2);
    ps::StatusSnapshot mid; mid.wifiActiveSlot = 9; assert(ps::decodeStatus(p2.data, p2.dataSize, mid) && mid.otaPercent == 42 && mid.wifiActiveSlot == 0 && mid.pomodoroRemainingSec == 0 && mid.integrationBusy == 0);
    assert(!ps::decodeStatus(p2.data, p2.dataSize - 1, mid));
    Bytes asV3(v.data, v.data + ps::StatusSizeV2); asV3[0] = 3; assert(!ps::decodeStatus(asV3.data(), asV3.size(), mid));
    // A schema-2 reader (n >= 65, ignores the tail) sees the same first 65 bytes of the schema-3 document.
    assert(!memcmp(v.data + 1, p2.data + 1, ps::StatusSizeV2 - 1));
    // The LinkInfo tail round-trips.
    uint8_t liveWire[ps::LinkLiveSize]; ps::putLive(liveWire, st); ps::StatusSnapshot liveBack; ps::getLive(liveWire, liveBack);
    assert(liveBack.pomodoroRemainingSec == 1234 && liveBack.irLearnState == 1 && liveBack.activityId == 5 && liveBack.gameState == 1);
    uint8_t moodWire[ps::LinkMoodSize]; ps::putMood(moodWire, st); ps::getMood(moodWire, liveBack);
    assert(liveBack.moodId == 7 && liveBack.moodCause == 9 && liveBack.sleepState == 2);
    // Schema 1 (58 B) still decodes with OTA fields zero; schema 2 accepts unknown trailing bytes, not a short document.
    const Bytes v1 = hex(fixture.at("response_status_schema1").at("payload"));
    ps::PageView p1; assert(ps::decodePage(v1.data() + ps::ResponseHeader, v1.size() - ps::ResponseHeader, p1) && p1.dataSize == ps::StatusSizeV1);
    ps::StatusSnapshot old; old.otaState = 9; assert(ps::decodeStatus(p1.data, p1.dataSize, old) && old.otaState == ps::OtaIdle && old.c3Version == 20261009);
    Bytes longer(v.data, v.data + v.dataSize); longer.push_back(0xee);
    assert(ps::decodeStatus(longer.data(), longer.size(), back) && back.otaTargetVersion == 20261010);
    assert(!ps::decodeStatus(v.data, ps::StatusSize - 1, back) && !ps::decodeStatus(p1.data, p1.dataSize + 1, back));
    assert(hex(fixture.at("response_command_rejected").at("payload")) == response(27, ps::KindCommand, ps::Rejected) && ps::Rejected == 23);
    assert(hex(fixture.at("response_command_robot_unavailable").at("payload")) == response(27, ps::KindCommand, ps::RobotUnavailable));
    assert(hex(fixture.at("response_command_bad_request").at("payload")) == response(27, ps::KindCommand, ps::BadRequest));
    st.otaPercent = 250; Bytes clamp(ps::StatusSize); assert(ps::encodeStatus(st, clamp.data(), clamp.size()) && clamp[60] == 100);
  }
  uint8_t token[8]; ps::factoryToken(0x78563412u, token);
  assert(!memcmp(hex(fixture.at("command_factory_reset").at("payload")).data() + 1, token, 8));
  std::puts("PASS: page, response and status encoders reproduce the shared hex vectors");

  // 2b. ota-start: Ok only schedules; the flow starts after the response left the slot.
  {
    ps::OtaStartGate g; g.admitted = true; g.linkConnected = true; g.wifiConnected = true; g.clockSet = true;
    ps::OtaStartGate anon = g; anon.admitted = false;
    ps::OtaStartScheduler s;
    assert(s.request(anon, 100) == ps::NotAuthorized && !s.pending());
    ps::OtaStartGate busy = g; busy.updateBusy = true; assert(s.request(busy, 100) == ps::UpdateBusy && !s.pending());
    ps::OtaStartGate link = g; link.linkConnected = false; assert(s.request(link, 100) == ps::RobotUnavailable);
    ps::OtaStartGate wifi = g; wifi.wifiConnected = false; assert(s.request(wifi, 100) == ps::NetworkUnavailable);
    ps::OtaStartGate clock = g; clock.clockSet = false; assert(s.request(clock, 100) == ps::TimeNotSet && !s.pending());
    assert(s.request(g, 1000) == ps::Ok && s.pending());
    assert(s.request(g, 1001) == ps::UpdateBusy);                   // already in progress
    assert(!s.due(1000, true) && !s.due(1000 + ps::OtaStartDelayMs - 1, true));
    assert(!s.due(1000 + ps::OtaStartDelayMs, false));             // response not yet sent
    assert(s.due(1000 + ps::OtaStartDelayMs, true) && !s.pending() && !s.due(5000, true));
    assert(s.request(g, 0xfffffff0u) == ps::Ok && !s.due(0xfffffff0u + 100, true));
    assert(s.due(0xfffffff0u + ps::OtaStartMaxWaitMs, false));     // slot never drained: start anyway (wraps)
    assert(ps::otaErrorCode("none") == ps::OtaErrNone && ps::otaErrorCode(nullptr) == ps::OtaErrNone);
    assert(ps::otaErrorCode("signature") == ps::OtaErrSignature && ps::otaErrorCode("tls") == ps::OtaErrNetwork);
    assert(ps::otaErrorCode("release_pair_mismatch") == ps::OtaErrPairMismatch && ps::otaErrorCode("robot_boot_not_confirmed") == ps::OtaErrRobotBoot);
    assert(ps::otaErrorCode("image_hash") == ps::OtaErrImage && ps::otaErrorCode("remote_commit") == ps::OtaErrRobotRejected);
    assert(ps::otaErrorCode("something_new") == ps::OtaErrOther);
    uint8_t cmd[1] = {ps::CommandOtaStart}, two[2] = {ps::CommandOtaStart, 1}; ps::Command c;
    assert(ps::decodeCommand(cmd, 1, 1, c) == ps::Ok && c.op == ps::CommandOtaStart && ps::decodeCommand(two, 2, 1, c) == ps::BadRequest);
  }
  std::puts("PASS: ota-start is deferred after the response, gated on auth/busy/link/Wi-Fi/clock");

  // 3. Paging and CRC: ordered pages with identical headers, verified length + CRC32.
  std::vector<uint8_t> stage(ps::DocMax);
  ps::PageStager stager; stager.attach(stage.data(), stage.size());
  const Bytes p0 = pageOf(doc, 0), p1 = pageOf(doc, 1);
  bool complete = false;
  assert(stager.accept(7, p0.data(), p0.size(), 100, complete) == ps::Ok && !complete && !stager.document());
  assert(stager.accept(7, p1.data(), p1.size(), 200, complete) == ps::Ok && complete);
  assert(stager.size() == doc.size() && !memcmp(stager.document(), doc.data(), doc.size()));
  stager.reset(); assert(!stager.document() && stage[0] == 0);
  assert(stager.accept(7, p1.data(), p1.size(), 300, complete) == ps::SequenceError);           // page 1 first
  assert(stager.accept(7, p0.data(), p0.size(), 300, complete) == ps::Ok);
  assert(stager.accept(8, p1.data(), p1.size(), 300, complete) == ps::SequenceError && !stager.active());  // other session
  assert(stager.accept(7, p0.data(), p0.size(), 400, complete) == ps::Ok);
  assert(stager.accept(7, p1.data(), p1.size(), 400 + ps::StageTimeoutMs + 1, complete) == ps::SequenceError);  // stale stage
  Bytes badCrc = p1; badCrc[4] ^= 1;
  assert(stager.accept(7, p0.data(), p0.size(), 500, complete) == ps::Ok);
  assert(stager.accept(7, badCrc.data(), badCrc.size(), 500, complete) == ps::SequenceError);  // header must match page 0
  Bytes corrupt0 = p0; corrupt0[ps::PageHeader] ^= 0x20;  // data corrupted, header CRC kept
  assert(stager.accept(7, corrupt0.data(), corrupt0.size(), 600, complete) == ps::Ok);
  assert(stager.accept(7, p1.data(), p1.size(), 600, complete) == ps::CrcMismatch && !complete && !stager.document());
  Bytes shortPage = p1; shortPage.pop_back();
  assert(stager.accept(7, shortPage.data(), shortPage.size(), 700, complete) == ps::BadRequest);
  assert(ps::validateRequest(ps::KindSettingsSet, shortPage.data(), shortPage.size(), 1) == ps::BadRequest);
  uint8_t oversized[ps::PageHeader]{}; ps::put16(oversized + 2, uint16_t(ps::DocMax + 1)); oversized[1] = 9;
  assert(ps::validateRequest(ps::KindSettingsSet, oversized, sizeof(oversized), 1) == ps::BadRequest);
  uint8_t pageOut[ps::PageHeader + ps::PageData]; uint8_t status = 0;
  assert(!ps::encodePage(doc.data(), doc.size(), 2, pageOut, sizeof(pageOut), &status) && status == ps::PageOutOfRange);
  std::puts("PASS: settings paging stages in order, verifies length/CRC32 and discards on any error");

  // 4. Atomic apply: a verified real export applies all at once; invalid or
  //    forbidden documents leave the current settings untouched.
  {
    RuntimeSettings current; RuntimeSettings::copy(current.phoneBlePeer, sizeof(current.phoneBlePeer), "AA:BB:CC:DD:EE:FF");
    current.pomodoroWasActive = 1;
    RuntimeSettings wanted = current; wanted.speakerGainMilli = 640; wanted.characterIntensity = 2; wanted.pomodoroWasActive = 0;
    RuntimeSettings::copy(wanted.robotName, sizeof(wanted.robotName), "Sonic");
    JsonDocument json; roboBehaviorExport(wanted, json.to<JsonObject>());
    std::string text; serializeJson(json, text);
    const Bytes realDoc(text.begin(), text.end());
    assert(realDoc.size() > ps::PageData && realDoc.size() <= ps::DocMax);
    stager.reset();
    for (uint8_t i = 0; i < ps::pageCount(realDoc.size()); ++i) {
      const Bytes page = pageOf(realDoc, i);
      assert(stager.accept(42, page.data(), page.size(), 1000, complete) == ps::Ok);
      assert(complete == (i + 1 == ps::pageCount(realDoc.size())));
    }
    RuntimeSettings applied = current;
    assert(roboBehaviorImportStaged(stager.document(), stager.size(), current, applied) == 0);
    assert(applied.speakerGainMilli == 640 && applied.characterIntensity == 2 && !strcmp(applied.robotName, "Sonic"));
    assert(applied.pomodoroWasActive == 1 && !strcmp(applied.phoneBlePeer, "AA:BB:CC:DD:EE:FF"));
    json["speakerGainMilli"] = 5000; text.clear(); serializeJson(json, text);
    RuntimeSettings untouched = current;
    assert(roboBehaviorImportStaged(reinterpret_cast<const uint8_t*>(text.data()), text.size(), current, untouched) == 1);
    assert(roboBehaviorSettingsEqual(untouched, current) && untouched.speakerGainMilli == current.speakerGainMilli && untouched.phonePlatform == current.phonePlatform);
    json["speakerGainMilli"] = 640; json["phonePlatform"] = 1; text.clear(); serializeJson(json, text);
    assert(roboBehaviorImportStaged(reinterpret_cast<const uint8_t*>(text.data()), text.size(), current, untouched) == 2);
    assert(roboBehaviorSettingsEqual(untouched, current) && untouched.speakerGainMilli == current.speakerGainMilli && untouched.phonePlatform == current.phonePlatform);
    const char truncated[] = "{\"schema\":1";
    assert(roboBehaviorImportStaged(reinterpret_cast<const uint8_t*>(truncated), sizeof(truncated) - 1, current, untouched) == 1);
  }
  std::puts("PASS: staged settings apply atomically; invalid, truncated and platform changes are rejected");

  // 5. Auth: only a live, bonded, MAC-verified Android session is admitted; a
  //    tampered or wrong-session settings envelope never reaches the handler.
  {
    ps::AuthContext ok; ok.sessionActive = ok.sameConnection = ok.bondedTrusted = ok.macVerified = ok.androidPlatform = true;
    assert(ps::admitted(ok));
    for (int i = 0; i < 5; ++i) {
      ps::AuthContext a = ok;
      bool* fields[] = {&a.sessionActive, &a.sameConnection, &a.bondedTrusted, &a.macVerified, &a.androidPlatform};
      *fields[i] = false; assert(!ps::admitted(a));
    }
    TagFixture tag{hex(fixture.at("secret_wifi_slot1").at("envelope"))};
    PhoneBridgeProtocol::EnvelopeView view;
    assert(PhoneBridgeProtocol::verifyEnvelope(tag.wire.data(), tag.wire.size(), PhoneBridgeProtocol::PhoneToRobot, 0x78563412u, zeroAge, nullptr, verifyTag, &tag, &view));
    assert(view.kind == ps::KindSecretSet);
    assert(!PhoneBridgeProtocol::verifyEnvelope(tag.wire.data(), tag.wire.size(), PhoneBridgeProtocol::PhoneToRobot, 0x78563413u, zeroAge, nullptr, verifyTag, &tag, &view));
    Bytes aged = tag.wire; aged[16] = 1;  // ageMs must be 0 for settings kinds
    assert(!PhoneBridgeProtocol::verifyEnvelope(aged.data(), aged.size(), PhoneBridgeProtocol::PhoneToRobot, 0x78563412u, zeroAge, nullptr, verifyTag, &tag, &view));
    {  // Phase 2 kinds share the gate: a clear-memory envelope is rejected for another session, nonzero age or a flipped bit.
      TagFixture clear{hex(fixture.at("memory_clear").at("envelope"))};
      assert(PhoneBridgeProtocol::verifyEnvelope(clear.wire.data(), clear.wire.size(), PhoneBridgeProtocol::PhoneToRobot, 0x78563412u, zeroAge, nullptr, verifyTag, &clear, &view) && view.kind == ps::KindMemoryClear);
      assert(!PhoneBridgeProtocol::verifyEnvelope(clear.wire.data(), clear.wire.size(), PhoneBridgeProtocol::PhoneToRobot, 0x78563413u, zeroAge, nullptr, verifyTag, &clear, &view));
      Bytes flipped = clear.wire; flipped[flipped.size() - 40] ^= 1; TagFixture flippedTag = clear;
      assert(!PhoneBridgeProtocol::verifyEnvelope(flipped.data(), flipped.size(), PhoneBridgeProtocol::PhoneToRobot, 0x78563412u, zeroAge, nullptr, verifyTag, &flippedTag, &view));
    }
    Bytes tampered = tag.wire; tampered[30] ^= 1; TagFixture original = tag;
    assert(!PhoneBridgeProtocol::verifyEnvelope(tampered.data(), tampered.size(), PhoneBridgeProtocol::PhoneToRobot, 0x78563412u, zeroAge, nullptr, verifyTag, &original, &view));
  }
  std::puts("PASS: settings kinds require bonded MAC-verified Android session; wrong session/age/tamper rejected");

  // 6. Exchange slot: single in-flight request, session-bound response, and a
  //    SecretSet response never echoes the secret.
  {
    ps::Exchange x;
    const Bytes secret = hex(fixture.at("secret_wifi_slot1").at("payload"));
    assert(x.claim(0x1111, 4, ps::KindSecretSet, secret.data(), secret.size()) && x.state() == ps::Exchange::Working);
    assert(!x.claim(0x1111, 5, ps::KindSettingsGet, secret.data(), 1));  // busy
    assert(contains(x.buffer(), ps::Exchange::Capacity, "correct horse"));
    assert(x.complete(ps::Ok, 64));
    const uint8_t* p = nullptr; size_t n = 0;
    assert(x.take(0x1111, p, n) && n == ps::ResponseHeader);
    assert(Bytes(p, p + n) == response(4, ps::KindSecretSet, ps::Ok));
    assert(!contains(x.buffer(), ps::Exchange::Capacity, "correct horse") && !contains(x.buffer(), ps::Exchange::Capacity, "HomeNet"));
    for (size_t i = ps::ResponseHeader; i < ps::Exchange::Capacity; ++i) assert(x.buffer()[i] == 0);
    x.release(); assert(x.state() == ps::Exchange::Idle);
    const Bytes get = {0};
    assert(x.claim(0x2222, 9, ps::KindSettingsGet, get.data(), get.size()));
    memcpy(x.buffer() + ps::ResponseHeader, p0.data(), p0.size());
    assert(x.complete(ps::Ok, p0.size()));
    assert(!x.take(0x3333, p, n) && x.state() == ps::Exchange::Idle);  // session changed: dropped and wiped
    for (size_t i = 0; i < ps::Exchange::Capacity; ++i) assert(x.buffer()[i] == 0);
    assert(x.claim(0x2222, 10, ps::KindSettingsGet, get.data(), get.size()) && x.complete(ps::Ok, 0));
    x.dropStale(0); assert(x.state() == ps::Exchange::Idle);
    assert(!x.claim(0, 1, ps::KindSettingsGet, get.data(), 1) && !x.claim(1, 1, 7, get.data(), 1));
    assert(!x.complete(ps::Ok, 0));
  }
  std::puts("PASS: exchange slot is single-flight, session-bound and never echoes secrets");

  // 7. Strict secret/command/request decoding.
  {
    ps::Secret s;
    const Bytes keep = hex(fixture.at("secret_wifi_keep_password_slot0").at("payload"));
    assert(ps::decodeSecret(keep.data(), keep.size(), s) && s.slot == 0 && (s.flags & ps::WifiKeepPassword) && s.ssidLength == 6);
    const Bytes clear = hex(fixture.at("secret_wifi_clear_slot2").at("payload"));
    assert(ps::decodeSecret(clear.data(), clear.size(), s) && s.slot == 2 && !s.ssidLength);
    const Bytes key = hex(fixture.at("secret_gemini_key").at("payload"));
    assert(ps::decodeSecret(key.data(), key.size(), s) && s.keyLength == 18 && !memcmp(s.key, "AIzaSyTEST-key_123", 18));
    const uint8_t badSlot[] = {1, 3, 0, 0, 0}, badFlags[] = {1, 0, 2, 0, 0}, keepClear[] = {1, 0, 1, 0, 0}, passNoSsid[] = {1, 0, 0, 0, 1, 'x'};
    const uint8_t control[] = {1, 0, 0, 1, '\n', 0}, badKey[] = {2, 3, 'a', '/', 'b'}, longKey[] = {2, 97}, unknown[] = {3, 0};
    for (const auto& c : {Bytes(badSlot, badSlot + 5), Bytes(badFlags, badFlags + 5), Bytes(keepClear, keepClear + 5),
                          Bytes(passNoSsid, passNoSsid + 6), Bytes(control, control + 6), Bytes(badKey, badKey + 5),
                          Bytes(longKey, longKey + 2), Bytes(unknown, unknown + 2), Bytes(key.begin(), key.end() - 1)})
      assert(!ps::decodeSecret(c.data(), c.size(), s));
    Bytes trailing = keep; trailing.push_back(0); assert(!ps::decodeSecret(trailing.data(), trailing.size(), s));
    ps::Command c;
    const Bytes factory = hex(fixture.at("command_factory_reset").at("payload"));
    assert(ps::decodeCommand(factory.data(), factory.size(), 0x78563412u, c) == ps::Ok && c.op == ps::CommandFactoryReset);
    assert(ps::decodeCommand(factory.data(), factory.size(), 0x78563413u, c) == ps::ConfirmRequired);
    Bytes wrong = factory; wrong[1] = 'w'; assert(ps::decodeCommand(wrong.data(), wrong.size(), 0x78563412u, c) == ps::ConfirmRequired);
    assert(ps::decodeCommand(factory.data(), 5, 0x78563412u, c) == ps::BadRequest);
    const uint8_t reboot0[] = {1, 0}, reboot4[] = {1, 4}, otaArgs[] = {3, 1}, op9[] = {16};
    assert(ps::decodeCommand(reboot0, 2, 1, c) == ps::BadRequest && ps::decodeCommand(reboot4, 2, 1, c) == ps::BadRequest);
    assert(ps::decodeCommand(otaArgs, 2, 1, c) == ps::BadRequest && ps::decodeCommand(op9, 1, 1, c) == ps::Unsupported);
    const uint8_t page9[] = {9}, page1[] = {1};
    assert(ps::validateRequest(ps::KindSettingsGet, page9, 1, 1) == ps::PageOutOfRange);
    assert(ps::validateRequest(ps::KindSettingsGet, page9, 0, 1) == ps::BadRequest);
    assert(ps::validateRequest(ps::KindStatusSnapshot, page1, 1, 1) == ps::PageOutOfRange);
    assert(ps::validateRequest(27, page1, 1, 1) == ps::Unsupported && !ps::isRequestKind(27) && !ps::isRequestKind(7) && ps::isRequestKind(26));  // 25/26 are chat
  }
  std::puts("PASS: secret, command and request shapes are strictly bounded");

  // 7b. Live-control ops 4..13: strict shapes, bounds, mapping to the dashboard companion actions.
  {
    const uint32_t session = 0x78563412u; ps::Command c; ps::LiveAction a;
    auto decode = [&](const Bytes& p) { c = ps::Command{}; return ps::decodeCommand(p.data(), p.size(), session, c); };
    auto live = [&](const char* fixtureName, const char* action, const char* value, uint8_t op) {
      const Bytes p = hex(fixture.at(fixtureName).at("payload"));
      assert(decode(p) == ps::Ok && c.op == op && ps::isLiveCommand(op) && ps::liveCommandAction(c, a));
      assert(!strcmp(a.action, action) && !strcmp(a.value, value));
      assert(ps::validateRequest(ps::KindCommand, p.data(), p.size(), session) == ps::Ok);
    };
    live("command_expression_love", "expression", "love", ps::CommandExpression);
    live("command_talk", "talk", "", ps::CommandTalk);
    live("command_pause_15m", "pause", "900", ps::CommandPause);
    live("command_resume", "resume", "", ps::CommandPause);
    live("command_pomodoro_start", "pomodoro_start", "", ps::CommandPomodoro);
    live("command_activity_start_5", "activity_start", "", ps::CommandActivity); assert(a.activityId == 5);
    live("command_activity_skip", "activity_skip", "", ps::CommandActivity);
    live("command_game_accept", "game_accept", "", ps::CommandGame);
    live("command_learning_on", "learning_enable", "on", ps::CommandLearning);
    live("command_sound_test_shake", "sound_test", "shake", ps::CommandSoundTest);
    live("command_ir_send", "ir_send", "20df10ef", ps::CommandIr); assert(c.code == 0x20df10efu);
    live("command_ir_learn_tv", "ir_learn", "TV power", ps::CommandIr);
    live("command_ir_replay_tv", "ir_replay", "TV power", ps::CommandIr);
    live("command_experiences_clear", "experiences_clear", "", ps::CommandExperiencesClear);
    live("command_rest_sleep", "sleep_now", "", ps::CommandRest);
    live("command_rest_wake", "wake_now", "", ps::CommandRest);
    live("command_pet", "remote_pet", "", ps::CommandPet);
    // Sub-action tables.
    for (uint8_t sub = 0; sub <= 3; ++sub) { assert(decode({ps::CommandPause, sub}) == ps::Ok && ps::liveCommandAction(c, a)); }
    assert(decode({ps::CommandPause, 3}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.value, "3600"));
    assert(decode({ps::CommandPause, 1}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.action, "pause") && !strcmp(a.value, "0"));
    static const char* const pom[] = {"", "pomodoro_start", "pomodoro_pause", "pomodoro_resume", "pomodoro_stop"};
    for (uint8_t s = 1; s <= 4; ++s) assert(decode({ps::CommandPomodoro, s}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.action, pom[s]));
    static const char* const act[] = {"", "activity_start", "activity_pause", "activity_resume", "activity_cancel", "activity_skip", "activity_favorite", "activity_history_clear"};
    for (uint8_t s = 2; s <= 7; ++s) assert(decode({ps::CommandActivity, s}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.action, act[s]) && !a.activityId);
    assert(decode({ps::CommandActivity, 1, 13}) == ps::Ok && c.id == 13);
    assert(decode({ps::CommandGame, 2}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.action, "game_cancel"));
    assert(decode({ps::CommandLearning, 0}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.value, "off"));
    assert(decode({ps::CommandLearning, 2}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.action, "learning_reset"));
    for (uint8_t i = 0; i < ps::ExpressionCount; ++i) assert(decode({ps::CommandExpression, i}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.value, ps::ExpressionNames[i]));
    for (uint8_t i = 0; i < ps::SoundCueCount; ++i) assert(decode({ps::CommandSoundTest, i}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.value, ps::SoundCueNames[i]));
    assert(decode({ps::CommandIr, 1, 0, 0, 0, 0}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.value, "0"));
    assert(decode({ps::CommandIr, 1, 0x0f, 0, 0, 0}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.value, "f"));
    assert(decode({ps::CommandIr, 1, 0xff, 0xff, 0xff, 0xff}) == ps::Ok && ps::liveCommandAction(c, a) && !strcmp(a.value, "ffffffff"));
    // Out of range, wrong length and bad names = BadRequest (never reaches the S3).
    const std::vector<Bytes> bad = {
      {ps::CommandExpression}, {ps::CommandExpression, ps::ExpressionCount}, {ps::CommandExpression, 0, 0}, {ps::CommandTalk, 0}, {ps::CommandPause}, {ps::CommandPause, 4},
      {ps::CommandPomodoro, 0}, {ps::CommandPomodoro, 5}, {ps::CommandActivity}, {ps::CommandActivity, 0}, {ps::CommandActivity, 8},
      {ps::CommandActivity, 1}, {ps::CommandActivity, 1, 0}, {ps::CommandActivity, 1, 14}, {ps::CommandActivity, 1, 5, 0}, {ps::CommandActivity, 2, 5},
      {ps::CommandGame, 0}, {ps::CommandGame, 3}, {ps::CommandLearning, 3}, {ps::CommandLearning}, {ps::CommandSoundTest, 12}, {ps::CommandSoundTest},
      {ps::CommandIr}, {ps::CommandIr, 0}, {ps::CommandIr, 4}, {ps::CommandIr, 1, 0, 0, 0}, {ps::CommandIr, 1, 0, 0, 0, 0, 0},
      {ps::CommandIr, 2}, {ps::CommandIr, 2, 0}, {ps::CommandIr, 2, 1}, {ps::CommandIr, 2, 1, 'a', 'b'}, {ps::CommandIr, 3, 2, 'a'},
      {ps::CommandIr, 2, 1, '/'}, {ps::CommandIr, 2, 1, '\n'}, {ps::CommandIr, 2, 1, 0}, {ps::CommandIr, 2, 1, 0xc3},
      {ps::CommandExperiencesClear, 0}, {ps::CommandRest}, {ps::CommandRest, 0}, {ps::CommandRest, 3}, {ps::CommandRest, 1, 0}, {ps::CommandPet, 0}};
    for (const auto& p : bad) assert(decode(p) == ps::BadRequest && ps::validateRequest(ps::KindCommand, p.data(), p.size(), session) == ps::BadRequest);
    Bytes name16 = {ps::CommandIr, 2, 16}; name16.insert(name16.end(), 16, 'a'); assert(decode(name16) == ps::BadRequest);
    Bytes name15 = {ps::CommandIr, 2, 15}; name15.insert(name15.end(), 15, 'a'); assert(decode(name15) == ps::Ok && c.nameLength == 15);
    Bytes dashed = {ps::CommandIr, 3, 7, 'a', '-', 'B', '_', '9', ' ', 'z'}; assert(decode(dashed) == ps::Ok);
    assert(decode({16}) == ps::Unsupported && decode({0}) == ps::Unsupported && !ps::isLiveCommand(3) && !ps::isLiveCommand(16) && ps::isLiveCommand(ps::CommandPet));
    // Remote expression/pet/sleep: >= 0.4 s apart and <= 40 per minute; other live ops are not limited.
    { ps::RemoteRateLimit rl; uint32_t t = 0xFFFFFFFFu - 20000u; assert(rl.allow(t) && !rl.allow(t + ps::RemoteRateLimit::GapMs - 1) && rl.allow(t + ps::RemoteRateLimit::GapMs));
      unsigned granted = 2; for (uint32_t k = 2 * ps::RemoteRateLimit::GapMs; k < 60000; k += ps::RemoteRateLimit::GapMs) granted += rl.allow(t + k) ? 1u : 0u; assert(granted == ps::RemoteRateLimit::PerWindow);
      assert(rl.allow(t + 60000) && ps::remoteRateLimited(ps::CommandPet) && ps::remoteRateLimited(ps::CommandRest) && ps::remoteRateLimited(ps::CommandExpression) && !ps::remoteRateLimited(ps::CommandTalk)); }
    // The S3 link carries session + payload; the longest request (IR learn, 15-byte name) fits the exchange and a UART Settings frame.
    assert(ps::LinkCommand == 12 && ps::LinkCommand != ps::LinkReboot && 4 + 1 + 4 + name15.size() <= 512);
    // Live ops are authenticated like every other kind: no session, no response.
    ps::AuthContext anon; assert(!ps::admitted(anon));
  }
  std::puts("PASS: live-control ops 4..13 are strictly bounded and map to the companion actions");

  // 8. Memory / reminder kinds (phase 2): codec, bounds, UTF-8, confirm token, paging and CRC helpers.
  {
    const auto payloadOf = [&](const char* name) { return hex(fixture.at(name).at("payload")); };
    const uint32_t session = 0x78563412u;
    assert(ps::isForwardedKind(13) && ps::isForwardedKind(19) && !ps::isForwardedKind(12) && !ps::isForwardedKind(20));
    for (uint8_t kind = 13; kind <= 19; ++kind) assert(ps::linkOpForKind(kind) == kind - 8);
    assert(ps::linkOpForKind(ps::KindMemoryList) == ps::LinkMemoryList && ps::linkOpForKind(ps::KindReminderDelete) == ps::LinkReminderDelete);
    assert(ps::isListKind(13) && ps::isListKind(17) && !ps::isListKind(14));
    // Incremental CRC equals the one-shot CRC over the concatenation.
    const uint8_t a[] = {1, 2, 3}, b[] = {4, 5};
    uint8_t joined[5]; memcpy(joined, a, 3); memcpy(joined + 3, b, 2);
    assert(~ps::crc32Update(ps::crc32Update(~0u, a, 3), b, 2) == ps::crc32(joined, 5) && ~ps::crc32Update(~0u, nullptr, 0) == 0);

    const Bytes list = payloadOf("memory_list_page0");
    assert(ps::validateRequest(ps::KindMemoryList, list.data(), list.size(), session) == ps::Ok);
    Bytes pageNine = list; pageNine[4] = 9; assert(ps::validateRequest(ps::KindMemoryList, pageNine.data(), pageNine.size(), session) == ps::PageOutOfRange);
    assert(ps::validateRequest(ps::KindMemoryList, list.data(), 4, session) == ps::BadRequest && ps::validateRequest(ps::KindMemoryList, list.data(), 0, session) == ps::BadRequest);
    const Bytes remList = payloadOf("reminder_list_page0");
    assert(ps::validateRequest(ps::KindReminderList, remList.data(), remList.size(), session) == ps::Ok);
    assert(ps::validateRequest(ps::KindReminderList, remList.data(), 0, session) == ps::BadRequest && ps::validateRequest(ps::KindReminderList, list.data(), 2, session) == ps::BadRequest);

    // MemoryAdd: exact length, key 1..39, value 1..159, category < 6, strict UTF-8, no controls.
    ps::MemoryAdd add;
    const Bytes drink = payloadOf("memory_add_drink"), utf8 = payloadOf("memory_add_name_utf8");
    assert(ps::decodeMemoryAdd(drink.data(), drink.size(), add) && add.kind == 1 && add.keyLength == 5 && add.valueLength == 4 && !memcmp(add.value, "kopi", 4));
    assert(ps::decodeMemoryAdd(utf8.data(), utf8.size(), add) && add.kind == 0 && add.valueLength == 5);
    const auto memAdd = [](uint8_t kind, const std::string& key, const std::string& value) {
      Bytes out = {kind, uint8_t(key.size()), uint8_t(value.size())};
      out.insert(out.end(), key.begin(), key.end()); out.insert(out.end(), value.begin(), value.end()); return out;
    };
    const auto addStatus = [&](const Bytes& p) { return ps::validateRequest(ps::KindMemoryAdd, p.data(), p.size(), session); };
    assert(addStatus(memAdd(5, std::string(39, 'k'), std::string(159, 'v'))) == ps::Ok);                        // largest valid
    assert(addStatus(memAdd(5, std::string(40, 'k'), "v")) == ps::BadRequest && addStatus(memAdd(5, "k", std::string(160, 'v'))) == ps::BadRequest);
    assert(addStatus(memAdd(5, "", "v")) == ps::BadRequest && addStatus(memAdd(5, "k", "")) == ps::BadRequest && addStatus(memAdd(6, "k", "v")) == ps::BadRequest);
    assert(addStatus(memAdd(5, "k", "tab\there")) == ps::BadRequest && addStatus(memAdd(5, "k\n", "v")) == ps::BadRequest);   // control chars
    assert(addStatus(memAdd(5, "k", std::string("a\x7f", 2))) == ps::BadRequest && addStatus(memAdd(5, "k", "   ")) == ps::BadRequest);
    assert(addStatus(memAdd(5, "k", std::string("a\xc2\x85", 3))) == ps::BadRequest);                           // C1 control U+0085
    assert(addStatus(memAdd(5, "k", "a\xe2\x80\xa8")) == ps::BadRequest);                                      // U+2028
    assert(addStatus(memAdd(5, "k", "\xc0\xaf")) == ps::BadRequest && addStatus(memAdd(5, "k", "\xe0\x80\xaf")) == ps::BadRequest);  // overlong
    assert(addStatus(memAdd(5, "k", "\xed\xa0\x80")) == ps::BadRequest);                                       // surrogate
    assert(addStatus(memAdd(5, "k", "\xf4\x90\x80\x80")) == ps::BadRequest && addStatus(memAdd(5, "k", "\xf8\x88\x80\x80\x80")) == ps::BadRequest);
    assert(addStatus(memAdd(5, "k", "\xc3")) == ps::BadRequest && addStatus(memAdd(5, "k", "\xff")) == ps::BadRequest && addStatus(memAdd(5, "k", "\x80")) == ps::BadRequest);
    assert(addStatus(memAdd(5, "k", "\xf0\x9f\x98\x80")) == ps::Ok);                                          // U+1F600 is valid
    Bytes trailing = drink; trailing.push_back('x'); assert(addStatus(trailing) == ps::BadRequest);
    Bytes shortAdd = drink; shortAdd.pop_back(); assert(addStatus(shortAdd) == ps::BadRequest);

    // ReminderAdd: per-schedule field rules.
    const auto remAdd = [](uint8_t schedule, uint16_t minute, uint32_t value, const std::string& text) {
      Bytes out(8); out[0] = schedule; ps::put16(out.data() + 1, minute); ps::put32(out.data() + 3, value); out[7] = uint8_t(text.size());
      out.insert(out.end(), text.begin(), text.end()); return out;
    };
    const auto remStatus = [&](const Bytes& p) { return ps::validateRequest(ps::KindReminderAdd, p.data(), p.size(), session); };
    for (const char* name : {"reminder_add_timer", "reminder_add_daily", "reminder_add_once"}) { const Bytes p = payloadOf(name); assert(remStatus(p) == ps::Ok); }
    assert(remStatus(remAdd(3, 0, 86400, "x")) == ps::Ok && remStatus(remAdd(3, 0, 86401, "x")) == ps::BadRequest && remStatus(remAdd(3, 0, 0, "x")) == ps::BadRequest);
    assert(remStatus(remAdd(3, 5, 60, "x")) == ps::BadRequest && remStatus(remAdd(2, 1440, 0, "x")) == ps::BadRequest && remStatus(remAdd(2, 1439, 0, "x")) == ps::Ok);
    assert(remStatus(remAdd(2, 60, 1, "x")) == ps::BadRequest && remStatus(remAdd(1, 0, 0, "x")) == ps::BadRequest && remStatus(remAdd(1, 7, 100, "x")) == ps::BadRequest);
    assert(remStatus(remAdd(0, 0, 1, "x")) == ps::BadRequest && remStatus(remAdd(4, 0, 1, "x")) == ps::BadRequest);
    assert(remStatus(remAdd(3, 0, 60, std::string(111, 'r'))) == ps::Ok && remStatus(remAdd(3, 0, 60, std::string(112, 'r'))) == ps::BadRequest && remStatus(remAdd(3, 0, 60, "")) == ps::BadRequest);
    assert(remStatus(remAdd(3, 0, 60, "a\nb")) == ps::BadRequest && remStatus(remAdd(3, 0, 60, "\xc3\x28")) == ps::BadRequest);
    Bytes badLength = remAdd(3, 0, 60, "abc"); badLength[7] = 4; assert(remStatus(badLength) == ps::BadRequest);

    // Delete takes exactly one non-zero id; clear needs the session-bound token.
    const Bytes del = payloadOf("memory_delete");
    assert(ps::validateRequest(ps::KindMemoryDelete, del.data(), del.size(), session) == ps::Ok && ps::validateRequest(ps::KindReminderDelete, del.data(), del.size(), session) == ps::Ok);
    const uint8_t zeroId[4] = {0, 0, 0, 0};
    assert(ps::validateRequest(ps::KindMemoryDelete, zeroId, 4, session) == ps::BadRequest && ps::validateRequest(ps::KindMemoryDelete, del.data(), 3, session) == ps::BadRequest);
    assert(ps::validateRequest(ps::KindReminderDelete, zeroId, 4, session) == ps::BadRequest && ps::validateRequest(ps::KindReminderDelete, del.data(), 5, session) == ps::BadRequest);
    const Bytes clear = payloadOf("memory_clear");
    assert(ps::validateRequest(ps::KindMemoryClear, clear.data(), clear.size(), session) == ps::Ok);
    assert(ps::validateRequest(ps::KindMemoryClear, clear.data(), clear.size(), session + 1) == ps::ConfirmRequired);
    Bytes wrongToken = clear; wrongToken[0] = 'W'; assert(ps::validateRequest(ps::KindMemoryClear, wrongToken.data(), wrongToken.size(), session) == ps::ConfirmRequired);
    const uint8_t factory[8] = {'W', 'I', 'P', 'E', 0x12, 0x34, 0x56, 0x78};  // the factory-reset token is not a clear token
    assert(ps::validateRequest(ps::KindMemoryClear, factory, 8, session) == ps::ConfirmRequired);
    assert(ps::validateRequest(ps::KindMemoryClear, clear.data(), 7, session) == ps::BadRequest && ps::validateRequest(ps::KindMemoryClear, clear.data(), 0, session) == ps::BadRequest);
    uint8_t token[8]; ps::memoryClearToken(session, token); assert(!memcmp(token, clear.data(), 8));

    // Entry encoders reproduce the fixture list documents; paged responses carry doc CRC.
    uint8_t entry[ps::MemoryEntryMax];
    const size_t n1 = ps::encodeMemoryEntry(entry, 1, 1800000000u, 20833, 1, 3, "drink", "kopi");
    assert(n1 == ps::MemoryEntryFixed + 9 && ps::get32(entry) == 1 && ps::get32(entry + 4) == 1800000000u && ps::get16(entry + 8) == 20833 && entry[12] == 5 && entry[13] == 4);
    const Bytes memResponse = payloadOf("response_memory_list_page0");
    ps::PageView v; assert(ps::decodePage(memResponse.data() + ps::ResponseHeader, memResponse.size() - ps::ResponseHeader, v) && v.count == 1);
    assert(!memcmp(v.data + ps::MemoryListHeader, entry, n1));
    uint8_t reminderEntry[ps::ReminderEntryFixed + ps::ReminderTextMax];
    const size_t rn = ps::encodeReminderEntry(reminderEntry, 2, 2, 480, 0, "Sarapan");
    const Bytes remResponse = payloadOf("response_reminder_list_page0");
    assert(ps::decodePage(remResponse.data() + ps::ResponseHeader, remResponse.size() - ps::ResponseHeader, v) && v.data[ps::ReminderListHeader - 1] == 3);
    assert(contains(v.data, v.dataSize, "Sarapan") && rn == ps::ReminderEntryFixed + 7);
    // Every new status code keeps its documented number and header shape.
    assert(ps::ContentRejected == 17 && ps::Full == 18 && ps::NotFound == 19 && ps::Disabled == 20 && ps::TimeNotSet == 21);
    assert(hex(fixture.at("response_memory_clear_confirm_required").at("payload")) == response(16, ps::KindMemoryClear, ps::ConfirmRequired));
    assert(hex(fixture.at("response_memory_delete_not_found").at("payload")) == response(15, ps::KindMemoryDelete, ps::NotFound));
    assert(hex(fixture.at("response_reminder_add_time_not_set").at("payload")) == response(20, ps::KindReminderAdd, ps::TimeNotSet));
    assert(hex(fixture.at("response_memory_add_full").at("payload")) == response(13, ps::KindMemoryAdd, ps::Full));
    assert(hex(fixture.at("response_memory_add_content_rejected").at("payload")) == response(13, ps::KindMemoryAdd, ps::ContentRejected));
    static_assert(ps::ResponseHeader + ps::PageHeader + ps::PageData == 494, "list page response size");
    static_assert(ps::MemoryEntryMax == 212 && ps::MemoryEntryMax + ps::MemoryListHeader <= ps::DocMax, "one entry always fits a window");
  }
  std::puts("PASS: memory/reminder codec, UTF-8 and control-character rejection, confirm token, bounds and fixtures");

  // 9. IR profile list (kind 20) and external integrations (kinds 21..24): codec, bounds, write-only secrets, authorization.
  {
    const auto payloadOf = [&](const char* name) { return hex(fixture.at(name).at("payload")); };
    const uint32_t session = 0x78563412u;
    // 9a. Every request kind 8..24 and every live op 4..13 sits behind the same gate: one missing flag = not admitted (no response).
    for (uint8_t kind = 8; kind <= 24; ++kind) assert(ps::isRequestKind(kind));
    assert(!ps::isRequestKind(7) && !ps::isRequestKind(27) && !ps::isForwardedKind(20) && !ps::isForwardedKind(24) && ps::LinkIrProfiles == 13);
    for (unsigned mask = 0; mask < 32; ++mask) {
      ps::AuthContext a; a.sessionActive = mask & 1; a.sameConnection = mask & 2; a.bondedTrusted = mask & 4; a.macVerified = mask & 8; a.androidPlatform = mask & 16;
      assert(ps::admitted(a) == (mask == 31));
    }
    // Wrong session / nonzero age / tampering rejects each new kind's envelope; the right one verifies.
    PhoneBridgeProtocol::EnvelopeView view;
    for (const char* name : {"ir_profile_list", "integrations_get", "integrations_set_config", "integrations_set_token", "integrations_set_calendar",
                             "integrations_set_token_clear", "integrations_refresh", "integrations_clear", "integrations_status"}) {
      TagFixture t{hex(fixture.at(name).at("envelope"))};
      assert(PhoneBridgeProtocol::verifyEnvelope(t.wire.data(), t.wire.size(), PhoneBridgeProtocol::PhoneToRobot, session, zeroAge, nullptr, verifyTag, &t, &view));
      assert(view.kind >= 20 && view.kind <= 24 && ps::validateRequest(view.kind, view.payload, view.payloadSize, session) == ps::Ok);
      assert(!PhoneBridgeProtocol::verifyEnvelope(t.wire.data(), t.wire.size(), PhoneBridgeProtocol::PhoneToRobot, session + 1, zeroAge, nullptr, verifyTag, &t, &view));
      Bytes aged = t.wire; aged[16] = 1; TagFixture orig = t;
      assert(!PhoneBridgeProtocol::verifyEnvelope(aged.data(), aged.size(), PhoneBridgeProtocol::PhoneToRobot, session, zeroAge, nullptr, verifyTag, &orig, &view));
      Bytes flipped = t.wire; flipped[flipped.size() - 40] ^= 1;
      assert(!PhoneBridgeProtocol::verifyEnvelope(flipped.data(), flipped.size(), PhoneBridgeProtocol::PhoneToRobot, session, zeroAge, nullptr, verifyTag, &orig, &view));
    }
    // 9b. Shapes: page byte must be 0 for kind 20/21/24; action 1..2; exact lengths.
    for (uint8_t kind : {ps::KindIrProfileList, ps::KindIntegrationsGet, ps::KindIntegrationsStatus}) {
      const uint8_t zero[1] = {0}, one[1] = {1}, two[2] = {0, 0};
      assert(ps::validateRequest(kind, zero, 1, session) == ps::Ok && ps::validateRequest(kind, one, 1, session) == ps::PageOutOfRange);
      assert(ps::validateRequest(kind, two, 2, session) == ps::BadRequest && ps::validateRequest(kind, zero, 0, session) == ps::BadRequest);
    }
    for (uint8_t sub = 0; sub < 5; ++sub) {
      const uint8_t a[1] = {sub};
      assert(ps::validateRequest(ps::KindIntegrationsAction, a, 1, session) == ((sub == 1 || sub == 2) ? ps::Ok : ps::BadRequest));
    }
    const uint8_t act2[2] = {1, 0}; assert(ps::validateRequest(ps::KindIntegrationsAction, act2, 2, session) == ps::BadRequest);
    // 9c. IntegrationsSet: strict decode of config / token / calendar.
    ps::IntegrationsSet set;
    const Bytes cfg = payloadOf("integrations_set_config");
    assert(ps::decodeIntegrationsSet(cfg.data(), cfg.size(), set) && set.type == ps::IntSetConfig && (set.flags & ps::IntFlagWeather));
    assert(set.latitude == -6.2f && set.longitude == 106.8f && set.baseLength == 18 && !memcmp(set.base, "https://ha.example", 18));
    assert(set.entityCount == 2 && set.entityLength[0] == 11 && !memcmp(set.entity[1], "light.lamp", 10));
    const Bytes tok = payloadOf("integrations_set_token"), cal = payloadOf("integrations_set_calendar"), tokClear = payloadOf("integrations_set_token_clear");
    assert(ps::decodeIntegrationsSet(tok.data(), tok.size(), set) && set.type == ps::IntSetToken && set.textLength == 17 && !memcmp(set.text, "TESTTOKEN-abc_123", 17));
    assert(ps::decodeIntegrationsSet(cal.data(), cal.size(), set) && set.type == ps::IntSetCalendar && set.textLength == 32);
    assert(ps::decodeIntegrationsSet(tokClear.data(), tokClear.size(), set) && set.textLength == 0);
    auto setStatus = [&](const Bytes& p) { return ps::validateRequest(ps::KindIntegrationsSet, p.data(), p.size(), session); };
    auto withText = [](uint8_t type, size_t len, uint8_t fill) { Bytes b = {type, uint8_t(len)}; b.insert(b.end(), len, fill); return b; };
    assert(setStatus(withText(ps::IntSetToken, 191, 'a')) == ps::Ok && setStatus(withText(ps::IntSetToken, 192, 'a')) == ps::BadRequest);
    assert(setStatus(withText(ps::IntSetCalendar, 255, 'a')) == ps::Ok && setStatus(withText(4, 1, 'a')) == ps::BadRequest && setStatus(withText(0, 1, 'a')) == ps::BadRequest);
    assert(setStatus(withText(ps::IntSetToken, 3, ' ')) == ps::BadRequest && setStatus(withText(ps::IntSetToken, 3, '\n')) == ps::BadRequest && setStatus(withText(ps::IntSetToken, 3, 0x80)) == ps::BadRequest);
    Bytes tokTrail = tok; tokTrail.push_back('x'); Bytes tokShort = tok; tokShort.pop_back();
    assert(setStatus(tokTrail) == ps::BadRequest && setStatus(tokShort) == ps::BadRequest && setStatus(Bytes{ps::IntSetToken}) == ps::BadRequest && setStatus(Bytes{}) == ps::BadRequest);
    auto cfgOf = [](uint8_t flags, float lat, float lon, const std::string& base, const std::vector<std::string>& ents) {
      Bytes b(11); b[0] = ps::IntSetConfig; b[1] = flags; ps::putFloat_(b.data() + 2, lat); ps::putFloat_(b.data() + 6, lon); b[10] = uint8_t(base.size());
      b.insert(b.end(), base.begin(), base.end()); b.push_back(uint8_t(ents.size()));
      for (const auto& e : ents) { b.push_back(uint8_t(e.size())); b.insert(b.end(), e.begin(), e.end()); }
      return b;
    };
    assert(setStatus(cfgOf(0, 0, 0, "", {})) == ps::Ok && setStatus(cfgOf(1, 90, -180, std::string(127, 'h'), {std::string(47, 'e'), "a.b", "c.d"})) == ps::Ok);
    assert(setStatus(cfgOf(2, 0, 0, "", {})) == ps::BadRequest);                                                 // unknown flag
    assert(setStatus(cfgOf(0, 90.5f, 0, "", {})) == ps::BadRequest && setStatus(cfgOf(0, -90.5f, 0, "", {})) == ps::BadRequest);
    assert(setStatus(cfgOf(0, 0, 180.5f, "", {})) == ps::BadRequest && setStatus(cfgOf(0, 0, -180.5f, "", {})) == ps::BadRequest);
    assert(setStatus(cfgOf(0, NAN, 0, "", {})) == ps::BadRequest && setStatus(cfgOf(0, 0, INFINITY, "", {})) == ps::BadRequest);
    assert(setStatus(cfgOf(0, 0, 0, std::string(128, 'h'), {})) == ps::BadRequest && setStatus(cfgOf(0, 0, 0, "ht tp", {})) == ps::BadRequest);
    assert(setStatus(cfgOf(0, 0, 0, "", {"a.b", "c.d", "e.f", "g.h"})) == ps::BadRequest && setStatus(cfgOf(0, 0, 0, "", {""})) == ps::BadRequest);
    assert(setStatus(cfgOf(0, 0, 0, "", {std::string(48, 'e')})) == ps::BadRequest && setStatus(cfgOf(0, 0, 0, "", {"a b"})) == ps::BadRequest);
    Bytes cfgTrail = cfg; cfgTrail.push_back(0); Bytes cfgShort = cfg; cfgShort.pop_back(); Bytes cfgTiny(cfg.begin(), cfg.begin() + 11);
    assert(setStatus(cfgTrail) == ps::BadRequest && setStatus(cfgShort) == ps::BadRequest && setStatus(cfgTiny) == ps::BadRequest);
    // The largest request of these kinds fits the exchange slot.
    assert(11 + 127 + 1 + 3 * 48 <= ps::Exchange::RequestMax && 2 + ps::IntCalendarMax <= ps::Exchange::RequestMax);
    // 9d. Encoders reproduce the shared vectors; responses never contain the token or the calendar URL.
    {
      const char* names[] = {"TV power", "Fan"};
      Bytes out(1 + 4 * 16); const size_t n = ps::encodeIrProfiles(out.data(), out.size(), names, 2); out.resize(n);
      assert(payloadOf("response_ir_profile_list") == response(36, ps::KindIrProfileList, ps::Ok, out) && n <= 65);
      const char* mixed[] = {"ok", "bad/name", "", "x"}; Bytes m(65); const size_t mn = ps::encodeIrProfiles(m.data(), m.size(), mixed, 4);
      assert(mn == 1 + 3 + 2 && m[0] == 2 && m[1] == 2 && m[4] == 1 && ps::encodeIrProfiles(m.data(), m.size(), mixed, 5) == 0 && ps::encodeIrProfiles(m.data(), 10, mixed, 1) == 0);
      assert(payloadOf("response_ir_profile_list_robot_unavailable") == response(36, ps::KindIrProfileList, ps::RobotUnavailable));
      Bytes body(ps::PageHeader + ps::IntConfigDocMax); const char* ents[3] = {"sensor.temp", "light.lamp", ""};
      size_t doc = ps::encodeIntegrationsConfig(body.data() + ps::PageHeader, ps::IntConfigDocMax, true, true, true, -6.2f, 106.8f, "https://ha.example", ents);
      assert(doc && ps::sealOnePage(body.data(), doc) == ps::PageHeader + doc); body.resize(ps::PageHeader + doc);
      assert(payloadOf("response_integrations_get") == response(37, ps::KindIntegrationsGet, ps::Ok, body));
      ps::PageView v; assert(ps::decodePage(body.data(), body.size(), v) && v.count == 1 && v.length == doc);
      assert(!contains(body.data(), body.size(), "TESTTOKEN") && !contains(body.data(), body.size(), "cal.example") && (v.data[1] & 7) == 7);
      Bytes full(ps::PageHeader + ps::IntConfigDocMax); const std::string big(47, 'e'), hugeBase(127, 'h'); const char* bigEnts[3] = {big.c_str(), big.c_str(), big.c_str()};
      assert(ps::encodeIntegrationsConfig(full.data() + ps::PageHeader, ps::IntConfigDocMax, false, false, false, 0, 0, hugeBase.c_str(), bigEnts) == ps::IntConfigDocMax);
      assert(ps::encodeIntegrationsConfig(full.data() + ps::PageHeader, ps::IntConfigDocMax - 1, false, false, false, 0, 0, "", ents) == 0);
      ps::IntegrationsStatusData d; d.weatherConfigured = d.weatherValid = d.homeAssistantConfigured = d.calendarConfigured = d.calendarValid = true;
      d.homeAssistantError = 4; d.weatherAgeMs = 60000; d.weather = "21.5 C, clear"; d.calendarAgeMs = 120000; d.calendar = "2026-10-12 09:00Z Rapat";
      d.entityCount = 2; d.entity[0].slot = 0; d.entity[0].valid = true; d.entity[0].ageMs = 30000; d.entity[0].state = "23.4"; d.entity[1].slot = 1;
      Bytes sb(ps::PageHeader + ps::IntStatusDocMax); doc = ps::encodeIntegrationsStatus(sb.data() + ps::PageHeader, ps::IntStatusDocMax, d);
      assert(doc && ps::sealOnePage(sb.data(), doc) == ps::PageHeader + doc); sb.resize(ps::PageHeader + doc);
      assert(payloadOf("response_integrations_status") == response(44, ps::KindIntegrationsStatus, ps::Ok, sb));
      const std::string w(300, 'w'); ps::IntegrationsStatusData worst = d; worst.weather = w.c_str(); worst.calendar = w.c_str(); worst.entityCount = 3;
      for (auto& e : worst.entity) e.state = w.c_str();
      Bytes wb(ps::IntStatusDocMax); assert(ps::encodeIntegrationsStatus(wb.data(), wb.size(), worst) == ps::IntStatusDocMax);   // texts are clamped; fits one 500 B response
      assert(ps::ResponseHeader + ps::PageHeader + ps::IntStatusDocMax <= ps::Exchange::Capacity);
      assert(ps::encodeIntegrationsStatus(wb.data(), wb.size() - 1, worst) == 0 && !ps::sealOnePage(wb.data(), 0) && !ps::sealOnePage(wb.data(), ps::PageData + 1));
      assert(payloadOf("response_integrations_set_ok") == response(38, ps::KindIntegrationsSet, ps::Ok) && payloadOf("response_integrations_busy") == response(43, ps::KindIntegrationsAction, ps::Busy));
    }
    // 9e. Exchange: an IntegrationsSet request (token / calendar URL) is wiped by complete(); no body is ever returned for it.
    {
      ps::Exchange x;
      assert(x.claim(0x4444, 39, ps::KindIntegrationsSet, tok.data(), tok.size()) && contains(x.buffer(), ps::Exchange::Capacity, "TESTTOKEN-abc_123"));
      assert(x.complete(ps::Ok, 0));
      const uint8_t* p = nullptr; size_t n = 0; assert(x.take(0x4444, p, n) && n == ps::ResponseHeader && !contains(x.buffer(), ps::Exchange::Capacity, "TESTTOKEN"));
      x.release();
      assert(x.claim(0x4444, 40, ps::KindIntegrationsSet, cal.data(), cal.size()) && x.complete(ps::StorageFailed, 0) && !contains(x.buffer(), ps::Exchange::Capacity, "cal.example"));
      x.release();
      for (uint8_t kind = 20; kind <= 24; ++kind) { const uint8_t pg[1] = {0}; assert(x.claim(0x4444, 50u + kind, kind, pg, 1)); x.release(); }
      assert(!x.claim(0x4444, 1, 27, tok.data(), 1));
    }
    // Statuses used by the new kinds keep their numbers.
    assert(ps::Busy == 1 && ps::BadRequest == 2 && ps::RobotUnavailable == 4 && ps::StorageFailed == 8 && ps::UpdateBusy == 11 && ps::Unsupported == 12);
  }
  std::puts("PASS: IR profile list and integrations kinds 20..24: codec, bounds, write-only secrets, authorization gate");
}
