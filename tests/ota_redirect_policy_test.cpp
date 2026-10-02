#include "../OtaRedirectPolicy.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  const char* allowed[] = {
      "https://github.com/akh-211/robodesk/releases/download/v2/RoboDeskSonicCharacter.ino.bin",
      "https://release-assets.githubusercontent.com/github-production-release-asset/123/abcdef?sp=r&sv=2018-11-09&sig=a%2Fb%3D",
      "https://objects.githubusercontent.com/github-production-release-asset/123/abcdef?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Signature=abcd",
      "https://GITHUB.COM:443/akh-211/robodesk/releases/download/v2/firmware.bin",
      "https://Release-Assets.GitHubUserContent.com:443/asset?sig=abc",
  };
  for (const char* url : allowed) assert(roboAllowedOtaRedirect(url));

  const char* denied[] = {
      "http://github.com/owner/repo/releases/download/v2/firmware.bin",
      "HTTPS://github.com/owner/repo/releases/download/v2/firmware.bin",
      "//github.com/owner/repo/releases/download/v2/firmware.bin",
      "https://github.com",
      "https:///owner/repo/firmware.bin",
      "https://github.com.evil.example/firmware.bin",
      "https://release-assets.githubusercontent.com.evil.example/asset",
      "https://objects.githubusercontent.com.evil.example/asset",
      "https://evil.example/github.com/firmware.bin",
      "https://github.com:444/firmware.bin",
      "https://github.com:443:443/firmware.bin",
      "https://user@github.com/firmware.bin",
      "https://github.com@evil.example/firmware.bin",
      "https://user:password@release-assets.githubusercontent.com/asset",
      "https://github.com/firmware.bin#fragment",
      "https://github.com/firmware.bin?sig=abc#fragment",
      "https://github.com/firmware.bin?sig=abc def",
      "https://github.com/firmware.bin\r\nHost:evil.example",
      "https://github.com/firmware.bin\t",
      "https://github.com\\@evil.example/firmware.bin",
      "https://github.com/firmware.bin\\evil",
      "https://github.com/firmware.bin\x7f",
      "https://github.com/firmware.bin\x80",
  };
  for (const char* url : denied) assert(!roboAllowedOtaRedirect(url));
  assert(!roboAllowedOtaRedirect(nullptr));
  assert(!roboAllowedOtaRedirect(""));

  const std::string prefix = "https://github.com/";
  const std::string maximum = prefix + std::string(1023 - prefix.size(), 'a');
  assert(maximum.size() == 1023);
  assert(roboAllowedOtaRedirect(maximum.c_str()));
  assert(!roboAllowedOtaRedirect((maximum + "a").c_str()));

  char location[1024] = {0};
  bool seen = false;
  assert(roboRecordOtaRedirectLocation(allowed[0], location, sizeof(location), &seen));
  assert(seen && std::string(location) == allowed[0]);
  assert(!roboRecordOtaRedirectLocation("https://evil.example/asset", location, sizeof(location), &seen));
  assert(std::string(location) == allowed[0]);
  seen = false;
  assert(roboRecordOtaRedirectLocation(allowed[1], location, sizeof(location), &seen));
  assert(std::string(location) == allowed[1]);
  seen = false;
  assert(roboRecordOtaRedirectLocation(maximum.c_str(), location, sizeof(location), &seen));
  seen = false;
  assert(!roboRecordOtaRedirectLocation((maximum + "a").c_str(), location, sizeof(location), &seen));
  assert(seen && std::string(location) == maximum);

  std::cout << "PASS: signed GitHub OTA redirect allowlist and single Location per hop\n";
}
