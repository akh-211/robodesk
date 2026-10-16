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
  static constexpr uint32_t QueueTimeoutMs = 45000, ReplyTimeoutMs = 90000, WipeAfterMs = 300000, TextQuietMs = 2500, NoTextMs = 8000;

  // Local command hook: returns true when `text` was a robot command and already executed; `reply` is then the
  // answer shown in the chat. Commands run without Gemini (also offline) and never reach the model.
  using CommandHook = bool (*)(const char* text, char* reply, size_t cap);
  void setCommandHook(CommandHook hook) { hook_ = hook; }

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
    reply_[0] = 0; replyLen_ = 0; completing_ = false; audioAt_ = 0; fail_ = ps::ChatFailNone; state_ = ps::ChatQueued;
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
  // Gemini sends turnComplete before (or between) the output transcription pieces: keep the turn open until the text has been
  // quiet for TextQuietMs, or give up after NoTextMs when no text came at all.
  void complete(uint32_t now) { if (active()) { completing_ = true; completeAt_ = now; } }
  void interrupted(uint32_t now) { if (active()) finish_(replyLen_ ? ps::ChatDone : ps::ChatFailed, replyLen_ ? ps::ChatFailNone : ps::ChatFailInterrupted, now); }
  // A queued turn survives a Gemini reconnect (it is still sent once ready); a running one ends.
  void offline(uint32_t now) { if (state_ == ps::ChatThinking || state_ == ps::ChatReplying) finish_(replyLen_ ? ps::ChatDone : ps::ChatFailed, replyLen_ ? ps::ChatFailNone : ps::ChatFailOffline, now); }

  // `now` may be older than a timestamp set by a callback later in the same loop pass: a negative age counts as 0
  // (unsigned wrap-around used to wipe a just-finished turn, so the app polled state "idle").
  static uint32_t age_(uint32_t now, uint32_t then) { const int32_t d = int32_t(now - then); return d > 0 ? uint32_t(d) : 0; }
  // Gemini's transcription is paced by the audio it speaks: while the robot is still playing the answer more text can follow turnComplete.
  void audioActivity(bool playing, uint32_t now) { if (playing) audioAt_ = now; }
  void tick(uint32_t now) {
    if (completing_ && active()) {
      const uint32_t quiet = age_(now, replyChangedAt_) < age_(now, audioAt_) ? age_(now, replyChangedAt_) : age_(now, audioAt_);
      if (replyLen_ ? quiet > TextQuietMs && age_(now, completeAt_) > TextQuietMs : age_(now, completeAt_) > NoTextMs && age_(now, audioAt_) > TextQuietMs)
        finish_(replyLen_ ? ps::ChatDone : ps::ChatFailed, replyLen_ ? ps::ChatFailNone : ps::ChatFailSend, now);
    }
    if ((state_ == ps::ChatQueued || state_ == ps::ChatThinking) && age_(now, startedAt_) > QueueTimeoutMs) finish_(ps::ChatFailed, ps::ChatFailTimeout, now);
    else if (state_ == ps::ChatReplying && age_(now, startedAt_) > ReplyTimeoutMs) finish_(ps::ChatDone, ps::ChatFailNone, now);
    else if ((state_ == ps::ChatDone || state_ == ps::ChatFailed) && age_(now, finishedAt_) > WipeAfterMs) wipe();
  }
  uint32_t wipes() const { return wipes_; }
  uint32_t finishedAt() const { return finishedAt_; }
  void wipe() { ++wipes_; memset(pending_, 0, sizeof(pending_)); memset(reply_, 0, sizeof(reply_)); replyLen_ = 0; state_ = ps::ChatIdle; fail_ = ps::ChatFailNone; }

  // op = LinkChatSend / LinkChatPoll; p/n = the BLE payload (the session u32 is already stripped). `ready` = robot can take a turn now.
  uint8_t handle(uint8_t op, const uint8_t* p, size_t n, uint32_t now, bool ready, uint8_t* out, size_t cap, size_t& body) {
    body = 0;
    if (op == ps::LinkChatSend) {
      const uint8_t* text = nullptr; size_t len = 0;
      if (!ps::decodeChatSend(p, n, text, len)) return ps::BadRequest;
      if (hook_ && !active()) {
        static char line[ps::ChatTextMax + 1], answer[ps::ChatReplyMax + 1];  // static: this runs deep in the loop task stack
        memcpy(line, text, len); line[len] = 0; answer[0] = 0;
        if (hook_(line, answer, sizeof(answer))) {
          if (cap < 4) return ps::NoMemory;
          pending_[0] = 0; replyLen_ = strnlen(answer, ps::ChatReplyMax); memcpy(reply_, answer, replyLen_); reply_[replyLen_] = 0;
          if (++turn_ == 0) turn_ = 1;
          startedAt_ = finishedAt_ = replyChangedAt_ = now; state_ = replyLen_ ? ps::ChatDone : ps::ChatFailed; fail_ = replyLen_ ? ps::ChatFailNone : ps::ChatFailSend;
          ps::put32(out, turn_); body = 4; return ps::Ok;
        }
      }
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
  void finish_(uint8_t state, uint8_t why, uint32_t now) { completing_ = false; state_ = state; fail_ = why; finishedAt_ = now; memset(pending_, 0, sizeof(pending_)); }
  char pending_[ps::ChatTextMax + 1] = {};
  char reply_[ps::ChatReplyMax + 1] = {};
  size_t replyLen_ = 0;
  CommandHook hook_ = nullptr;
  uint32_t wipes_ = 0, completeAt_ = 0, audioAt_ = 0;
  bool completing_ = false;
  uint32_t turn_ = 0, startedAt_ = 0, finishedAt_ = 0, replyChangedAt_ = 0;
  uint8_t state_ = ps::ChatIdle, fail_ = ps::ChatFailNone;
};
}  // namespace phone_chat_detail
using PhoneChat = phone_chat_detail::PhoneChat;
