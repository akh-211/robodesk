#include <cassert>
#include <cstdio>
#include <cstring>
#include "../PhoneChat.h"

namespace ps = PhoneSettingsProtocol;

static size_t sendPayload(uint8_t* out, const char* text) {
  const size_t n = strlen(text); ps::put16(out, uint16_t(n)); memcpy(out + 2, text, n); return n + 2;
}
static size_t pollPayload(uint8_t* out, uint32_t turn, uint16_t offset) { ps::put32(out, turn); ps::put16(out + 4, offset); return 6; }

int main() {
  uint8_t req[400], out[400]; size_t body = 0;
  // Request shape checks
  assert(ps::isRequestKind(ps::KindChatSend) && ps::isRequestKind(ps::KindChatPoll) && !ps::isRequestKind(27));
  assert(ps::isForwardedKind(ps::KindChatSend) && ps::isForwardedKind(ps::KindChatPoll));
  assert(ps::linkOpForKind(ps::KindChatSend) == ps::LinkChatSend && ps::linkOpForKind(ps::KindChatPoll) == ps::LinkChatPoll);
  assert(ps::linkOpForKind(ps::KindMemoryList) == ps::LinkMemoryList && ps::linkOpForKind(ps::KindReminderDelete) == ps::LinkReminderDelete);
  size_t n = sendPayload(req, "Halo robot, apa kabar?");
  assert(ps::validateRequest(ps::KindChatSend, req, n, 1) == ps::Ok);
  assert(ps::validateRequest(ps::KindChatSend, req, n - 1, 1) == ps::BadRequest);        // length mismatch
  assert(ps::validateRequest(ps::KindChatSend, req, 2, 1) == ps::BadRequest);            // no text
  uint8_t ctl[8] = {3, 0, 'a', '\n', 'b'}; assert(ps::validateRequest(ps::KindChatSend, ctl, 5, 1) == ps::BadRequest);  // control character
  uint8_t big[2 + ps::ChatTextMax + 1]; ps::put16(big, uint16_t(ps::ChatTextMax + 1)); memset(big + 2, 'x', ps::ChatTextMax + 1);
  assert(ps::validateRequest(ps::KindChatSend, big, sizeof(big), 1) == ps::BadRequest);  // too long
  ps::put16(big, uint16_t(ps::ChatTextMax)); assert(ps::validateRequest(ps::KindChatSend, big, 2 + ps::ChatTextMax, 1) == ps::Ok);
  uint8_t poll[6]; pollPayload(poll, 0, 0); assert(ps::validateRequest(ps::KindChatPoll, poll, 6, 1) == ps::Ok);
  assert(ps::validateRequest(ps::KindChatPoll, poll, 5, 1) == ps::BadRequest);
  pollPayload(poll, 0, uint16_t(ps::ChatReplyMax + 1)); assert(ps::validateRequest(ps::KindChatPoll, poll, 6, 1) == ps::BadRequest);

  PhoneChat chat;
  // Not ready -> RobotUnavailable, nothing queued
  assert(chat.handle(ps::LinkChatSend, req, n, 1000, false, out, sizeof(out), body) == ps::RobotUnavailable && !chat.active());
  // Send -> turn id, Queued
  assert(chat.handle(ps::LinkChatSend, req, n, 1000, true, out, sizeof(out), body) == ps::Ok && body == 4);
  const uint32_t turn = ps::get32(out); assert(turn == 1 && chat.state() == ps::ChatQueued);
  // A second send while running is Busy
  assert(chat.handle(ps::LinkChatSend, req, n, 1100, true, out, sizeof(out), body) == ps::Busy);
  // Poll before the S3 took the text: queued, empty reply
  size_t pn = pollPayload(poll, turn, 0);
  assert(chat.handle(ps::LinkChatPoll, poll, pn, 1200, true, out, sizeof(out), body) == ps::Ok && out[4] == ps::ChatQueued && ps::get16(out + 6) == 0);
  // The loop takes the text exactly once
  char text[ps::ChatTextMax + 1];
  assert(chat.takePending(text, sizeof(text)) && !strcmp(text, "Halo robot, apa kabar?") && chat.state() == ps::ChatThinking && !chat.takePending(text, sizeof(text)));
  // Reply arrives in pieces; poll windows it in chunks
  chat.appendReply("Halo! ", 1500); assert(chat.state() == ps::ChatReplying);
  static char long_[400]; memset(long_, 'a', 300); long_[300] = 0; chat.appendReply(long_, 1600);
  pollPayload(poll, 0, 0);
  assert(chat.handle(ps::LinkChatPoll, poll, 6, 1700, true, out, sizeof(out), body) == ps::Ok);
  assert(out[4] == ps::ChatReplying && ps::get16(out + 6) == 306 && out[10] == ps::ChatChunkMax && body == ps::ChatPollHeader + ps::ChatChunkMax);
  pollPayload(poll, turn, ps::ChatChunkMax);
  assert(chat.handle(ps::LinkChatPoll, poll, 6, 1700, true, out, sizeof(out), body) == ps::Ok && ps::get16(out + 8) == ps::ChatChunkMax && out[10] == 306 - ps::ChatChunkMax);
  chat.complete(2000); assert(chat.state() == ps::ChatDone && !chat.active());
  // Reply length is capped
  assert(chat.handle(ps::LinkChatSend, req, n, 3000, true, out, sizeof(out), body) == ps::Ok && ps::get32(out) == 2);
  chat.takePending(text, sizeof(text)); for (int i = 0; i < 10; ++i) chat.appendReply(long_, 3100); assert(chat.replyLength() == ps::ChatReplyMax);
  chat.interrupted(3200); assert(chat.state() == ps::ChatDone);  // partial reply is kept as done
  // Unknown turn polls as idle
  pollPayload(poll, 99, 0);
  assert(chat.handle(ps::LinkChatPoll, poll, 6, 3300, true, out, sizeof(out), body) == ps::Ok && out[4] == ps::ChatIdle && ps::get16(out + 6) == 0);
  // Timeouts: queued too long fails; failed turn without text stays failed
  assert(chat.handle(ps::LinkChatSend, req, n, 10000, true, out, sizeof(out), body) == ps::Ok);
  chat.tick(10000 + PhoneChat::QueueTimeoutMs + 1); assert(chat.state() == ps::ChatFailed && chat.fail() == ps::ChatFailTimeout);
  assert(chat.handle(ps::LinkChatSend, req, n, 60000, true, out, sizeof(out), body) == ps::Ok);
  chat.takePending(text, sizeof(text)); chat.sendFailed(ps::ChatFailSend, 60100); assert(chat.state() == ps::ChatFailed && chat.fail() == ps::ChatFailSend);
  // Data is wiped after the retention window
  chat.tick(60100 + PhoneChat::WipeAfterMs + 1); assert(chat.state() == ps::ChatIdle && chat.replyLength() == 0 && chat.reply()[0] == 0);
  // complete() with no reply is a failure, offline() too
  assert(chat.handle(ps::LinkChatSend, req, n, 500000, true, out, sizeof(out), body) == ps::Ok);
  chat.takePending(text, sizeof(text)); chat.complete(500100); assert(chat.state() == ps::ChatFailed);
  assert(chat.handle(ps::LinkChatSend, req, n, 600000, true, out, sizeof(out), body) == ps::Ok);
  chat.offline(600050); assert(chat.state() == ps::ChatQueued);  // not sent yet: survives a reconnect
  chat.takePending(text, sizeof(text)); chat.offline(600100); assert(chat.state() == ps::ChatFailed && chat.fail() == ps::ChatFailOffline);
  puts("PASS: chat protocol shape, one turn at a time, chunked reply, caps, timeouts and RAM wipe");
  return 0;
}
