#pragma once

#include <ctype.h>
#include <stddef.h>
#include <string.h>

inline bool roboRecordOtaRedirectLocation(const char* value, char* destination, size_t capacity, bool* seen) {
  if (!value || !destination || !capacity || !seen || *seen) return false;
  *seen = true;
  const size_t length = strlen(value);
  if (length >= capacity) return false;
  memcpy(destination, value, length + 1);
  return true;
}

inline bool roboAllowedOtaRedirect(const char* url) {
  if (!url || strncmp(url, "https://", 8) != 0) return false;
  const size_t length = strlen(url);
  if (length < 10 || length >= 1024) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    if (c <= 0x20 || c >= 0x7f || c == '\\' || c == '#') return false;
  }

  const char* authority = url + 8;
  const char* path = strchr(authority, '/');
  if (!path || path == authority || size_t(path - authority) >= 96 ||
      memchr(authority, '@', size_t(path - authority))) return false;

  char host[96];
  const size_t hostLength = size_t(path - authority);
  memcpy(host, authority, hostLength);
  host[hostLength] = 0;
  if (hostLength > 4 && strcmp(host + hostLength - 4, ":443") == 0) host[hostLength - 4] = 0;
  for (char* c = host; *c; ++c) *c = char(tolower(static_cast<unsigned char>(*c)));
  return strcmp(host, "github.com") == 0 ||
         strcmp(host, "release-assets.githubusercontent.com") == 0 ||
         strcmp(host, "objects.githubusercontent.com") == 0;
}
