#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <atomic>
#include "RoboLinkProtocol.h"

// BLE settings protocol v1 (docs/mobile/BLE_SETTINGS_PROTOCOL.md). Payloads of
// authenticated v2 envelopes only; platform-neutral so host tests use the same
// codec, paging and exchange-slot rules as the C3 firmware.
namespace PhoneSettingsProtocol {
// Phone -> robot envelope kinds (1..7 are notification/navigation/companion).
constexpr uint8_t KindSettingsGet = 8;
constexpr uint8_t KindSettingsSet = 9;
constexpr uint8_t KindSecretSet = 10;
constexpr uint8_t KindStatusSnapshot = 11;
constexpr uint8_t KindCommand = 12;
// Memory facts and reminders (phase 2). The robot (S3) owns the data; the C3 only forwards.
constexpr uint8_t KindMemoryList = 13;
constexpr uint8_t KindMemoryAdd = 14;
constexpr uint8_t KindMemoryDelete = 15;
constexpr uint8_t KindMemoryClear = 16;
constexpr uint8_t KindReminderList = 17;
constexpr uint8_t KindReminderAdd = 18;
constexpr uint8_t KindReminderDelete = 19;
// IR profile names (S3) and external integrations (C3 config), docs/mobile/BLE_SETTINGS_PROTOCOL.md sections 16 and 17.
constexpr uint8_t KindIrProfileList = 20;
constexpr uint8_t KindIntegrationsGet = 21;
constexpr uint8_t KindIntegrationsSet = 22;
constexpr uint8_t KindIntegrationsAction = 23;
constexpr uint8_t KindIntegrationsStatus = 24;
// Chat with the robot (S3 owns the turn, the C3 only forwards). The text lives in RAM only and is never stored.
constexpr uint8_t KindChatSend = 25, KindChatPoll = 26;
// Robot -> phone response kind (0x7e companion result, 0x7f ack are existing).
constexpr uint8_t KindResponse = 0x7d;

enum Status : uint8_t {
  Ok = 0, Busy = 1, BadRequest = 2, NotAuthorized = 3, RobotUnavailable = 4,
  SequenceError = 5, CrcMismatch = 6, InvalidSettings = 7, StorageFailed = 8,
  NotImplemented = 9, ConfirmRequired = 10, UpdateBusy = 11, Unsupported = 12,
  PageOutOfRange = 13, Forbidden = 14, TooLarge = 15, NoMemory = 16,
  ContentRejected = 17, Full = 18, NotFound = 19, Disabled = 20, TimeNotSet = 21,
  NetworkUnavailable = 22, Rejected = 23
};

constexpr size_t ResponseHeader = 6;   // request counter u32, request kind, status
constexpr size_t PageHeader = 8;       // page, pageCount, totalLength u16, crc32
constexpr size_t PageData = 480;
constexpr size_t DocMax = 4096;
constexpr uint8_t MaxPages = uint8_t((DocMax + PageData - 1) / PageData);  // 9
constexpr uint32_t StageTimeoutMs = 30000;
constexpr size_t SsidMax = 32, PasswordMax = 64, GeminiKeyMax = 96;
constexpr uint8_t SecretWifi = 1, SecretGeminiKey = 2;
constexpr uint8_t WifiKeepPassword = 1;
constexpr uint8_t CommandReboot = 1, CommandFactoryReset = 2, CommandOtaStart = 3;
// Live-control ops (4..13) run on the S3 through the dashboard's companion actions. Ok = accepted.
constexpr uint8_t CommandExpression = 4, CommandTalk = 5, CommandPause = 6, CommandPomodoro = 7, CommandActivity = 8,
                  CommandGame = 9, CommandLearning = 10, CommandSoundTest = 11, CommandIr = 12, CommandExperiencesClear = 13;
// PD-2.5/3.3: Tidurkan/Bangunkan (sub 1 sleep, 2 wake) and a remote pet; rate limited on the S3 (RemoteRateLimit).
constexpr uint8_t CommandRest = 14, CommandPet = 15, RestSleep = 1, RestWake = 2;
constexpr uint8_t ActivityStart = 1, ActivityPause = 2, ActivityResume = 3, ActivityCancel = 4, ActivitySkip = 5,
                  ActivityFavorite = 6, ActivityHistoryClear = 7, ActivityIdMax = 13;
constexpr uint8_t IrSend = 1, IrLearn = 2, IrReplay = 3;
constexpr size_t IrNameMax = 15;
constexpr uint8_t ExpressionCount = 22, SoundCueCount = 12;
inline const char* const ExpressionNames[ExpressionCount] = {"happy", "curious", "shy", "love", "sad", "surprised", "thinking",
                                                             "agree", "disagree", "wink", "laugh", "sleepy",
                                                             "annoyed", "dizzy", "excited", "calm", "bored", "proud", "anxious", "relieved", "mischievous", "lonely"};
inline const char* const SoundCueNames[SoundCueCount] = {"wake", "happy", "curious", "affection", "thinking", "surprise", "sad",
                                                         "sleepy", "error", "success", "pickup", "shake"};
constexpr uint8_t RebootGateway = 1, RebootRobot = 2;
constexpr size_t StatusSizeV1 = 58, StatusSizeV2 = 65, StatusSizeV3 = 76, StatusSize = 79;
constexpr uint8_t StatusSchema = 4;
// OTA progress in StatusSnapshot schema 2 (spec section 7.1).
enum OtaState : uint8_t {
  OtaIdle = 0, OtaChecking = 1, OtaDownloadingS3 = 2, OtaFlashingS3 = 3, OtaWaitingS3Boot = 4,
  OtaDownloadingC3 = 5, OtaDone = 6, OtaFailed = 7, OtaUpToDate = 8
};
enum OtaError : uint8_t {
  OtaErrNone = 0, OtaErrWifi = 1, OtaErrClock = 2, OtaErrRobotBusy = 3, OtaErrNetwork = 4, OtaErrManifest = 5,
  OtaErrSignature = 6, OtaErrImage = 7, OtaErrRobotRejected = 8, OtaErrPairMismatch = 9, OtaErrRobotBoot = 10,
  OtaErrGatewayInstall = 11, OtaErrGatewayNotUpdated = 12, OtaErrRobotUnavailable = 13, OtaErrOther = 255
};

// UART ops carried in robolink::Settings / SettingsReply frames.
// Request: id u32, op u8, op data. Reply: id u32, op u8, status u8, body.
constexpr uint8_t LinkGetPage = 1, LinkPutPage = 2, LinkInfo = 3, LinkReboot = 4;
constexpr size_t LinkInfoSize = 16, LinkLiveSize = 9, LinkInfoSizeV2 = LinkInfoSize + LinkLiveSize;
// PD-2.4: mood id, mood cause, sleep state appended after the live tail (older S3 replies stop at V2).
constexpr size_t LinkMoodSize = 3, LinkInfoSizeV3 = LinkInfoSizeV2 + LinkMoodSize;
// Live command forwarded to the S3: data = session u32 + the BLE Command payload (op 4..13).
constexpr uint8_t LinkCommand = 12;
// IR profile list (kind 20 is NOT kind - 8: op 12 is LinkCommand). Request data = page u8 (0), like GetPage.
constexpr uint8_t LinkIrProfiles = 13;
// Chat kinds 25/26 (op != kind - 8). Request data = session u32 + the BLE payload.
constexpr uint8_t LinkChatSend = 14, LinkChatPoll = 15;
// Memory/reminder kinds map 1:1 to ops (op = kind - 8). Request data = session u32 + the BLE payload.
constexpr uint8_t LinkMemoryList = 5, LinkMemoryAdd = 6, LinkMemoryDelete = 7, LinkMemoryClear = 8,
                  LinkReminderList = 9, LinkReminderAdd = 10, LinkReminderDelete = 11;

// Memory / reminder limits (companion::Memory key[40], value[160]; Reminder text[112]).
constexpr uint8_t MemoryCategories = 6;  // livingeyes::SemanticMemoryKind 0..5
constexpr size_t MemoryKeyMax = 39, MemoryValueMax = 159, ReminderTextMax = 111;
constexpr uint8_t ListSchema = 1;
constexpr size_t MemoryListHeader = 8, ReminderListHeader = 6;
constexpr size_t MemoryEntryFixed = 14, MemoryEntryMax = MemoryEntryFixed + MemoryKeyMax + MemoryValueMax;
constexpr size_t ReminderEntryFixed = 12;
constexpr uint8_t ScheduleOnce = 1, ScheduleDaily = 2, ScheduleTimer = 3;
constexpr uint32_t TimerSecondsMax = 86400;

// Maps GitHubOtaUpdate / pair-flow failure strings to the OtaError byte.
inline uint8_t otaErrorCode(const char* f) {
  if (!f || !*f || !strcmp(f, "none")) return OtaErrNone;
  struct Entry { const char* name; uint8_t code; };
  static const Entry table[] = {
    {"wifi", OtaErrWifi}, {"clock", OtaErrClock}, {"robot_busy", OtaErrRobotBusy}, {"prepare_timeout", OtaErrRobotBusy},
    {"dns", OtaErrNetwork}, {"tcp", OtaErrNetwork}, {"transport", OtaErrNetwork}, {"tls", OtaErrNetwork},
    {"download", OtaErrNetwork}, {"http_status", OtaErrNetwork}, {"redirect", OtaErrNetwork},
    {"manifest_format", OtaErrManifest}, {"signature", OtaErrSignature}, {"image_hash", OtaErrImage}, {"image_size", OtaErrImage},
    {"image_begin", OtaErrRobotRejected}, {"image_write", OtaErrRobotRejected}, {"remote_commit", OtaErrRobotRejected},
    {"release_pair_mismatch", OtaErrPairMismatch}, {"robot_boot_not_confirmed", OtaErrRobotBoot},
    {"gateway_check", OtaErrGatewayInstall}, {"gateway_install", OtaErrGatewayInstall}, {"version_stage", OtaErrGatewayInstall},
    {"image_commit", OtaErrGatewayInstall}, {"gateway_not_updated", OtaErrGatewayNotUpdated}, {"robot_unavailable", OtaErrRobotUnavailable}};
  for (const auto& e : table) if (!strcmp(f, e.name)) return e.code;
  return OtaErrOther;
}

inline uint16_t get16(const uint8_t* p) { return robolink::get16(p); }
inline uint32_t get32(const uint8_t* p) { return robolink::get32(p); }
inline void put16(uint8_t* p, uint16_t v) { robolink::put16(p, v); }
inline void put32(uint8_t* p, uint32_t v) { robolink::put32(p, v); }
inline uint32_t crc32(const uint8_t* p, size_t n) { return robolink::crc32(p, n); }

inline bool isRequestKind(uint8_t kind) { return kind >= KindSettingsGet && kind <= KindChatPoll; }
inline bool isChatKind(uint8_t kind) { return kind == KindChatSend || kind == KindChatPoll; }
inline bool isForwardedKind(uint8_t kind) { return (kind >= KindMemoryList && kind <= KindReminderDelete) || isChatKind(kind); }
inline bool isListKind(uint8_t kind) { return kind == KindMemoryList || kind == KindReminderList; }
inline uint8_t linkOpForKind(uint8_t kind) { return kind == KindChatSend ? LinkChatSend : (kind == KindChatPoll ? LinkChatPoll : uint8_t(kind - 8)); }
// Incremental CRC-32: start with ~0u, finish with ~state (same polynomial as crc32()).
inline uint32_t crc32Update(uint32_t c, const uint8_t* p, size_t n) {
  while (n--) { c ^= *p++; for (unsigned i = 0; i < 8; ++i) c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u); }
  return c;
}
inline uint8_t pageCount(size_t length) { return length ? uint8_t((length + PageData - 1) / PageData) : 0; }
inline size_t pageLength(size_t length, uint8_t page) {
  const size_t start = size_t(page) * PageData;
  if (start >= length) return 0;
  return length - start < PageData ? length - start : PageData;
}

// Every BLE-reachable kind needs the bonded, MAC-verified session. The
// transport only reaches this after verifyEnvelope; the explicit check keeps
// the policy visible and testable. Unauthenticated input gets no response.
struct AuthContext {
  bool sessionActive = false, sameConnection = false, bondedTrusted = false,
       macVerified = false, androidPlatform = false;
};
inline bool admitted(const AuthContext& a) {
  return a.sessionActive && a.sameConnection && a.bondedTrusted && a.macVerified && a.androidPlatform;
}

inline size_t writeResponseHeader(uint8_t* out, uint32_t counter, uint8_t kind, uint8_t status) {
  put32(out, counter); out[4] = kind; out[5] = status; return ResponseHeader;
}

// One page of a document: header + data. Returns bytes written or 0.
inline size_t encodePage(const uint8_t* doc, size_t length, uint8_t page, uint8_t* out,
                         size_t capacity, uint8_t* status = nullptr) {
  if (status) *status = BadRequest;
  if (!doc || !out || !length) return 0;
  if (length > DocMax) { if (status) *status = TooLarge; return 0; }
  const uint8_t count = pageCount(length);
  if (page >= count) { if (status) *status = PageOutOfRange; return 0; }
  const size_t n = pageLength(length, page);
  if (capacity < PageHeader + n) return 0;
  out[0] = page; out[1] = count; put16(out + 2, uint16_t(length)); put32(out + 4, crc32(doc, length));
  memcpy(out + PageHeader, doc + size_t(page) * PageData, n);
  if (status) *status = Ok;
  return PageHeader + n;
}

struct PageView { uint8_t page = 0, count = 0; uint16_t length = 0; uint32_t crc = 0; const uint8_t* data = nullptr; size_t dataSize = 0; };
// Strict: data size must be exactly the size of that page.
inline bool decodePage(const uint8_t* p, size_t n, PageView& out) {
  if (!p || n < PageHeader) return false;
  PageView v; v.page = p[0]; v.count = p[1]; v.length = get16(p + 2); v.crc = get32(p + 4);
  v.data = p + PageHeader; v.dataSize = n - PageHeader;
  if (!v.length || v.length > DocMax || v.count != pageCount(v.length) || v.page >= v.count ||
      v.dataSize != pageLength(v.length, v.page)) return false;
  out = v; return true;
}

// Write-only secret. Pointers alias the request buffer; the caller wipes it.
struct Secret {
  uint8_t type = 0, slot = 0, flags = 0;
  const uint8_t* ssid = nullptr; uint8_t ssidLength = 0;
  const uint8_t* password = nullptr; uint8_t passwordLength = 0;
  const uint8_t* key = nullptr; uint8_t keyLength = 0;
};
inline bool printable_(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) if (p[i] < 0x20 || p[i] == 0x7f) return false;
  return true;
}
inline bool keyAlphabet_(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const uint8_t c = p[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
  }
  return true;
}
inline bool decodeSecret(const uint8_t* p, size_t n, Secret& out) {
  if (!p || n < 2) return false;
  Secret s; s.type = p[0];
  if (s.type == SecretWifi) {
    if (n < 5) return false;
    s.slot = p[1]; s.flags = p[2]; s.ssidLength = p[3];
    if (s.slot > 2 || (s.flags & ~WifiKeepPassword) || s.ssidLength > SsidMax || n < size_t(5) + s.ssidLength) return false;
    s.ssid = p + 4; s.passwordLength = p[4 + s.ssidLength]; s.password = p + 5 + s.ssidLength;
    if (s.passwordLength > PasswordMax || n != size_t(5) + s.ssidLength + s.passwordLength) return false;
    if (!printable_(s.ssid, s.ssidLength) || !printable_(s.password, s.passwordLength)) return false;
    if ((s.flags & WifiKeepPassword) && (s.passwordLength || !s.ssidLength)) return false;
    if (!s.ssidLength && s.passwordLength) return false;  // clearing a slot clears both
  } else if (s.type == SecretGeminiKey) {
    s.keyLength = p[1]; s.key = p + 2;
    if (s.keyLength > GeminiKeyMax || n != size_t(2) + s.keyLength || !keyAlphabet_(s.key, s.keyLength)) return false;
  } else {
    return false;
  }
  out = s; return true;
}

inline bool isLiveCommand(uint8_t op) { return op >= CommandExpression && op <= CommandPet; }
inline bool irNameValid_(const uint8_t* p, size_t n) {
  if (!p || !n || n > IrNameMax) return false;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t c = p[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ')) return false;
  }
  return true;
}
struct Command {
  uint8_t op = 0, target = 0;
  uint8_t sub = 0, id = 0, nameLength = 0;  // live ops: sub-action, expression/activity/cue id, IR name length
  uint32_t code = 0;                        // IR NEC code
  const uint8_t* name = nullptr;            // aliases the request buffer
};
// Factory reset confirm token: ASCII "WIPE" followed by the session ID (LE).
inline void factoryToken(uint32_t session, uint8_t out[8]) { memcpy(out, "WIPE", 4); put32(out + 4, session); }
inline uint8_t decodeCommand(const uint8_t* p, size_t n, uint32_t session, Command& out) {
  if (!p || !n) return BadRequest;
  Command c; c.op = p[0];
  if (c.op == CommandReboot) {
    if (n != 2 || !p[1] || (p[1] & ~uint8_t(RebootGateway | RebootRobot))) return BadRequest;
    c.target = p[1];
  } else if (c.op == CommandFactoryReset) {
    if (n != 9) return BadRequest;
    uint8_t token[8]; factoryToken(session, token);
    uint8_t diff = 0; for (unsigned i = 0; i < 8; ++i) diff |= uint8_t(token[i] ^ p[1 + i]);
    if (diff) return ConfirmRequired;
  } else if (c.op == CommandOtaStart) {
    if (n != 1) return BadRequest;
  } else if (isLiveCommand(c.op)) {
    switch (c.op) {
      case CommandExpression: if (n != 2 || p[1] >= ExpressionCount) return BadRequest; c.id = p[1]; break;
      case CommandTalk: case CommandExperiencesClear: case CommandPet: if (n != 1) return BadRequest; break;
      case CommandRest: if (n != 2 || p[1] < RestSleep || p[1] > RestWake) return BadRequest; c.sub = p[1]; break;
      case CommandPause: if (n != 2 || p[1] > 3) return BadRequest; c.sub = p[1]; break;
      case CommandPomodoro: if (n != 2 || p[1] < 1 || p[1] > 4) return BadRequest; c.sub = p[1]; break;
      case CommandActivity:
        if (n < 2 || p[1] < ActivityStart || p[1] > ActivityHistoryClear) return BadRequest;
        c.sub = p[1];
        if (c.sub == ActivityStart) { if (n != 3 || p[2] < 1 || p[2] > ActivityIdMax) return BadRequest; c.id = p[2]; }
        else if (n != 2) return BadRequest;
        break;
      case CommandGame: if (n != 2 || p[1] < 1 || p[1] > 2) return BadRequest; c.sub = p[1]; break;
      case CommandLearning: if (n != 2 || p[1] > 2) return BadRequest; c.sub = p[1]; break;
      case CommandSoundTest: if (n != 2 || p[1] >= SoundCueCount) return BadRequest; c.id = p[1]; break;
      default:  // CommandIr
        if (n < 2 || p[1] < IrSend || p[1] > IrReplay) return BadRequest;
        c.sub = p[1];
        if (c.sub == IrSend) { if (n != 6) return BadRequest; c.code = get32(p + 2); }
        else {
          if (n < 3) return BadRequest;
          c.nameLength = p[2];
          if (n != size_t(3) + c.nameLength || !irNameValid_(p + 3, c.nameLength)) return BadRequest;
          c.name = p + 3;
        }
        break;
    }
  } else {
    return Unsupported;
  }
  out = c; return Ok;
}

// The dashboard companion action a live command maps to (RoboDeskSonicCharacter.ino dashboardCompanionAction).
// "sound_test" and "experiences_clear" are the two non-companion dashboard handlers (/sound/test, /memory/experiences/clear).
// For activity_start the S3 resolves `activityId` to the catalog name.
struct LiveAction { const char* action = nullptr; char value[24]{}; uint8_t activityId = 0; };
inline bool liveCommandAction(const Command& c, LiveAction& out) {
  LiveAction a;
  switch (c.op) {
    case CommandExpression: if (c.id >= ExpressionCount) return false; a.action = "expression"; strcpy(a.value, ExpressionNames[c.id]); break;
    case CommandTalk: a.action = "talk"; break;
    case CommandPause:
      if (c.sub == 0) a.action = "resume";
      else { a.action = "pause"; strcpy(a.value, c.sub == 1 ? "0" : (c.sub == 2 ? "900" : "3600")); }
      break;
    case CommandPomodoro: {
      static const char* const n[] = {nullptr, "pomodoro_start", "pomodoro_pause", "pomodoro_resume", "pomodoro_stop"};
      if (c.sub < 1 || c.sub > 4) return false;
      a.action = n[c.sub]; break; }
    case CommandActivity: {
      static const char* const n[] = {nullptr, "activity_start", "activity_pause", "activity_resume", "activity_cancel",
                                      "activity_skip", "activity_favorite", "activity_history_clear"};
      if (c.sub < 1 || c.sub > ActivityHistoryClear) return false;
      a.action = n[c.sub]; a.activityId = c.id; break; }
    case CommandGame: if (c.sub == 1) a.action = "game_accept"; else if (c.sub == 2) a.action = "game_cancel"; else return false; break;
    case CommandLearning:
      if (c.sub == 2) a.action = "learning_reset";
      else if (c.sub <= 1) { a.action = "learning_enable"; strcpy(a.value, c.sub ? "on" : "off"); }
      else return false;
      break;
    case CommandSoundTest: if (c.id >= SoundCueCount) return false; a.action = "sound_test"; strcpy(a.value, SoundCueNames[c.id]); break;
    case CommandIr:
      if (c.sub == IrSend) {
        a.action = "ir_send"; static const char hex[] = "0123456789abcdef"; size_t k = 0; bool lead = true;
        for (int sh = 28; sh >= 0; sh -= 4) { const uint8_t d = uint8_t((c.code >> sh) & 15u); if (d || !lead || sh == 0) { lead = false; a.value[k++] = hex[d]; } }
        a.value[k] = 0;
      } else if ((c.sub == IrLearn || c.sub == IrReplay) && c.name && c.nameLength && c.nameLength <= IrNameMax) {
        a.action = c.sub == IrLearn ? "ir_learn" : "ir_replay"; memcpy(a.value, c.name, c.nameLength); a.value[c.nameLength] = 0;
      } else return false;
      break;
    case CommandExperiencesClear: a.action = "experiences_clear"; break;
    case CommandRest: if (c.sub == RestSleep) a.action = "sleep_now"; else if (c.sub == RestWake) a.action = "wake_now"; else return false; break;
    case CommandPet: a.action = "remote_pet"; break;
    default: return false;
  }
  out = a; return true;
}

// PD-3.3: remote expression/pet/sleep commands at most one per 1.5 s and 12 per minute (no remote spamming the robot).
class RemoteRateLimit {
 public:
  static constexpr uint32_t GapMs = 400, WindowMs = 60000;
  static constexpr uint8_t PerWindow = 40;
  bool allow(uint32_t now) {
    if (count_ && uint32_t(now - lastAt_) < GapMs) return false;
    if (!count_ || uint32_t(now - windowAt_) >= WindowMs) { windowAt_ = now; count_ = 0; }
    if (count_ >= PerWindow) return false;
    ++count_; lastAt_ = now; return true;
  }

 private:
  uint32_t lastAt_ = 0, windowAt_ = 0;
  uint8_t count_ = 0;
};
inline bool remoteRateLimited(uint8_t op) { return op == CommandExpression || op == CommandRest || op == CommandPet; }

// Phone-supplied text is untrusted: strict UTF-8 (no overlongs, surrogates, > U+10FFFF),
// no C0/C1 controls, DEL or line/paragraph separators, and at least one non-space byte.
inline bool validUserText(const uint8_t* p, size_t n) {
  if (!p || !n) return false;
  bool visible = false;
  for (size_t i = 0; i < n;) {
    const uint8_t c = p[i];
    if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; if (c != ' ') visible = true; ++i; continue; }
    size_t len; uint32_t cp;
    if (c >= 0xc2 && c <= 0xdf) { len = 2; cp = c & 0x1f; }
    else if (c >= 0xe0 && c <= 0xef) { len = 3; cp = c & 0x0f; }
    else if (c >= 0xf0 && c <= 0xf4) { len = 4; cp = c & 0x07; }
    else return false;
    if (i + len > n) return false;
    for (size_t k = 1; k < len; ++k) { if ((p[i + k] & 0xc0) != 0x80) return false; cp = (cp << 6) | (p[i + k] & 0x3f); }
    if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10ffff)) ||
        (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp <= 0x9f) || cp == 0x2028 || cp == 0x2029) return false;
    visible = true; i += len;
  }
  return visible;
}

// MemoryAdd: kind u8 (0..5), keyLength u8, valueLength u8, key, value.
struct MemoryAdd { uint8_t kind = 0, keyLength = 0, valueLength = 0; const uint8_t* key = nullptr; const uint8_t* value = nullptr; };
inline bool decodeMemoryAdd(const uint8_t* p, size_t n, MemoryAdd& out) {
  if (!p || n < 5) return false;
  MemoryAdd a; a.kind = p[0]; a.keyLength = p[1]; a.valueLength = p[2];
  if (a.kind >= MemoryCategories || !a.keyLength || a.keyLength > MemoryKeyMax || !a.valueLength || a.valueLength > MemoryValueMax ||
      n != size_t(3) + a.keyLength + a.valueLength) return false;
  a.key = p + 3; a.value = p + 3 + a.keyLength;
  if (!validUserText(a.key, a.keyLength) || !validUserText(a.value, a.valueLength)) return false;
  out = a; return true;
}
// ---- Chat (kinds 25/26) -----------------------------------------------------------------------------------------------------
// ChatSend: textLength u16, text (UTF-8 without control characters). Reply body: turn u32 (never 0).
// ChatPoll: turn u32 (0 = the latest turn), offset u16. Reply body: turn u32, state u8, fail u8, total u16, offset u16, chunkLength u8, chunk.
constexpr size_t ChatTextMax = 280, ChatChunkMax = 180, ChatReplyMax = 640, ChatPollHeader = 11;
enum ChatState : uint8_t { ChatIdle = 0, ChatQueued = 1, ChatThinking = 2, ChatReplying = 3, ChatDone = 4, ChatFailed = 5 };
enum ChatFail : uint8_t { ChatFailNone = 0, ChatFailSend = 1, ChatFailTimeout = 2, ChatFailOffline = 3, ChatFailInterrupted = 4 };
inline bool decodeChatSend(const uint8_t* p, size_t n, const uint8_t*& text, size_t& length) {
  if (!p || n < 3) return false;
  const size_t len = get16(p);
  if (!len || len > ChatTextMax || n != 2 + len || !validUserText(p + 2, len)) return false;
  text = p + 2; length = len; return true;
}
inline bool decodeChatPoll(const uint8_t* p, size_t n, uint32_t& turn, uint16_t& offset) {
  if (!p || n != 6) return false;
  turn = get32(p); offset = get16(p + 4); return offset <= ChatReplyMax;
}
inline size_t encodeChatPoll(uint8_t* out, size_t cap, uint32_t turn, uint8_t state, uint8_t fail, const char* reply, size_t total, size_t offset) {
  if (!out || cap < ChatPollHeader) return 0;
  if (!reply) total = 0;
  if (offset > total) offset = total;
  size_t chunk = total - offset; if (chunk > ChatChunkMax) chunk = ChatChunkMax; if (chunk > cap - ChatPollHeader) chunk = cap - ChatPollHeader;
  put32(out, turn); out[4] = state; out[5] = fail; put16(out + 6, uint16_t(total)); put16(out + 8, uint16_t(offset)); out[10] = uint8_t(chunk);
  if (chunk) memcpy(out + ChatPollHeader, reply + offset, chunk);
  return ChatPollHeader + chunk;
}
// ReminderAdd: schedule u8, minute u16, value u32, textLength u8, text.
// Once: value = UTC epoch seconds, minute 0. Daily: minute 0..1439, value 0. Timer: value = seconds 1..86400, minute 0.
struct ReminderAdd { uint8_t schedule = 0, textLength = 0; uint16_t minute = 0; uint32_t value = 0; const uint8_t* text = nullptr; };
inline bool decodeReminderAdd(const uint8_t* p, size_t n, ReminderAdd& out) {
  if (!p || n < 9) return false;
  ReminderAdd r; r.schedule = p[0]; r.minute = get16(p + 1); r.value = get32(p + 3); r.textLength = p[7];
  if (!r.textLength || r.textLength > ReminderTextMax || n != size_t(8) + r.textLength) return false;
  if (r.schedule == ScheduleOnce) { if (r.minute || !r.value) return false; }
  else if (r.schedule == ScheduleDaily) { if (r.minute > 1439 || r.value) return false; }
  else if (r.schedule == ScheduleTimer) { if (r.minute || !r.value || r.value > TimerSecondsMax) return false; }
  else return false;
  r.text = p + 8;
  if (!validUserText(r.text, r.textLength)) return false;
  out = r; return true;
}
// MemoryClear confirm token: ASCII "MCLR" followed by the session ID (LE).
inline void memoryClearToken(uint32_t session, uint8_t out[8]) { memcpy(out, "MCLR", 4); put32(out + 4, session); }
inline uint8_t decodeMemoryClear(const uint8_t* p, size_t n, uint32_t session) {
  if (!p || n != 8) return BadRequest;
  uint8_t token[8]; memoryClearToken(session, token);
  uint8_t diff = 0; for (unsigned i = 0; i < 8; ++i) diff |= uint8_t(token[i] ^ p[i]);
  return diff ? ConfirmRequired : Ok;
}
inline uint8_t validatePageIndex_(const uint8_t* p, size_t n) { return n == 1 ? (p[0] < MaxPages ? Ok : PageOutOfRange) : BadRequest; }

// List entries (docs/mobile/BLE_SETTINGS_PROTOCOL.md section 14). Strings are raw bytes, never NUL-terminated.
inline size_t encodeMemoryEntry(uint8_t* out, uint32_t id, uint32_t updatedEpoch, uint16_t day, uint8_t kind, uint8_t flags,
                                const char* key, const char* value) {
  const size_t k = strlen(key), v = strlen(value);
  put32(out, id); put32(out + 4, updatedEpoch); put16(out + 8, day); out[10] = kind; out[11] = flags;
  out[12] = uint8_t(k); out[13] = uint8_t(v); memcpy(out + MemoryEntryFixed, key, k); memcpy(out + MemoryEntryFixed + k, value, v);
  return MemoryEntryFixed + k + v;
}
inline size_t encodeReminderEntry(uint8_t* out, uint32_t id, uint8_t schedule, uint16_t minute, uint32_t due, const char* text) {
  const size_t t = strlen(text);
  put32(out, id); out[4] = schedule; put16(out + 5, minute); put32(out + 7, due); out[11] = uint8_t(t);
  memcpy(out + ReminderEntryFixed, text, t); return ReminderEntryFixed + t;
}

// IR profile list (kind 20). Response body (not paged, <= 65 B): count u8 (0..4), then per profile len u8 (1..15) + name.
constexpr size_t IrProfilesMax = 4;
inline size_t encodeIrProfiles(uint8_t* out, size_t capacity, const char* const* names, size_t count) {
  if (!out || !names || count > IrProfilesMax || capacity < 1 + IrProfilesMax * (1 + IrNameMax)) return 0;
  size_t k = 1, listed = 0;
  for (size_t i = 0; i < count; ++i) {
    const size_t n = names[i] ? strlen(names[i]) : 0;
    if (!irNameValid_(reinterpret_cast<const uint8_t*>(names[i]), n)) continue;  // invalid names are never listed
    out[k++] = uint8_t(n); memcpy(out + k, names[i], n); k += n; ++listed;
  }
  out[0] = uint8_t(listed); return k;
}

// External integrations (kinds 21..24), C3 only. Strings are raw bytes, printable ASCII 0x21..0x7e, never NUL-terminated.
// The Home Assistant token and the calendar URL (a private feed URL is a secret, as on the dashboard) are WRITE-ONLY:
// no response carries them, only "configured" flags. Semantic validation (https URL, entity syntax, save) is
// roboexternal's own code on the C3; this layer only checks shape, so malformed input never reaches it.
constexpr uint8_t IntegrationsSchema = 1;
constexpr size_t IntBaseMax = 127, IntEntityMax = 47, IntTokenMax = 191, IntCalendarMax = 255, IntEntities = 3;
constexpr uint8_t IntSetConfig = 1, IntSetToken = 2, IntSetCalendar = 3;
constexpr uint8_t IntFlagWeather = 1, IntFlagToken = 2, IntFlagCalendar = 4;  // Get doc flags; Set config uses IntFlagWeather only
constexpr uint8_t IntActionRefresh = 1, IntActionClear = 2;
constexpr size_t IntConfigDocMax = 1 + 1 + 8 + 1 + IntBaseMax + 1 + IntEntities * (1 + IntEntityMax);  // 283
constexpr size_t IntStatusDocMax = 5 + (4 + 1 + 95) + (1 + IntEntities * (1 + 1 + 4 + 1 + 24)) + (4 + 1 + 219);  // 423
static_assert(ResponseHeader + PageHeader + IntStatusDocMax <= 500 && IntStatusDocMax <= PageData, "integration status fits one page");
static_assert(IntConfigDocMax <= PageData, "integration config fits one page");

inline bool printableNoSpace_(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) if (p[i] < 0x21 || p[i] > 0x7e) return false;
  return true;
}
inline float getFloat_(const uint8_t* p) { const uint32_t v = get32(p); float f; memcpy(&f, &v, 4); return f; }
inline void putFloat_(uint8_t* p, float f) { uint32_t v; memcpy(&v, &f, 4); put32(p, v); }

struct IntegrationsSet {
  uint8_t type = 0, flags = 0;
  float latitude = 0, longitude = 0;
  const uint8_t* base = nullptr; uint8_t baseLength = 0;
  uint8_t entityCount = 0; const uint8_t* entity[IntEntities]{}; uint8_t entityLength[IntEntities]{};
  const uint8_t* text = nullptr; uint8_t textLength = 0;  // token or calendar URL; length 0 clears
};
// type 1 config: 01 flags lat f32 lon f32 baseLen base entCount [len ent]*
// type 2 token:    02 len token (len 0..191, 0 clears)      type 3 calendar: 03 len url (len 0..255, 0 clears)
inline bool decodeIntegrationsSet(const uint8_t* p, size_t n, IntegrationsSet& out) {
  if (!p || n < 2) return false;
  IntegrationsSet s; s.type = p[0];
  if (s.type == IntSetConfig) {
    if (n < 12) return false;
    s.flags = p[1]; s.latitude = getFloat_(p + 2); s.longitude = getFloat_(p + 6); s.baseLength = p[10];
    if ((s.flags & ~IntFlagWeather) || !(s.latitude >= -90.0f && s.latitude <= 90.0f) || !(s.longitude >= -180.0f && s.longitude <= 180.0f) ||
        s.baseLength > IntBaseMax || n < size_t(12) + s.baseLength) return false;
    s.base = p + 11; size_t at = 11 + size_t(s.baseLength);
    s.entityCount = p[at++];
    if (s.entityCount > IntEntities || !printableNoSpace_(s.base, s.baseLength)) return false;
    for (uint8_t i = 0; i < s.entityCount; ++i) {
      if (at >= n) return false;
      const uint8_t len = p[at++];
      if (!len || len > IntEntityMax || at + len > n || !printableNoSpace_(p + at, len)) return false;
      s.entity[i] = p + at; s.entityLength[i] = len; at += len;
    }
    if (at != n) return false;
  } else if (s.type == IntSetToken || s.type == IntSetCalendar) {
    s.textLength = p[1]; s.text = p + 2;
    if (s.textLength > (s.type == IntSetToken ? IntTokenMax : IntCalendarMax) || n != size_t(2) + s.textLength ||
        !printableNoSpace_(s.text, s.textLength)) return false;
  } else {
    return false;
  }
  out = s; return true;
}
// A document in one page: body = page header (8) + doc. The caller wrote the doc at body + PageHeader.
inline size_t sealOnePage(uint8_t* body, size_t docLength) {
  if (!body || !docLength || docLength > PageData) return 0;
  body[0] = 0; body[1] = 1; put16(body + 2, uint16_t(docLength)); put32(body + 4, crc32(body + PageHeader, docLength));
  return PageHeader + docLength;
}
// Get doc: schema u8, flags u8 (IntFlag*), lat f32, lon f32, baseLen u8, base, entCount u8, [len ent]* (<= 283 B). No token, no calendar URL.
inline size_t encodeIntegrationsConfig(uint8_t* out, size_t capacity, bool weather, bool tokenConfigured, bool calendarConfigured,
                                       float latitude, float longitude, const char* base, const char* const* entities) {
  const size_t b = base ? strlen(base) : 0;
  if (!out || capacity < IntConfigDocMax || b > IntBaseMax) return 0;
  out[0] = IntegrationsSchema;
  out[1] = uint8_t((weather ? IntFlagWeather : 0) | (tokenConfigured ? IntFlagToken : 0) | (calendarConfigured ? IntFlagCalendar : 0));
  putFloat_(out + 2, latitude); putFloat_(out + 6, longitude); out[10] = uint8_t(b);
  if (b) memcpy(out + 11, base, b);
  size_t k = 11 + b, count = 0; const size_t countAt = k++;
  for (size_t i = 0; i < IntEntities; ++i) {
    const size_t n = entities && entities[i] ? strlen(entities[i]) : 0;
    if (!n || n > IntEntityMax) continue;
    out[k++] = uint8_t(n); memcpy(out + k, entities[i], n); k += n; ++count;
  }
  out[countAt] = uint8_t(count); return k;
}
struct IntegrationsStatusData {
  bool busy = false, weatherConfigured = false, weatherValid = false, homeAssistantConfigured = false, calendarConfigured = false, calendarValid = false;
  uint8_t weatherError = 0, homeAssistantError = 0, calendarError = 0;
  uint32_t weatherAgeMs = 0, calendarAgeMs = 0;
  const char* weather = ""; const char* calendar = "";
  uint8_t entityCount = 0;
  struct Entity { uint8_t slot = 0; bool valid = false; uint32_t ageMs = 0; const char* state = ""; } entity[IntEntities];
};
// Status doc: schema u8, flags u8 (b0 busy, b1 weather configured, b2 weather valid, b3 HA configured, b4 calendar configured, b5 calendar valid),
// weatherErr, haErr, calErr, then weather: age u32, len u8, text | entCount u8, [slot u8, valid u8, age u32, len u8, text]* | calendar: age u32, len u8, text.
// Error codes are roboexternal's (0 ok .. 8 waiting_for_clock). Texts are the scrubbed cache strings (weather <= 95, state <= 24, calendar <= 219).
// slot = index in roboexternal's entity array, which Set always fills from 0 without gaps (= position in the Get list).
inline size_t encodeIntegrationsStatus(uint8_t* out, size_t capacity, const IntegrationsStatusData& d) {
  if (!out || capacity < IntStatusDocMax || d.entityCount > IntEntities) return 0;
  size_t k = 0;
  auto text = [&](const char* t, size_t max) { size_t n = t ? strlen(t) : 0; if (n > max) n = max; out[k++] = uint8_t(n); if (n) memcpy(out + k, t, n); k += n; };
  out[k++] = IntegrationsSchema;
  out[k++] = uint8_t((d.busy ? 1 : 0) | (d.weatherConfigured ? 2 : 0) | (d.weatherValid ? 4 : 0) | (d.homeAssistantConfigured ? 8 : 0) |
                     (d.calendarConfigured ? 16 : 0) | (d.calendarValid ? 32 : 0));
  out[k++] = d.weatherError; out[k++] = d.homeAssistantError; out[k++] = d.calendarError;
  put32(out + k, d.weatherAgeMs); k += 4; text(d.weather, 95);
  out[k++] = d.entityCount;
  for (uint8_t i = 0; i < d.entityCount; ++i) { out[k++] = d.entity[i].slot; out[k++] = d.entity[i].valid ? 1 : 0; put32(out + k, d.entity[i].ageMs); k += 4; text(d.entity[i].state, 24); }
  put32(out + k, d.calendarAgeMs); k += 4; text(d.calendar, 219);
  return k;
}

// Request shape checks done before any work is queued.
inline uint8_t validateRequest(uint8_t kind, const uint8_t* p, size_t n, uint32_t session) {
  if (n && !p) return BadRequest;
  switch (kind) {
    case KindSettingsGet: return n == 1 && p[0] < MaxPages ? Ok : (n == 1 ? PageOutOfRange : BadRequest);
    case KindSettingsSet: { PageView v; return decodePage(p, n, v) ? Ok : BadRequest; }
    case KindSecretSet: { Secret s; return decodeSecret(p, n, s) ? Ok : BadRequest; }
    case KindStatusSnapshot: return n != 1 ? BadRequest : (p[0] == 0 ? Ok : PageOutOfRange);
    case KindCommand: { Command c; return decodeCommand(p, n, session, c); }
    case KindMemoryList: return n == 5 ? validatePageIndex_(p + 4, 1) : uint8_t(BadRequest);
    case KindMemoryAdd: { MemoryAdd a; return decodeMemoryAdd(p, n, a) ? Ok : BadRequest; }
    case KindMemoryDelete: case KindReminderDelete: return n == 4 && get32(p) ? Ok : BadRequest;
    case KindMemoryClear: return decodeMemoryClear(p, n, session);
    case KindReminderList: return validatePageIndex_(p, n);
    case KindReminderAdd: { ReminderAdd r; return decodeReminderAdd(p, n, r) ? Ok : BadRequest; }
    case KindChatSend: { const uint8_t* t = nullptr; size_t len = 0; return decodeChatSend(p, n, t, len) ? Ok : BadRequest; }
    case KindChatPoll: { uint32_t turn = 0; uint16_t offset = 0; return decodeChatPoll(p, n, turn, offset) ? Ok : BadRequest; }
    case KindIrProfileList: case KindIntegrationsGet: case KindIntegrationsStatus: return n != 1 ? BadRequest : (p[0] == 0 ? Ok : PageOutOfRange);
    case KindIntegrationsSet: { IntegrationsSet s; return decodeIntegrationsSet(p, n, s) ? Ok : BadRequest; }
    case KindIntegrationsAction: return n == 1 && (p[0] == IntActionRefresh || p[0] == IntActionClear) ? Ok : BadRequest;
    default: return Unsupported;
  }
}

struct StatusSnapshot {
  uint32_t c3Version = 0, s3Version = 0, c3UptimeMs = 0, s3UptimeMs = 0;
  uint32_t c3HeapFree = 0, c3HeapMinFree = 0, c3HeapLargest = 0, s3InternalFree = 0, s3InternalMinFree = 0;
  uint8_t linkFlags = 0;  // bit0 connected, bit1 S3 info valid
  uint32_t linkAgeMs = 0, linkRetries = 0, linkCrcErrors = 0, linkDisconnects = 0;
  uint8_t wifiState = 0;  // bit0 station connected, bit1 setup AP active
  int8_t wifiRssi = 0;
  uint8_t bleFlags = 0;     // bit0 ready, bit1 connected, bit2 pairing open, bit3 paired
  uint8_t configFlags = 0;  // bit0 Gemini key configured, bit1..3 Wi-Fi slot 0..2 configured
  uint8_t otaState = OtaIdle, otaError = OtaErrNone, otaPercent = 0;  // schema 2
  uint32_t otaTargetVersion = 0;
  // Schema 3 (live status). S3-derived fields travel in the LinkInfo reply (LinkLiveSize bytes).
  uint8_t wifiActiveSlot = 0;       // 0 none/unknown, 1..3 connected slot (C3)
  uint8_t pomodoroState = 0;        // RobotFeatures::Phase: 0 idle, 1 focus, 2 break, 3 long break, 4 paused
  uint16_t pomodoroRemainingSec = 0;
  uint8_t activityId = 0;           // 0 none, else catalog id 1..13
  uint8_t activityState = 0;        // companion::ActivityState: 0 idle, 1 running, 2 paused, 3 completed, 4 interrupted, 5 cancelled
  uint8_t gameState = 0;            // 0 not offered, 1 offered (eligible), 2 active
  uint8_t learningEnabled = 0;
  uint8_t irProfileCount = 0;       // 0..4
  uint8_t irLearnState = 0;         // 0 idle, 1 listening, 2 saved, 3 failed/timed out
  uint8_t integrationBusy = 0;      // C3 external request in progress
  // Schema 4 (PD-2.4). 0 = unknown. Mood 1 calm, 2 cheerful, 3 curious, 4 affectionate, 5 restless, 6 subdued, 7 sleepy,
  // 8 happy, 9 surprised, 10 sulking; cause = livingeyes::MindCause; sleep = robosleep::State + 1.
  uint8_t moodId = 0, moodCause = 0, sleepState = 0;
};
inline void putLive(uint8_t* o, const StatusSnapshot& s) {
  o[0] = s.pomodoroState; put16(o + 1, s.pomodoroRemainingSec); o[3] = s.activityId; o[4] = s.activityState;
  o[5] = s.gameState; o[6] = s.learningEnabled; o[7] = s.irProfileCount; o[8] = s.irLearnState;
}
inline void getLive(const uint8_t* p, StatusSnapshot& s) {
  s.pomodoroState = p[0]; s.pomodoroRemainingSec = get16(p + 1); s.activityId = p[3]; s.activityState = p[4];
  s.gameState = p[5]; s.learningEnabled = p[6]; s.irProfileCount = p[7]; s.irLearnState = p[8];
}
// CharacterMind::moodName() -> schema-4 mood id (0 for an unknown name).
inline uint8_t moodId(const char* name) {
  static const char* const names[] = {"calm", "cheerful", "curious", "affectionate", "restless", "subdued", "sleepy", "happy", "surprised", "sulking"};
  for (uint8_t i = 0; name && i < sizeof(names) / sizeof(names[0]); ++i)
    if (!strcmp(name, names[i])) return uint8_t(i + 1);
  return 0;
}
inline void putMood(uint8_t* o, const StatusSnapshot& s) { o[0] = s.moodId; o[1] = s.moodCause; o[2] = s.sleepState; }
inline void getMood(const uint8_t* p, StatusSnapshot& s) { s.moodId = p[0]; s.moodCause = p[1]; s.sleepState = p[2]; }
inline size_t encodeStatus(const StatusSnapshot& s, uint8_t* out, size_t capacity) {
  if (!out || capacity < StatusSize) return 0;
  out[0] = StatusSchema;
  put32(out + 1, s.c3Version); put32(out + 5, s.s3Version); put32(out + 9, s.c3UptimeMs); put32(out + 13, s.s3UptimeMs);
  put32(out + 17, s.c3HeapFree); put32(out + 21, s.c3HeapMinFree); put32(out + 25, s.c3HeapLargest);
  put32(out + 29, s.s3InternalFree); put32(out + 33, s.s3InternalMinFree);
  out[37] = s.linkFlags; put32(out + 38, s.linkAgeMs); put32(out + 42, s.linkRetries); put32(out + 46, s.linkCrcErrors); put32(out + 50, s.linkDisconnects);
  out[54] = s.wifiState; out[55] = uint8_t(s.wifiRssi); out[56] = s.bleFlags; out[57] = s.configFlags;
  out[58] = s.otaState; out[59] = s.otaError; out[60] = s.otaPercent > 100 ? 100 : s.otaPercent; put32(out + 61, s.otaTargetVersion);
  out[65] = s.wifiActiveSlot; putLive(out + 66, s); out[75] = s.integrationBusy;
  putMood(out + 76, s);
  return StatusSize;
}
inline bool decodeStatus(const uint8_t* p, size_t n, StatusSnapshot& s) {
  // Schema 1 is exactly 58 bytes, schema 2 at least 65, schema 3 at least 76, schema >= 4 at least 79; later schemas may append bytes.
  if (!p || !p[0] || (p[0] == 1 ? n != StatusSizeV1 : n < (p[0] == 2 ? StatusSizeV2 : p[0] == 3 ? StatusSizeV3 : StatusSize))) return false;
  s = StatusSnapshot{};
  s.c3Version = get32(p + 1); s.s3Version = get32(p + 5); s.c3UptimeMs = get32(p + 9); s.s3UptimeMs = get32(p + 13);
  s.c3HeapFree = get32(p + 17); s.c3HeapMinFree = get32(p + 21); s.c3HeapLargest = get32(p + 25);
  s.s3InternalFree = get32(p + 29); s.s3InternalMinFree = get32(p + 33);
  s.linkFlags = p[37]; s.linkAgeMs = get32(p + 38); s.linkRetries = get32(p + 42); s.linkCrcErrors = get32(p + 46); s.linkDisconnects = get32(p + 50);
  s.wifiState = p[54]; s.wifiRssi = int8_t(p[55]); s.bleFlags = p[56]; s.configFlags = p[57];
  if (p[0] >= 2) { s.otaState = p[58]; s.otaError = p[59]; s.otaPercent = p[60]; s.otaTargetVersion = get32(p + 61); }
  if (p[0] >= 3) { s.wifiActiveSlot = p[65]; getLive(p + 66, s); s.integrationBusy = p[75]; }
  if (p[0] >= 4) getMood(p + 76, s);
  return true;
}
// Status is a one-page document so it can grow without a new kind.
inline size_t encodeStatusPage(const StatusSnapshot& s, uint8_t* out, size_t capacity) {
  uint8_t doc[StatusSize];
  if (!encodeStatus(s, doc, sizeof(doc))) return 0;
  return encodePage(doc, sizeof(doc), 0, out, capacity);
}

// BLE ota-start (C3 main loop). Ok means "scheduled": the caller sends the Ok
// response first and starts the GitHub pair flow only once due() is true, i.e.
// the response left the exchange slot and OtaStartDelayMs passed (the flow
// stops BLE before its TLS work).
constexpr uint32_t OtaStartDelayMs = 1500, OtaStartMaxWaitMs = 5000;
struct OtaStartGate { bool admitted = false, updateBusy = false, linkConnected = false, wifiConnected = false, clockSet = false; };
class OtaStartScheduler {
 public:
  uint8_t request(const OtaStartGate& g, uint32_t now) {
    if (!g.admitted) return NotAuthorized;
    if (pending_ || g.updateBusy) return UpdateBusy;
    if (!g.linkConnected) return RobotUnavailable;
    if (!g.wifiConnected) return NetworkUnavailable;
    if (!g.clockSet) return TimeNotSet;
    pending_ = true; at_ = now; return Ok;
  }
  bool due(uint32_t now, bool responseSent) {
    if (!pending_) return false;
    const uint32_t elapsed = now - at_;
    if (elapsed < OtaStartDelayMs || (!responseSent && elapsed < OtaStartMaxWaitMs)) return false;
    pending_ = false; return true;
  }
  bool pending() const { return pending_; }
 private:
  bool pending_ = false; uint32_t at_ = 0;
};

// S3-side staging of a SettingsSet document. Pages must arrive in order with
// identical header fields; the document is exposed only after length and
// CRC32 verify. Any error discards the whole stage.
class PageStager {
 public:
  void attach(uint8_t* buffer, size_t capacity) { buffer_ = buffer; capacity_ = capacity; reset(); }
  uint8_t accept(uint32_t stream, const uint8_t* p, size_t n, uint32_t now, bool& complete) {
    complete = false;
    PageView v;
    if (!buffer_ || capacity_ < DocMax) { reset(); return NoMemory; }
    if (!decodePage(p, n, v)) { reset(); return BadRequest; }
    if (active_ && uint32_t(now - lastAt_) > StageTimeoutMs) reset();
    if (v.page == 0) { reset(); active_ = true; stream_ = stream; length_ = v.length; count_ = v.count; crc_ = v.crc; next_ = 0; }
    if (!active_ || stream != stream_ || v.page != next_ || v.length != length_ || v.count != count_ || v.crc != crc_) {
      reset(); return SequenceError;
    }
    memcpy(buffer_ + size_t(v.page) * PageData, v.data, v.dataSize);
    received_ += v.dataSize; ++next_; lastAt_ = now;
    if (next_ < count_) return Ok;
    if (received_ != length_ || crc32(buffer_, length_) != crc_) { reset(); return CrcMismatch; }
    complete = true; ready_ = true; active_ = false;
    return Ok;
  }
  const uint8_t* document() const { return ready_ ? buffer_ : nullptr; }
  size_t size() const { return ready_ ? length_ : 0; }
  bool active() const { return active_; }
  void reset() {
    if (buffer_ && capacity_) memset(buffer_, 0, capacity_ < DocMax ? capacity_ : DocMax);
    active_ = ready_ = false; stream_ = 0; length_ = 0; received_ = 0; count_ = next_ = 0; crc_ = 0; lastAt_ = 0;
  }
 private:
  uint8_t* buffer_ = nullptr; size_t capacity_ = 0;
  bool active_ = false, ready_ = false; uint32_t stream_ = 0, crc_ = 0, lastAt_ = 0;
  size_t length_ = 0, received_ = 0; uint8_t count_ = 0, next_ = 0;
};

// The C3's only settings buffer (500 B). Ownership moves Idle -> Working
// (claimed by the BLE service, owned by the gateway handler) -> Ready (owned
// by the BLE service until sent) -> Idle. The request is stored at
// RequestOffset so UART headers can be prepended in place, and the S3 reply
// (id, op, status, body) lands at offset 0, where complete() rewrites the
// first 6 bytes into the BLE response header (counter, kind, status).
class Exchange {
 public:
  enum State : uint8_t { Idle = 0, Working = 1, Ready = 2 };
  static constexpr size_t Capacity = 500, RequestOffset = 12;
  static constexpr size_t RequestMax = PageHeader + PageData;  // 488, largest valid request
  bool claim(uint32_t session, uint32_t counter, uint8_t kind, const uint8_t* p, size_t n) {
    if (!session || !counter || !isRequestKind(kind) || n > RequestMax || (n && !p)) return false;
    uint8_t expected = Idle;
    if (!state_.compare_exchange_strong(expected, Working, std::memory_order_acq_rel)) return false;
    memset(buffer_, 0, sizeof(buffer_));
    session_ = session; counter_ = counter; kind_ = kind; requestSize_ = uint16_t(n); responseSize_ = 0;
    if (n) memcpy(buffer_ + RequestOffset, p, n);
    return true;
  }
  uint8_t* buffer() { return buffer_; }
  const uint8_t* request() const { return buffer_ + RequestOffset; }
  size_t requestSize() const { return requestSize_; }
  uint8_t kind() const { return kind_; }
  uint32_t session() const { return session_; }
  uint32_t counter() const { return counter_; }
  State state() const { return State(state_.load(std::memory_order_acquire)); }
  // Body (if any) is already at buffer()+ResponseHeader. A secret response
  // never carries a body and everything past the header is wiped.
  bool complete(uint8_t status, size_t bodySize) {
    if (state() != Working) return false;
    if (kind_ == KindSecretSet || bodySize > Capacity - ResponseHeader) bodySize = 0;
    writeResponseHeader(buffer_, counter_, kind_, status);
    memset(buffer_ + ResponseHeader + bodySize, 0, Capacity - ResponseHeader - bodySize);
    responseSize_ = uint16_t(ResponseHeader + bodySize); requestSize_ = 0;
    state_.store(Ready, std::memory_order_release);
    return true;
  }
  // Response for the live session only; a stale one is wiped and dropped.
  bool take(uint32_t activeSession, const uint8_t*& p, size_t& n) {
    p = nullptr; n = 0;
    if (state() != Ready) return false;
    if (!activeSession || activeSession != session_) { release(); return false; }
    p = buffer_; n = responseSize_; return true;
  }
  void dropStale(uint32_t activeSession) { if (state() == Ready && (!activeSession || activeSession != session_)) release(); }
  void release() {
    memset(buffer_, 0, sizeof(buffer_)); session_ = counter_ = 0; kind_ = 0; requestSize_ = responseSize_ = 0;
    state_.store(Idle, std::memory_order_release);
  }
 private:
  std::atomic<uint8_t> state_{Idle};
  uint32_t session_ = 0, counter_ = 0; uint8_t kind_ = 0; uint16_t requestSize_ = 0, responseSize_ = 0;
  uint8_t buffer_[Capacity]{};
};
static_assert(Exchange::RequestOffset + Exchange::RequestMax <= Exchange::Capacity, "request fits");
static_assert(ResponseHeader + PageHeader + PageData <= Exchange::Capacity, "page response fits");
static_assert(Exchange::RequestOffset >= 9, "UART put header fits before the request");
}  // namespace PhoneSettingsProtocol
