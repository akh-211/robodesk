#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_ota_ops.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <ctype.h>
#include <strings.h>
#include "FirmwareOtaKey.h"

// Downloads only signed application images from this project's public GitHub Release.
// The worker runs outside the dashboard loop so HTTPS stalls cannot starve audio/UI work.
class GitHubOtaUpdate {
 public:
  typedef bool (*ControlCallback)(void*, bool);
  enum State : uint8_t { Idle, Checking, UpToDate, UpdateAvailable, Downloading, Rebooting, Failed };

  void begin(ControlCallback control, void* context) { control_ = control; context_ = context; }

  bool startCheck() { return startTask(false); }
  bool startInstall() {
    if (state() != UpdateAvailable) return false;
    return startTask(true);
  }

  State state() const { return state_; }
  uint32_t availableVersion() const { return availableVersion_; }
  void statusText(char* out, size_t size) const {
    if (!size) return;
    portENTER_CRITICAL(&mux_);
    strlcpy(out, message_, size);
    portEXIT_CRITICAL(&mux_);
  }

 private:
  static constexpr size_t kManifestMax = 1024;
  static constexpr size_t kImageMax = 0x300000;
  static const char* manifestUrl() { return "https://github.com/akh-211/robodesk/releases/latest/download/manifest.txt"; }
  static const char* imageUrl(uint32_t version, char* out, size_t size) {
    snprintf(out, size, "https://github.com/akh-211/robodesk/releases/download/v%u/RoboDeskSonicCharacter.ino.bin", unsigned(version));
    return out;
  }

  struct Manifest {
    uint32_t version = 0;
    size_t size = 0;
    char sha256[65] = {0};
    char signature[170] = {0};
    char url[160] = {0};
  };
  struct HttpSink {
    GitHubOtaUpdate* self;
    char* buffer;
    size_t capacity;
    size_t used;
    bool overflow;
    bool redirectOverflow;
    char redirectUrl[1024];
    bool image;
  };
  struct ActiveImage {
    mbedtls_sha256_context hash;
    size_t bytes = 0;
    size_t expected = 0;
    bool hashStarted = false;
    bool writeFailed = false;
  };

  ControlCallback control_ = nullptr;
  void* context_ = nullptr;
  volatile State state_ = Idle;
  volatile uint32_t availableVersion_ = 0;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  char message_[112] = "Check for an update when RoboDesk is online.";
  volatile bool taskActive_ = false;
  bool installTask_ = false;
  ActiveImage image_;

  void setStatus(State state, const char* message, uint32_t version = 0) {
    portENTER_CRITICAL(&mux_);
    state_ = state;
    if (state == Checking || state == Downloading || state == Failed) availableVersion_ = 0;
    else if (version) availableVersion_ = version;
    strlcpy(message_, message ? message : "", sizeof(message_));
    portEXIT_CRITICAL(&mux_);
  }

  bool startTask(bool install) {
    portENTER_CRITICAL(&mux_);
    if (taskActive_ || state_ == Checking || state_ == Downloading || state_ == Rebooting) { portEXIT_CRITICAL(&mux_); return false; }
    taskActive_ = true;
    installTask_ = install;
    state_ = install ? Downloading : Checking;
    availableVersion_ = 0;
    strlcpy(message_, install ? "Preparing secure download..." : "Checking GitHub Releases...", sizeof(message_));
    portEXIT_CRITICAL(&mux_);
    if (xTaskCreate(taskEntry, "robodesk_ota", 9216, this, 1, nullptr) != pdPASS) {
      portENTER_CRITICAL(&mux_);
      taskActive_ = false;
      portEXIT_CRITICAL(&mux_);
      setStatus(Failed, "Could not start the update worker.");
      return false;
    }
    return true;
  }

  static void taskEntry(void* argument) {
    GitHubOtaUpdate* self = static_cast<GitHubOtaUpdate*>(argument);
    self->runTask();
    portENTER_CRITICAL(&self->mux_);
    self->taskActive_ = false;
    portEXIT_CRITICAL(&self->mux_);
    vTaskDelete(nullptr);
  }

  static esp_err_t httpEvent(esp_http_client_event_t* event) {
    if (!event->user_data) return ESP_OK;
    HttpSink* sink = static_cast<HttpSink*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        strlen(event->header_key) == 8 && strncasecmp(event->header_key, "Location", 8) == 0) {
      const size_t length = strlen(event->header_value);
      if (length >= sizeof(sink->redirectUrl)) sink->redirectOverflow = true;
      else strlcpy(sink->redirectUrl, event->header_value, sizeof(sink->redirectUrl));
      return ESP_OK;
    }
    if (event->event_id != HTTP_EVENT_ON_DATA || !event->data || event->data_len <= 0 ||
        esp_http_client_get_status_code(event->client) != 200) return ESP_OK;
    const size_t amount = size_t(event->data_len);
    if (sink->image) {
      ActiveImage& active = sink->self->image_;
      if (active.writeFailed || active.bytes + amount > active.expected) { active.writeFailed = true; return ESP_FAIL; }
      const size_t written = Update.write(static_cast<uint8_t*>(event->data), amount);
      if (written != amount || mbedtls_sha256_update(&active.hash, static_cast<uint8_t*>(event->data), amount) != 0) {
        active.writeFailed = true;
        return ESP_FAIL;
      }
      active.bytes += written;
      if ((active.bytes & 0x7fff) < amount) vTaskDelay(1);
      return ESP_OK;
    }
    if (amount > sink->capacity - sink->used) { sink->overflow = true; return ESP_FAIL; }
    memcpy(sink->buffer + sink->used, event->data, amount);
    sink->used += amount;
    return ESP_OK;
  }

  static esp_err_t performGet(const char* url, HttpSink* sink, int* status) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 12000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.max_redirection_count = 5;
    config.max_authorization_retries = -1;
    config.keep_alive_enable = false;
    config.buffer_size = 1024;
    config.user_data = sink;
    config.event_handler = httpEvent;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t result = ESP_OK;
    int code = 0;
    for (unsigned redirects = 0; redirects <= 5; ++redirects) {
      sink->redirectUrl[0] = 0;
      sink->redirectOverflow = false;
      result = esp_http_client_perform(client);
      code = esp_http_client_get_status_code(client);
      if (result != ESP_OK || (code != 301 && code != 302 && code != 303 && code != 307 && code != 308)) break;
      if (redirects == 5 || sink->redirectOverflow || !allowedRedirect(sink->redirectUrl) || esp_http_client_set_redirection(client) != ESP_OK) {
        result = ESP_FAIL;
        break;
      }
    }
    if (status) *status = code;
    esp_http_client_cleanup(client);
    return result;
  }

  static bool allowedRedirect(const char* url) {
    if (!url || strncmp(url, "https://", 8) != 0) return false;
    const char* authority = url + 8;
    const char* path = strchr(authority, '/');
    if (!path || path == authority || size_t(path - authority) >= 96 || memchr(authority, '@', size_t(path - authority))) return false;
    char host[96];
    const size_t length = size_t(path - authority);
    memcpy(host, authority, length); host[length] = 0;
    if (length > 4 && strcmp(host + length - 4, ":443") == 0) host[length - 4] = 0;
    for (char* c = host; *c; ++c) *c = char(tolower(static_cast<unsigned char>(*c)));
    return strcmp(host, "github.com") == 0 || strcmp(host, "release-assets.githubusercontent.com") == 0 ||
        strcmp(host, "objects.githubusercontent.com") == 0;
  }

  static bool parseUnsigned(const char* text, uint32_t* value) {
    if (!text || !*text || !value) return false;
    uint64_t parsed = 0;
    for (const char* p = text; *p; ++p) {
      if (*p < '0' || *p > '9') return false;
      parsed = parsed * 10 + unsigned(*p - '0');
      if (parsed > 0x7fffffffULL) return false;
    }
    if (!parsed) return false;
    *value = uint32_t(parsed);
    return true;
  }

  static bool parseManifest(char* data, size_t size, Manifest* manifest) {
    if (!data || !size || !manifest || size >= kManifestMax) return false;
    data[size] = 0;
    bool haveFormat = false, haveBoard = false, haveVersion = false, haveSize = false;
    bool haveSha = false, haveImage = false, haveSig = false, haveUrl = false;
    char* cursor = data;
    while (*cursor) {
      char* end = strchr(cursor, '\n');
      if (end) *end = 0;
      size_t len = strlen(cursor);
      if (len && cursor[len - 1] == '\r') cursor[--len] = 0;
      char* equals = strchr(cursor, '=');
      if (!equals) return false;
      *equals++ = 0;
      if (!strcmp(cursor, "format")) { if (strcmp(equals, "robodesk-ota-v1")) return false; haveFormat = true; }
      else if (!strcmp(cursor, "board")) { if (strcmp(equals, "esp32s3")) return false; haveBoard = true; }
      else if (!strcmp(cursor, "version")) { if (!parseUnsigned(equals, &manifest->version)) return false; haveVersion = true; }
      else if (!strcmp(cursor, "size")) { uint32_t n; if (!parseUnsigned(equals, &n) || n > kImageMax) return false; manifest->size = n; haveSize = true; }
      else if (!strcmp(cursor, "sha256")) { if (strlen(equals) != 64) return false; for (size_t i=0;i<64;++i) if (!isxdigit(static_cast<unsigned char>(equals[i]))) return false; strlcpy(manifest->sha256, equals, sizeof(manifest->sha256)); haveSha = true; }
      else if (!strcmp(cursor, "image")) { if (strcmp(equals, "RoboDeskSonicCharacter.ino.bin")) return false; haveImage = true; }
      else if (!strcmp(cursor, "signature")) { if (strlen(equals) < 130 || strlen(equals) >= sizeof(manifest->signature)) return false; strlcpy(manifest->signature, equals, sizeof(manifest->signature)); haveSig = true; }
      else if (!strcmp(cursor, "url")) { if (strlen(equals) >= sizeof(manifest->url)) return false; strlcpy(manifest->url, equals, sizeof(manifest->url)); haveUrl = true; }
      else return false;
      if (!end) break;
      cursor = end + 1;
    }
    char expectedUrl[sizeof(manifest->url)];
    imageUrl(manifest->version, expectedUrl, sizeof(expectedUrl));
    return haveFormat && haveBoard && haveVersion && haveSize && manifest->size > 0 && haveSha && haveImage && haveSig && haveUrl && strcmp(manifest->url, expectedUrl) == 0;
  }

  bool currentVersion(uint32_t* current) {
    Preferences prefs;
    if (!prefs.begin("robodesk_ota", true)) return false;
    *current = prefs.getUInt("version", ROBODESK_OTA_INITIAL_VERSION);
    prefs.end();
    return true;
  }

  bool fetchManifest(Manifest* manifest, const char* failure) {
    char data[kManifestMax + 1] = {0};
    HttpSink sink = {this, data, kManifestMax, 0, false, false, {0}, false};
    int status = 0;
    const esp_err_t result = performGet(manifestUrl(), &sink, &status);
    if (result != ESP_OK || status != 200 || sink.overflow || !parseManifest(data, sink.used, manifest)) {
      setStatus(Failed, failure);
      return false;
    }
    return true;
  }

  static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool decodeSignature(const char* encoded, uint32_t version, uint8_t* signature, size_t* length) {
    const char* separator = strchr(encoded, ':');
    if (!separator || separator == encoded || size_t(separator - encoded) > 10) return false;
    char versionText[12] = {0};
    memcpy(versionText, encoded, size_t(separator - encoded));
    uint32_t signedVersion = 0;
    if (!parseUnsigned(versionText, &signedVersion) || signedVersion != version) return false;
    const char* hex = separator + 1;
    const size_t hexLength = strlen(hex);
    if (hexLength < 128 || hexLength > 160 || (hexLength & 1)) return false;
    *length = hexLength / 2;
    for (size_t i=0;i<*length;++i) {
      const int hi=hexNibble(hex[2*i]), lo=hexNibble(hex[2*i+1]);
      if (hi < 0 || lo < 0) return false;
      signature[i]=uint8_t((hi<<4)|lo);
    }
    return true;
  }

  bool verifySignature(const Manifest& manifest, const uint8_t digest[32]) {
    uint8_t signature[80] = {0}; size_t signatureLength = 0;
    if (!decodeSignature(manifest.signature, manifest.version, signature, &signatureLength)) return false;
    char message[144];
    const int count = snprintf(message, sizeof(message), "RoboDeskSonicCharacter|ESP32-S3|%u|%u|%s", unsigned(manifest.version), unsigned(manifest.size), manifest.sha256);
    if (count <= 0 || size_t(count) >= sizeof(message)) return false;
    uint8_t messageDigest[32] = {0};
    mbedtls_sha256_context hash; mbedtls_sha256_init(&hash);
    const bool hashOk = mbedtls_sha256_starts(&hash, 0) == 0 && mbedtls_sha256_update(&hash, reinterpret_cast<const uint8_t*>(message), size_t(count)) == 0 && mbedtls_sha256_finish(&hash, messageDigest) == 0;
    mbedtls_sha256_free(&hash);
    if (!hashOk) return false;
    mbedtls_pk_context key; mbedtls_pk_init(&key);
    const int parsed = mbedtls_pk_parse_public_key(&key, reinterpret_cast<const uint8_t*>(ROBODESK_OTA_PUBLIC_KEY_PEM), sizeof(ROBODESK_OTA_PUBLIC_KEY_PEM));
    const int verified = parsed == 0 ? mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, messageDigest, 0, signature, signatureLength) : parsed;
    mbedtls_pk_free(&key);
    (void)digest;
    return verified == 0;
  }

  bool digestMatches(const uint8_t digest[32], const char* expected) {
    static const char digits[] = "0123456789abcdef";
    char actual[65];
    for (size_t i=0;i<32;++i) { actual[2*i]=digits[digest[i]>>4]; actual[2*i+1]=digits[digest[i]&15]; }
    actual[64]=0;
    for (size_t i=0;i<64;++i) if (tolower(static_cast<unsigned char>(actual[i])) != tolower(static_cast<unsigned char>(expected[i]))) return false;
    return true;
  }

  bool downloadImage(const Manifest& manifest) {
    memset(&image_, 0, sizeof(image_));
    image_.expected = manifest.size;
    mbedtls_sha256_init(&image_.hash);
    image_.hashStarted = mbedtls_sha256_starts(&image_.hash, 0) == 0;
    if (!image_.hashStarted || !Update.begin(manifest.size, U_FLASH)) { mbedtls_sha256_free(&image_.hash); return false; }
    HttpSink sink = {this, nullptr, 0, 0, false, false, {0}, true};
    int status = 0;
    const esp_err_t result = performGet(manifest.url, &sink, &status);
    uint8_t digest[32] = {0};
    const bool hashOk = mbedtls_sha256_finish(&image_.hash, digest) == 0;
    mbedtls_sha256_free(&image_.hash);
    const bool valid = result == ESP_OK && status == 200 && !image_.writeFailed && image_.bytes == manifest.size && hashOk && digestMatches(digest, manifest.sha256) && verifySignature(manifest, digest);
    if (!valid) { Update.abort(); return false; }
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    Preferences prefs;
    if (!target || !prefs.begin("robodesk_ota", false)) { Update.abort(); return false; }
    const bool staged = prefs.putUInt("pending", manifest.version) == sizeof(uint32_t) && prefs.putUInt("pend_off", target->address) == sizeof(uint32_t);
    if (!staged) { prefs.remove("pending"); prefs.remove("pend_off"); }
    prefs.end();
    if (!staged || !Update.end(true) || !Update.isFinished()) {
      if (staged) { prefs.begin("robodesk_ota", false); prefs.remove("pending"); prefs.remove("pend_off"); prefs.end(); }
      Update.abort();
      return false;
    }
    return true;
  }

  void runTask() {
    if (WiFi.status() != WL_CONNECTED) { setStatus(Failed, "RoboDesk is offline. Connect it to Wi-Fi with internet access."); return; }
    bool held = false;
    if (installTask_) {
      if (!control_ || !control_(context_, true)) { setStatus(Failed, "Robot is busy. Try again when audio is idle."); return; }
      held = true;
    }
    Manifest manifest;
    if (!fetchManifest(&manifest, "Could not securely read the GitHub release manifest.")) { if (held) control_(context_, false); return; }
    uint32_t current = 0;
    if (!currentVersion(&current)) { setStatus(Failed, "Could not read the installed firmware version."); if (held) control_(context_, false); return; }
    if (!installTask_) {
      if (manifest.version > current) setStatus(UpdateAvailable, "A signed firmware update is available.", manifest.version);
      else setStatus(UpToDate, "RoboDesk already has the latest firmware.", manifest.version);
      return;
    }
    if (manifest.version <= current) { setStatus(UpToDate, "No newer release is available.", manifest.version); control_(context_, false); return; }
    setStatus(Downloading, "Downloading and verifying signed firmware...", manifest.version);
    if (!downloadImage(manifest)) { setStatus(Failed, "Download or signature verification failed; current firmware is unchanged."); control_(context_, false); return; }
    setStatus(Rebooting, "Firmware verified. RoboDesk is restarting...");
    vTaskDelay(pdMS_TO_TICKS(900));
    ESP.restart();
  }
};
