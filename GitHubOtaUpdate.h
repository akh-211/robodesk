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
#include <time.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include "RoboLog.h"
#include "FirmwareOtaKey.h"
#include "OtaRedirectPolicy.h"
#include "OtaWorkflowPolicy.h"

// Downloads only signed application images from this project's public GitHub Release.
// The worker runs outside the dashboard loop so HTTPS stalls cannot starve audio/UI work.
class GitHubOtaUpdate {
 public:
  typedef bool (*ControlCallback)(void*, bool);
  enum State : uint8_t { Idle, Checking, UpToDate, UpdateAvailable, Downloading, Rebooting, Failed };
  using StartResult = RoboOtaStartResult;
  enum class Preparation : uint8_t { Pending, Ready, Rejected };
  using PreparationCallback = Preparation (*)(void*,bool);
  struct Snapshot {
    State state;
    uint32_t version;
    bool active;
    StartResult startResult;
    char stage[24];
    char failure[32];
    char message[112];
  };

  void begin(ControlCallback control, void* context, PreparationCallback prepare=nullptr) { control_ = control; context_ = context; prepare_=prepare; }

  // Runs on the dashboard/Arduino loop; preparation never blocks that loop.
  void service() {
    portENTER_CRITICAL(&mux_);
    const bool pending=launchPending_;
    const bool first=!preparationBegun_;
    if(pending)preparationBegun_=true;
    portEXIT_CRITICAL(&mux_);
    if(!pending)return;
    const Preparation result=prepare_(context_,first);
    const uint32_t now=millis();
    const auto decision=roboOtaPreparationDecision(result==Preparation::Rejected,result==Preparation::Ready,uint32_t(now-preparationStarted_),readySince_!=0,uint32_t(now-readySince_));
    if(decision==RoboOtaPreparationStep::Rejected){failPreparation("robot_busy","Robot is busy or WakeNet is armed.");return;}
    if(decision==RoboOtaPreparationStep::TimedOut){failPreparation("prepare_timeout","Existing network worker did not stop in time.");return;}
    if(result==Preparation::Pending){
      readySince_=0;
      return;
    }
    // Give the idle task time to reclaim the cancelled connection worker's stack.
    if(!readySince_){readySince_=now?now:1;return;}
    if(decision!=RoboOtaPreparationStep::Launch)return;
    portENTER_CRITICAL(&mux_);launchPending_=false;portEXIT_CRITICAL(&mux_);
    createWorker();
  }

  bool startCheck() { return requestCheck() == StartResult::Accepted; }
  bool startInstall() { return requestInstall() == StartResult::Accepted; }
  StartResult requestCheck() { return startTask(Operation::Check, 0, 0, "dashboard"); }
  StartResult requestInstall() { return startTask(Operation::Install, 0, 0, "dashboard"); }
  StartResult requestDiagnostics(uint32_t minimumExclusive, long timezoneSeconds) { return startTask(Operation::Diagnostics, minimumExclusive, timezoneSeconds, "serial"); }
  Snapshot snapshot() const {
    Snapshot out={};
    portENTER_CRITICAL(&mux_);
    out.state=state_;out.version=availableVersion_;out.active=taskActive_;out.startResult=lastStartResult_;
    strlcpy(out.stage,stage_,sizeof(out.stage));strlcpy(out.failure,failure_,sizeof(out.failure));strlcpy(out.message,message_,sizeof(out.message));
    portEXIT_CRITICAL(&mux_);
    return out;
  }
  State state() const { portENTER_CRITICAL(&mux_);const State value=state_;portEXIT_CRITICAL(&mux_);return value; }
  uint32_t availableVersion() const { portENTER_CRITICAL(&mux_);const uint32_t value=availableVersion_;portEXIT_CRITICAL(&mux_);return value; }
  bool active() const { portENTER_CRITICAL(&mux_);const bool value=taskActive_;portEXIT_CRITICAL(&mux_);return value; }
  bool trackedVersion(uint32_t* value) { return value && currentVersion(value); }
  void statusText(char* out, size_t size) const {
    if (!size) return;
    portENTER_CRITICAL(&mux_);
    strlcpy(out, message_, size);
    portEXIT_CRITICAL(&mux_);
  }

 private:
  enum class Operation : uint8_t { Check, Install, Diagnostics };
  static constexpr size_t kManifestMax = 1024;
  static constexpr size_t kImageMax = 0x300000;
  // This USB diagnostic build is based on v6; the NVS record may still be older.
  static constexpr uint32_t kMinimumExclusiveVersion = 6;
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
    bool redirectSeen;
    char redirectUrl[1024];
    bool image;
    bool redirectRejected;
    uint8_t redirectCount;
    int transportErrno;
    int tlsError;
    int tlsFlags;
    esp_err_t tlsReadResult;
  };
  struct ActiveImage {
    mbedtls_sha256_context hash;
    size_t bytes = 0;
    size_t expected = 0;
    bool hashStarted = false;
    bool writeFailed = false;
  };

  ControlCallback control_ = nullptr;
  PreparationCallback prepare_ = nullptr;
  void* context_ = nullptr;
  volatile State state_ = Idle;
  volatile uint32_t availableVersion_ = 0;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  char message_[112] = "Check for an update when RoboDesk is online.";
  volatile bool taskActive_ = false;
  StartResult lastStartResult_ = StartResult::Accepted;
  char stage_[24] = "idle";
  char failure_[32] = "none";
  Operation operation_ = Operation::Check;
  uint32_t minimumExclusive_ = 0;
  long timezoneSeconds_ = 0;
  bool launchPending_ = false;
  bool preparationBegun_ = false;
  uint32_t preparationStarted_ = 0;
  uint32_t readySince_ = 0;
  ActiveImage image_;

  void setStatus(State state, const char* message, uint32_t version = 0, const char* failure = "none") {
    portENTER_CRITICAL(&mux_);
    state_ = state;
    if (state == Checking || state == Downloading || state == Failed) availableVersion_ = 0;
    else if (version) availableVersion_ = version;
    strlcpy(message_, message ? message : "", sizeof(message_));
    strlcpy(failure_, failure, sizeof(failure_));
    portEXIT_CRITICAL(&mux_);
  }

  void setStage(const char* stage) {
    portENTER_CRITICAL(&mux_);strlcpy(stage_,stage,sizeof(stage_));portEXIT_CRITICAL(&mux_);
    RoboLog.printf("LEV,OTA,STAGE,name=%s,internal=%u,largest=%u,psram=%u,stack_free=%u\n",stage,unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  }

  StartResult startTask(Operation operation, uint32_t minimumExclusive, long timezoneSeconds, const char* origin) {
    const bool install=operation==Operation::Install;
    RoboLog.printf("LEV,OTA,START_REQUEST,origin=%s,operation=%u,internal=%u,largest=%u,psram=%u\n",origin,unsigned(operation),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    portENTER_CRITICAL(&mux_);
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    constexpr bool rollback=true;
#else
    constexpr bool rollback=false;
#endif
    const StartResult decision=roboOtaStartDecision(rollback,taskActive_,state_==Checking||state_==Downloading||state_==Rebooting,install,state_==UpdateAvailable);
    lastStartResult_=decision;
    if(decision!=StartResult::Accepted){const State state=state_;const bool active=taskActive_;portEXIT_CRITICAL(&mux_);RoboLog.printf("LEV,OTA,START_REJECT,reason=%s,state=%u,active=%u\n",roboOtaStartResultName(decision),unsigned(state),unsigned(active));return decision;}
    taskActive_ = true;
    operation_ = operation;
    minimumExclusive_ = minimumExclusive>kMinimumExclusiveVersion?minimumExclusive:kMinimumExclusiveVersion;
    timezoneSeconds_ = timezoneSeconds;
    launchPending_=prepare_!=nullptr;
    preparationBegun_=false;preparationStarted_=millis();readySince_=0;
    state_ = install ? Downloading : Checking;
    availableVersion_ = 0;
    strlcpy(stage_,prepare_?"preparing":"starting",sizeof(stage_));strlcpy(failure_,"none",sizeof(failure_));
    strlcpy(message_, install ? "Preparing secure download..." : "Checking GitHub Releases...", sizeof(message_));
    portEXIT_CRITICAL(&mux_);
    return prepare_?StartResult::Accepted:createWorker();
  }

  void logResult() const {
    const Snapshot result=snapshot();
    RoboLog.printf("LEV,OTA,RESULT,state=%u,version=%lu,stage=%s,failure=%s,message=%s\n",unsigned(result.state),static_cast<unsigned long>(result.version),result.stage,result.failure,result.message);
  }

  void failPreparation(const char* failure,const char* message) {
    setStatus(Failed,message,0,failure);
    logResult();
    if(operation_==Operation::Diagnostics)RoboLog.println("LEV,NETDIAG,DONE");
    portENTER_CRITICAL(&mux_);taskActive_=false;launchPending_=false;portEXIT_CRITICAL(&mux_);
  }

  StartResult createWorker() {
    const bool diagnostics=operation_==Operation::Diagnostics;
    RoboLog.printf("LEV,OTA,WORKER_ALLOC,internal=%u,largest=%u,psram=%u\n",unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    const BaseType_t created=xTaskCreate(taskEntry, "robodesk_ota", 9216, this, 1, nullptr);
    RoboLog.printf("LEV,OTA,TASK_CREATE,result=%d,internal=%u,largest=%u\n",int(created),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    if (created != pdPASS) {
      portENTER_CRITICAL(&mux_);
      state_=Failed;lastStartResult_=StartResult::NoMemory;
      strlcpy(failure_,"task_alloc",sizeof(failure_));
      strlcpy(message_,"Not enough internal RAM to start the update worker.",sizeof(message_));
      portEXIT_CRITICAL(&mux_);
      logResult();
      if(diagnostics)RoboLog.println("LEV,NETDIAG,DONE");
      portENTER_CRITICAL(&mux_);taskActive_=false;portEXIT_CRITICAL(&mux_);
      return StartResult::NoMemory;
    }
    return StartResult::Accepted;
  }

  static void taskEntry(void* argument) {
    GitHubOtaUpdate* self = static_cast<GitHubOtaUpdate*>(argument);
    RoboLog.println("LEV,OTA,WORKER,begin=1");
    self->runTask();
    self->logResult();
    RoboLog.printf("LEV,OTA,WORKER,end=1,stack_free=%u\n",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    if(self->operation_==Operation::Diagnostics)RoboLog.println("LEV,NETDIAG,DONE");
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
      if (!roboRecordOtaRedirectLocation(event->header_value, sink->redirectUrl,
                                         sizeof(sink->redirectUrl), &sink->redirectSeen)) sink->redirectOverflow = true;
      return ESP_OK;
    }
    if (event->event_id == HTTP_EVENT_REDIRECT) {
      if (sink->redirectOverflow || !roboAllowedOtaRedirect(sink->redirectUrl) ||
          ++sink->redirectCount > 5 || esp_http_client_set_redirection(event->client) != ESP_OK) {
        sink->redirectRejected = true;
        return ESP_OK;
      }
      sink->redirectUrl[0] = 0;
      sink->redirectSeen = false;
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
    // Allowed signed asset URLs can approach 1024 bytes. The request line
    // also needs the GET prefix, HTTP version and CRLF beyond the URL bytes.
    config.buffer_size_tx = 2048;
    config.user_data = sink;
    config.event_handler = httpEvent;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    sink->redirectUrl[0] = 0;
    sink->redirectOverflow = false;
    sink->redirectSeen = false;
    sink->redirectRejected = false;
    sink->redirectCount = 0;
    sink->transportErrno = 0;
    sink->tlsError = 0;
    sink->tlsFlags = 0;
    sink->tlsReadResult = ESP_FAIL;
    const esp_err_t result = esp_http_client_perform(client);
    const int code = esp_http_client_get_status_code(client);
    sink->transportErrno = esp_http_client_get_errno(client);
    sink->tlsReadResult = esp_http_client_get_and_clear_last_tls_error(client, &sink->tlsError, &sink->tlsFlags);
    if (status) *status = code;
    RoboLog.printf("LEV,OTA,HTTP,result=%d,http=%d,errno=%d,tls_api=%d,tls_code=%d,tls_flags=%d,redirects=%u,rejected=%u\n",
                   int(result), code, sink->transportErrno, int(sink->tlsReadResult), sink->tlsError,
                   sink->tlsFlags, unsigned(sink->redirectCount), unsigned(sink->redirectRejected));
    esp_http_client_cleanup(client);
    return sink->redirectRejected ? ESP_FAIL : result;
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

  bool fetchManifest(Manifest* manifest) {
    setStage("manifest");
    char data[kManifestMax + 1] = {0};
    HttpSink sink = {this, data, kManifestMax, 0, false, false, false, {0}, false, false, 0};
    int status = 0;
    const esp_err_t result = performGet(manifestUrl(), &sink, &status);
    if (sink.redirectRejected) { setStatus(Failed, "GitHub redirected to an untrusted or invalid address.",0,"redirect"); return false; }
    if (result != ESP_OK) { setStatus(Failed, "Secure GitHub connection failed. Check internet, DNS and clock.",0,result==ESP_ERR_NO_MEM?"http_alloc":(sink.tlsError||sink.tlsFlags?"tls":"transport")); return false; }
    if (status != 200) { setStatus(Failed, "GitHub manifest is unavailable. Retry later.",0,"http_status"); return false; }
    if (sink.overflow || !parseManifest(data, sink.used, manifest)) {
      setStatus(Failed, "GitHub manifest is too large or invalid.",0,"manifest_format");
      return false;
    }
    setStage("signature");
    if (!verifySignature(*manifest, nullptr)) {
      setStatus(Failed, "GitHub manifest signature is invalid.",0,"signature");
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
    setStage("image");
    memset(&image_, 0, sizeof(image_));
    image_.expected = manifest.size;
    mbedtls_sha256_init(&image_.hash);
    image_.hashStarted = mbedtls_sha256_starts(&image_.hash, 0) == 0;
    if (!image_.hashStarted || !Update.begin(manifest.size, U_FLASH)) { mbedtls_sha256_free(&image_.hash);setStatus(Failed,"Could not prepare the inactive firmware slot.",0,"image_begin"); return false; }
    HttpSink sink = {this, nullptr, 0, 0, false, false, false, {0}, true, false, 0};
    int status = 0;
    const esp_err_t result = performGet(manifest.url, &sink, &status);
    uint8_t digest[32] = {0};
    const bool hashOk = mbedtls_sha256_finish(&image_.hash, digest) == 0;
    mbedtls_sha256_free(&image_.hash);
    const char* failure=nullptr;
    if(sink.redirectRejected)failure="redirect";
    else if(result!=ESP_OK)failure=sink.tlsError||sink.tlsFlags?"tls":"download";
    else if(status!=200)failure="http_status";
    else if(image_.writeFailed)failure="image_write";
    else if(image_.bytes!=manifest.size)failure="image_size";
    else if(!hashOk||!digestMatches(digest,manifest.sha256))failure="image_hash";
    else if(!verifySignature(manifest,digest))failure="signature";
    if(failure){Update.abort();setStatus(Failed,"Image verification failed; the running firmware is unchanged.",0,failure);return false;}
    setStage("version_stage");
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    Preferences prefs;
    if (!target || !prefs.begin("robodesk_ota", false)) { Update.abort();setStatus(Failed,"Could not stage firmware version.",0,"version_stage"); return false; }
    const bool staged = prefs.putUInt("pending", manifest.version) == sizeof(uint32_t) && prefs.putUInt("pend_off", target->address) == sizeof(uint32_t);
    if (!staged) { prefs.remove("pending"); prefs.remove("pend_off"); }
    prefs.end();
    if (!staged || !Update.end(true) || !Update.isFinished()) {
      if (staged) { prefs.begin("robodesk_ota", false); prefs.remove("pending"); prefs.remove("pend_off"); prefs.end(); }
      Update.abort();
      setStatus(Failed,"Could not commit the verified firmware.",0,!staged?"version_stage":"image_commit");
      return false;
    }
    return true;
  }

  bool diagnoseNetwork() {
    RoboLog.printf("LEV,NETDIAG,START,wifi=%u,rssi=%d\n",unsigned(WiFi.status()==WL_CONNECTED),WiFi.status()==WL_CONNECTED?WiFi.RSSI():0);
    if(WiFi.status()!=WL_CONNECTED){setStatus(Failed,"Connect RoboDesk to Wi-Fi.",0,"wifi");return false;}
    setStage("ntp");
    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTime(timezoneSeconds_,0,"pool.ntp.org","time.google.com");
    const uint32_t started=millis();
    bool fresh=false;
    time_t epoch=time(nullptr);
    sntp_sync_status_t status=SNTP_SYNC_STATUS_RESET;
    while(uint32_t(millis()-started)<30000u){
      status=esp_sntp_get_sync_status();epoch=time(nullptr);
      if(status==SNTP_SYNC_STATUS_COMPLETED&&epoch>=1700000000){fresh=true;break;}
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    RoboLog.printf("LEV,NETDIAG,NTP,fresh=%u,status=%d,epoch=%lu\n",unsigned(fresh),int(status),static_cast<unsigned long>(epoch));
    setStage("dns");IPAddress ip;
    const bool dns=WiFi.hostByName("github.com",ip)==1;
    RoboLog.printf("LEV,NETDIAG,DNS,ok=%u,ip=%s\n",unsigned(dns),dns?ip.toString().c_str():"none");
    setStage("tcp");bool tcpOk=false;
    if(dns){WiFiClient tcp;tcpOk=tcp.connect(ip,443,8000);tcp.stop();}
    RoboLog.printf("LEV,NETDIAG,TCP,ok=%u,port=443\n",unsigned(tcpOk));
    if(!fresh){setStatus(Failed,"Fresh NTP synchronization timed out.",0,"clock");return false;}
    if(!dns){setStatus(Failed,"GitHub DNS lookup failed.",0,"dns");return false;}
    if(!tcpOk){setStatus(Failed,"GitHub TCP port 443 is unreachable.",0,"tcp");return false;}
    return true;
  }

  void runTask() {
    const bool diagnostics=operation_==Operation::Diagnostics;
    const bool install=operation_==Operation::Install;
    if(diagnostics&&!diagnoseNetwork())return;
    if (WiFi.status() != WL_CONNECTED) { setStatus(Failed, "Connect RoboDesk to router Wi-Fi with internet access.",0,"wifi"); return; }
    if (time(nullptr) < 1700000000) { setStatus(Failed, "Clock not synchronized. Wait for Wi-Fi time sync and retry.",0,"clock"); return; }
    bool held = false;
    if (install) {
      setStage("quiesce");
      if (!control_ || !control_(context_, true)) {
        setStatus(Failed, "Robot is busy or WakeNet is armed. Select Touch-to-talk, save and reboot before retrying.",0,"robot_busy");
        return;
      }
      held = true;
    }
    Manifest manifest;
    if (!fetchManifest(&manifest)) { if (held) control_(context_, false); return; }
    uint32_t current = 0;
    if (!currentVersion(&current)) { setStatus(Failed, "Could not read the installed firmware version.",0,"version_read"); if (held) control_(context_, false); return; }
    RoboLog.printf("LEV,OTA,MANIFEST,signature=valid,version=%lu,installed=%lu,minimum_exclusive=%lu\n",static_cast<unsigned long>(manifest.version),static_cast<unsigned long>(current),static_cast<unsigned long>(minimumExclusive_));
    if(!roboOtaVersionEligible(manifest.version,current,minimumExclusive_)){
      setStatus(UpToDate,"No newer signed release is available for this firmware.",manifest.version);
      if(diagnostics)RoboLog.printf("LEV,NETDIAG,OTA_INSTALL,skipped=1,version=%lu,reason=no_newer_release\n",static_cast<unsigned long>(manifest.version));
      if(held)control_(context_,false);
      return;
    }
    if (!install) {
      setStatus(UpdateAvailable, "A signed firmware update is available.", manifest.version);
      if(!diagnostics)return;
      setStage("quiesce");
      if(!control_||!control_(context_,true)){setStatus(Failed,"Robot is not ready for firmware installation.",0,"robot_busy");return;}
      held=true;
    }
    setStatus(Downloading, "Downloading and verifying signed firmware...", manifest.version);
    if(diagnostics)RoboLog.printf("LEV,NETDIAG,OTA_INSTALL,version=%lu,accepted=1\n",static_cast<unsigned long>(manifest.version));
    if (!downloadImage(manifest)) { control_(context_, false); return; }
    setStage("reboot");
    setStatus(Rebooting, "Firmware verified. RoboDesk is restarting...");
    vTaskDelay(pdMS_TO_TICKS(900));
    ESP.restart();
  }
};
