#include "../DashboardJsonWriter.h"
#include <cassert>
#include <cstring>

int main() {
  char buffer[24];
  DashboardJsonWriter writer(buffer, sizeof(buffer));
  assert(writer.appendf("{\"a\":%u", 1u));
  const size_t prefixSize = writer.size();
  assert(writer.remaining() == sizeof(buffer) - prefixSize);
  assert(!writer.appendf(",\"tooLarge\":\"%s\"", "this fragment does not fit"));
  assert(writer.failed());
  assert(writer.remaining() == 0);
  assert(writer.size() == prefixSize);
  assert(std::strcmp(buffer, "{\"a\":1") == 0);
  assert(!writer.appendf(",\"later\":true"));

  char full[96];
  DashboardJsonWriter complete(full, sizeof(full));
  assert(complete.appendf("{\"activity\":{\"state\":\"%s\"}}", "interrupted"));
  assert(std::strcmp(full, "{\"activity\":{\"state\":\"interrupted\"}}") == 0);
  assert(!complete.failed());
  assert(complete.remaining() == sizeof(full) - std::strlen(full));
  return 0;
}
