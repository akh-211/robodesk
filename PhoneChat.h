#pragma once
#include <string.h>
#include "PhoneSettingsProtocol.h"

// S3 side of the BLE chat kinds (docs/mobile/BLE_SETTINGS_PROTOCOL.md section 18).
// One chat turn at a time. The phone text is untrusted data: it is validated by
// PhoneSettingsProtocol::decodeChatSend, handed to Gemini as plain user text and never
// interpreted. Text and reply live in RAM only and are wiped a few minutes after the turn ends.
namespace phone_chat_detail {
namespace ps = PhoneSettingsProtocol;
class PhoneChat {
 public:
  static constexpr uint32_t QueueTimeoutMs = 45000, ReplyTimeoutMs = 90000, WipeAfterMs = 300000;

  uint32_t turn() const { return turn_; }
  uint8_t state() const { return state_; }
  uint8_t fail() const { return fail_; }
  bool active() const { return state_ == ps::ChatQueued || state_ == ps::ChatThinking || state_ == ps::ChatReplying; }
  const char* reply() const { return reply_; }
  size_t replyLength() const { return replyLen_; }
  uint32_t replyChangedAt() const { return replyChangedAt_; }

  // Queues one turn. Busy while the previous turn is still running.
  uint8_t submit(const uint8_t* text, size_t n, uint32_t now, uint32_t& turnOut) {
    tick(now);
    if (!text || !n || n > ps::ChatTextMax) return ps::BadRequest;
    if (active()) return ps::Busy;
    memcpy(pending_, text, n); pending_[n] = 0;
    reply_[0] = 0; replyLen_ = 0; fail_ = ps::ChatFailNone; state_ = ps::ChatQueued;
    if (++turn_ == 0) turn_ = 1;
    startedAt_ = now; replyChangedAt_ = now; turnOut = turn_;
    return ps::Ok;
  }

  // The S3 loop takes the text once Gemini is ready to receive it (Queued -> Thinking).
  bool takePending(char* out, size_t cap) {
    if (state_ != ps::ChatQueued || !out || cap <= strlen(pending_)) return false;
    strcpy(out, pending_); memset(pending_, 0, sizeof(pending_)); state_ = ps::ChatThinking; return true;
  }
  void sendFailed(uint8_t why, uint32_t now) { if (active()) finish_(ps::ChatFailed, why, now); }
  // Gemini output transcription arrives in pieces.
  void appendReply(const char* text, uint32_t now) {
    if (!active() || !text) return;
    state_ = ps::ChatReplying;
    for (; *text && replyLen_ < ps::ChatReplyMax; ++text) reply_[replyLen_++] = *text;
    reply_[replyLen_] = 0; replyChangedAt_ = now;
  }
  void complete(uint32_t now) { if (active()) finish_(replyLen_ ? ps::ChatDone : ps::ChatFailed, replyLen_ ? ps::ChatFailNone : ps::ChatFailSend, now); }
  void interrupted(uint32_t now) { if (active()) finish_(replyLen_ ? ps::ChatDone : ps::ChatFailed, replyLen_ ? ps::ChatFailNone : ps::ChatFailInterrupted, now); }
  // A queued turn survives a Gemini reconnect (it is still sent once ready); a running one ends.
  void offline(uint32_t now) { if (state_ == ps::ChatThinking || state_ == ps::ChatReplying) finish_(replyLen_ ? ps::ChatDone : ps::ChatFailed, replyLen_ ? ps::ChatFailNone : ps::ChatFailOffline, now); }

  void tick(uint32_t now) {
    if ((state_ == ps::ChatQueued || state_ == ps::ChatThinking) && uint32_t(now - startedAt_) > QueueTimeoutMs) finish_(ps::ChatFailed, ps::ChatFailTimeout, now);
    else if (state_ == ps::ChatReplying && uint32_t(now - startedAt_) > ReplyTimeoutMs) finish_(ps::ChatDone, ps::ChatFailNone, now);
    else if ((state_ == ps::ChatDone || state_ == ps::ChatFailed) && uint32_t(now - finishedAt_) > WipeAfterMs) wipe();
  }
  void wipe() { memset(pending_, 0, sizeof(pending_)); memset(reply_, 0, sizeof(reply_)); replyLen_ = 0; state_ = ps::ChatIdle; fail_ = ps::ChatFailNone; }

  // op = LinkChatSend / LinkChatPoll; p/n = the BLE payload (the session u32 is already stripped). `ready` = robot can take a turn now.
  uint8_t handle(uint8_t op, const uint8_t* p, size_t n, uint32_t now, bool ready, uint8_t* out, size_t cap, size_t& body) {
    body = 0;
    if (op == ps::LinkChatSend) {
      const uint8_t* text = nullptr; size_t len = 0;
      if (!ps::decodeChatSend(p, n, text, len)) return ps::BadRequest;
      if (!ready) return ps::RobotUnavailable;
      if (cap < 4) return ps::NoMemory;
      uint32_t id = 0; const uint8_t status = submit(text, len, now, id);
      if (status != ps::Ok) return status;
      ps::put32(out, id); body = 4; return ps::Ok;
    }
    if (op == ps::LinkChatPoll) {
      uint32_t asked = 0; uint16_t offset = 0;
      if (!ps::decodeChatPoll(p, n, asked, offset)) return ps::BadRequest;
      tick(now);
      const bool known = turn_ != 0 && (asked == 0 || asked == turn_) && state_ != ps::ChatIdle;
      body = known ? ps::encodeChatPoll(out, cap, turn_, state_, fail_, reply_, replyLen_, offset)
                   : ps::encodeChatPoll(out, cap, asked ? asked : turn_, ps::ChatIdle, ps::ChatFailNone, nullptr, 0, 0);
      return body ? ps::Ok : ps::NoMemory;
    }
    return ps::Unsupported;
  }

 private:
  void finish_(uint8_t state, uint8_t why, uint32_t now) { state_ = state; fail_ = why; finishedAt_ = now; memset(pending_, 0, sizeof(pending_)); }
  char pending_[ps::ChatTextMax + 1] = {};
  char reply_[ps::ChatReplyMax + 1] = {};
  size_t replyLen_ = 0;
  uint32_t turn_ = 0, startedAt_ = 0, finishedAt_ = 0, replyChangedAt_ = 0;
  uint8_t state_ = ps::ChatIdle, fail_ = ps::ChatFailNone;
};
}  // namespace phone_chat_detail
using PhoneChat = phone_chat_detail::PhoneChat;
