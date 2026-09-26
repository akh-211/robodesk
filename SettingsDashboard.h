#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include "FirmwareOtaKey.h"
#include "RuntimeSettings.h"
#include "WakeWordBuildConfig.h"
#include "RoboBrain.h"
#include "GitHubOtaUpdate.h"

class SettingsDashboard {
 public:
  typedef bool (*SoundTestCallback)(void*, const char*);
  typedef bool (*FirmwareUpdateControlCallback)(void*, bool);
  typedef void (*DiagnosticsCallback)(void*, char*, size_t);
  SettingsDashboard() : server_(80) {}

  void begin(RuntimeSettings* settings, RuntimeSettingsStore* store, RoboBrain* brain, const char* setupApPassword, SoundTestCallback soundTest=0, void* soundTestContext=0, FirmwareUpdateControlCallback firmwareUpdateControl=0, void* firmwareUpdateContext=0, DiagnosticsCallback diagnostics=0, void* diagnosticsContext=0) {
    settings_ = settings;
    store_ = store;
    brain_ = brain;
    soundTest_ = soundTest;
    soundTestContext_ = soundTestContext;
    firmwareUpdateControl_ = firmwareUpdateControl;
    firmwareUpdateContext_ = firmwareUpdateContext;
    diagnostics_ = diagnostics;
    diagnosticsContext_ = diagnosticsContext;
    githubOta_.begin(firmwareUpdateControl, firmwareUpdateContext);
    RuntimeSettings::copy(setupApPassword_, sizeof(setupApPassword_), setupApPassword && setupApPassword[0] ? setupApPassword : "robodesk123");

    static const char* requestHeaders[] = {"Origin", "Host", "X-Robo-Signature"};
    server_.collectHeaders(requestHeaders, sizeof(requestHeaders) / sizeof(requestHeaders[0]));
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/save", HTTP_POST, [this]() { handleSave(); });
    server_.on("/reboot", HTTP_POST, [this]() { if (!authorized()) return; server_.send(200, "text/plain", "Rebooting"); rebootAt_ = millis() + 400; });
    server_.on("/factory", HTTP_POST, [this]() { handleFactory(); });
    server_.on("/memory/clear", HTTP_POST, [this]() { handleMemoryClear(); });
    server_.on("/memory/add", HTTP_POST, [this]() { handleMemoryAdd(); });
    server_.on("/sound/test", HTTP_POST, [this]() { handleSoundTest(); });
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    server_.on("/ota", HTTP_POST, [this]() { handleFirmwareUpdateComplete(); }, [this]() { handleFirmwareUpdateUpload(); });
    server_.on("/api/ota", HTTP_GET, [this]() { handleGitHubOtaStatus(); });
    server_.on("/ota/github/check", HTTP_POST, [this]() { handleGitHubOtaRequest(false); });
    server_.on("/ota/github/install", HTTP_POST, [this]() { handleGitHubOtaRequest(true); });
#endif
    server_.onNotFound([this]() { server_.sendHeader("Location", "/", true); server_.send(302, "text/plain", ""); });
    Serial.println("LEV,BOOT,DASHBOARD_ROUTES_READY");
  }

  void startHttp() {
    if (started_) return;
    server_.begin();
    started_ = true;
    Serial.println("LEV,CFG,DASHBOARD,HTTP=80");
  }

  void service(uint32_t now, bool allowHttp = true) {
    if (started_ && allowHttp) server_.handleClient();
    if (rebootAt_ && int32_t(now - rebootAt_) >= 0) ESP.restart();
    if (WiFi.status() == WL_CONNECTED && !mdnsStarted_) {
      if (MDNS.begin("robodesk")) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted_ = true;
        Serial.println("LEV,CFG,DASHBOARD,url=http://robodesk.local/");
      }
    }
  }

  bool startSetupAp() {
    if (apStarted_) return true;
    WiFi.mode(WIFI_AP_STA);
    char ssid[40];
    const uint32_t suffix = uint32_t(ESP.getEfuseMac());
    snprintf(ssid, sizeof(ssid), "RoboDesk-Setup-%04X", unsigned(suffix & 0xffff));
    apStarted_ = WiFi.softAP(ssid, setupApPassword_);
    if (apStarted_) {
      Serial.printf("LEV,CFG,AP,ssid=%s,ip=%s\n", ssid, WiFi.softAPIP().toString().c_str());
    } else {
      Serial.println("LEV,CFG,AP,FAILED");
    }
    return apStarted_;
  }

  bool apStarted() const { return apStarted_; }

 private:
  WebServer server_;
  RuntimeSettings* settings_ = 0;
  RuntimeSettingsStore* store_ = 0;
  RoboBrain* brain_ = 0;
  SoundTestCallback soundTest_ = 0;
  void* soundTestContext_ = 0;
  FirmwareUpdateControlCallback firmwareUpdateControl_ = 0;
  void* firmwareUpdateContext_ = 0;
  DiagnosticsCallback diagnostics_ = 0;
  void* diagnosticsContext_ = 0;
  GitHubOtaUpdate githubOta_;
  bool started_ = false;
  bool apStarted_ = false;
  bool mdnsStarted_ = false;
  uint32_t rebootAt_ = 0;
  char setupApPassword_[64] = {0};
  bool otaActive_ = false;
  bool otaHashStarted_ = false;
  bool otaControlHeld_ = false;
  bool otaSignatureValid_ = false;
  bool otaUpdateBegun_ = false;
  uint8_t otaSignature_[80] = {0};
  size_t otaSignatureLength_ = 0;
  size_t otaBytesWritten_ = 0;
  uint32_t otaVersion_ = 0;
  uint16_t otaErrorCode_ = 0;
  char otaError_[80] = {0};
  mbedtls_sha256_context otaImageHash_;

  bool authorized() {
    if (!settings_) return false;
    if (server_.authenticate("admin", settings_->adminPin)) return true;
    server_.requestAuthentication(BASIC_AUTH, "RoboDesk Settings", "Authentication required");
    return false;
  }

  static String esc(const char* s) {
    String out;
    if (!s) return out;
    while (*s) {
      const char c = *s++;
      if (c == '&') out += F("&amp;");
      else if (c == '<') out += F("&lt;");
      else if (c == '>') out += F("&gt;");
      else if (c == '"') out += F("&quot;");
      else out += c;
    }
    return out;
  }

  static String jsonEsc(const char* s) {
    String out;
    if (!s) return out;
    char escaped[7];
    while (*s) {
      const uint8_t c = uint8_t(*s++);
      if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
      else if (c == '\b') out += F("\\b");
      else if (c == '\f') out += F("\\f");
      else if (c == '\n') out += F("\\n");
      else if (c == '\r') out += F("\\r");
      else if (c == '\t') out += F("\\t");
      else if (c < 0x20) { snprintf(escaped, sizeof(escaped), "\\u%04x", unsigned(c)); out += escaped; }
      else out += char(c);
    }
    return out;
  }

  static uint16_t parseU16(const String& v, uint16_t fallback, uint16_t lo, uint16_t hi) {
    long x = v.toInt();
    if (x < lo || x > hi) return fallback;
    return uint16_t(x);
  }

  String pageHead(const char* title) {
    String h;
    h.reserve(1800);
    h += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
    h += F("<title>RoboDesk</title><style>body{font-family:system-ui;background:#111;color:#eee;margin:0;padding:20px}main{max-width:760px;margin:auto}.card{background:#1b1b1b;border:1px solid #333;border-radius:16px;padding:18px;margin:14px 0}h1{font-size:1.6rem}h2{font-size:1.05rem;margin-top:0}label{display:block;margin:12px 0 5px;color:#bbb}input,select,textarea{width:100%;box-sizing:border-box;background:#0f0f0f;color:#fff;border:1px solid #444;border-radius:9px;padding:10px}button{background:#eee;color:#111;border:0;border-radius:10px;padding:11px 16px;font-weight:700;margin-top:14px}.danger{background:#a33;color:white}.muted{color:#999;font-size:.9rem}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}@media(max-width:600px){.grid{grid-template-columns:1fr}}</style></head><body><main>");
    h += "<h1>"; h += title; h += F("</h1>");
    return h;
  }

  bool sameOriginRequest() const {
    const String host = server_.header("Host");
    const String origin = server_.header("Origin");
    if (!host.length() || !origin.length()) return false;
    String expected = F("http://");
    expected += host;
    return origin == expected;
  }

  void releaseFirmwareUpdate(bool resumeAudio) {
    if (otaUpdateBegun_) Update.abort();
    otaUpdateBegun_ = false;
    if (otaHashStarted_) mbedtls_sha256_free(&otaImageHash_);
    otaHashStarted_ = false;
    otaActive_ = false;
    otaSignatureValid_ = false;
    if (resumeAudio && otaControlHeld_ && firmwareUpdateControl_) {
      firmwareUpdateControl_(firmwareUpdateContext_, false);
    }
    otaControlHeld_ = false;
  }

  void failFirmwareUpdate(uint16_t status, const char* message) {
    otaErrorCode_ = status;
    RuntimeSettings::copy(otaError_, sizeof(otaError_), message);
    releaseFirmwareUpdate(true);
  }

  bool parseFirmwareSignature(const String& hex) {
    const int separator = hex.indexOf(':');
    if (separator < 1 || separator > 10) return false;
    const String versionText = hex.substring(0, separator);
    for (size_t i = 0; i < versionText.length(); ++i) if (versionText[i] < '0' || versionText[i] > '9') return false;
    const uint32_t version = uint32_t(strtoul(versionText.c_str(), nullptr, 10));
    const String signatureHex = hex.substring(separator + 1);
    if (!version || version > 0x7fffffffUL || signatureHex.length() < 128 || signatureHex.length() > sizeof(otaSignature_) * 2 || (signatureHex.length() & 1)) return false;
    Preferences prefs;
    if (!prefs.begin("robodesk_ota", true)) return false;
    const uint32_t current = prefs.getUInt("version", 0);
    prefs.end();
    if (!current || version <= current) return false;
    otaVersion_ = version;
    otaSignatureLength_ = signatureHex.length() / 2;
    for (size_t i = 0; i < otaSignatureLength_; ++i) {
      const char hi = signatureHex[i * 2], lo = signatureHex[i * 2 + 1];
      const int h = hi >= '0' && hi <= '9' ? hi - '0' : hi >= 'a' && hi <= 'f' ? hi - 'a' + 10 : hi >= 'A' && hi <= 'F' ? hi - 'A' + 10 : -1;
      const int l = lo >= '0' && lo <= '9' ? lo - '0' : lo >= 'a' && lo <= 'f' ? lo - 'a' + 10 : lo >= 'A' && lo <= 'F' ? lo - 'A' + 10 : -1;
      if (h < 0 || l < 0) return false;
      otaSignature_[i] = uint8_t((h << 4) | l);
    }
    return true;
  }

  bool verifyUploadedFirmware() {
    uint8_t imageDigest[32] = {0};
    if (!otaHashStarted_ || mbedtls_sha256_finish(&otaImageHash_, imageDigest) != 0) return false;
    mbedtls_sha256_free(&otaImageHash_);
    otaHashStarted_ = false;

    char shaHex[65];
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(imageDigest); ++i) {
      shaHex[i * 2] = digits[imageDigest[i] >> 4];
      shaHex[i * 2 + 1] = digits[imageDigest[i] & 0x0f];
    }
    shaHex[64] = 0;
    char signedMessage[144];
    const int messageLength = snprintf(signedMessage, sizeof(signedMessage),
        "RoboDeskSonicCharacter|ESP32-S3|%u|%u|%s", unsigned(otaVersion_), unsigned(otaBytesWritten_), shaHex);
    if (messageLength <= 0 || size_t(messageLength) >= sizeof(signedMessage)) return false;

    uint8_t messageDigest[32] = {0};
    mbedtls_sha256_context messageHash;
    mbedtls_sha256_init(&messageHash);
    const int hashOk = mbedtls_sha256_starts(&messageHash, 0) == 0 &&
        mbedtls_sha256_update(&messageHash, reinterpret_cast<const uint8_t*>(signedMessage), size_t(messageLength)) == 0 &&
        mbedtls_sha256_finish(&messageHash, messageDigest) == 0;
    mbedtls_sha256_free(&messageHash);
    if (!hashOk) return false;

    mbedtls_pk_context publicKey;
    mbedtls_pk_init(&publicKey);
    const int parsed = mbedtls_pk_parse_public_key(&publicKey,
        reinterpret_cast<const uint8_t*>(ROBODESK_OTA_PUBLIC_KEY_PEM), sizeof(ROBODESK_OTA_PUBLIC_KEY_PEM));
    const int verified = parsed == 0 ? mbedtls_pk_verify(&publicKey, MBEDTLS_MD_SHA256,
        messageDigest, 0, otaSignature_, otaSignatureLength_) : parsed;
    mbedtls_pk_free(&publicKey);
    return verified == 0;
  }

  void handleFirmwareUpdateUpload() {
#if !defined(CONFIG_APP_ROLLBACK_ENABLE)
    return;
#else
    HTTPUpload& upload = server_.upload();
    if (upload.status == UPLOAD_FILE_START) {
      otaErrorCode_ = 0;
      otaError_[0] = 0;
      if (otaActive_) { failFirmwareUpdate(409, "An update is already in progress"); return; }
      if (!authorized()) { failFirmwareUpdate(401, "Authentication required"); return; }
      if (!sameOriginRequest()) { failFirmwareUpdate(403, "Request origin rejected"); return; }
      String filename = upload.filename;
      filename.toLowerCase();
      if (upload.name != "firmware" || !filename.endsWith(".bin")) { failFirmwareUpdate(400, "Select one application .bin file"); return; }
      if (!parseFirmwareSignature(server_.header("X-Robo-Signature"))) { failFirmwareUpdate(400, "Firmware signature is missing or malformed"); return; }
      if (!firmwareUpdateControl_ || !firmwareUpdateControl_(firmwareUpdateContext_, true)) { failFirmwareUpdate(409, "Robot is busy; wait until audio or network activity stops"); return; }
      otaControlHeld_ = true;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { failFirmwareUpdate(500, "Could not open the inactive firmware slot"); return; }
      otaUpdateBegun_ = true;
      otaActive_ = true;
      otaSignatureValid_ = false;
      otaBytesWritten_ = 0;
      mbedtls_sha256_init(&otaImageHash_);
      otaHashStarted_ = mbedtls_sha256_starts(&otaImageHash_, 0) == 0;
      if (!otaHashStarted_) { failFirmwareUpdate(500, "Could not initialize firmware hash"); return; }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (!otaActive_ || !otaUpdateBegun_ || otaErrorCode_) return;
      const size_t written = Update.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize || mbedtls_sha256_update(&otaImageHash_, upload.buf, written) != 0) {
        failFirmwareUpdate(500, "Firmware write or hash failed");
        return;
      }
      otaBytesWritten_ += written;
    } else if (upload.status == UPLOAD_FILE_END) {
      if (!otaActive_ || otaErrorCode_) return;
      otaSignatureValid_ = verifyUploadedFirmware();
      if (!otaSignatureValid_) failFirmwareUpdate(400, "Firmware signature verification failed");
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      failFirmwareUpdate(400, "Firmware upload was interrupted");
    }
#endif
  }

  void handleFirmwareUpdateComplete() {
#if !defined(CONFIG_APP_ROLLBACK_ENABLE)
    server_.send(503, "text/plain", "OTA rollback is not enabled in this firmware build");
#else
    if (!authorized()) { releaseFirmwareUpdate(true); return; }
    if (!sameOriginRequest()) { releaseFirmwareUpdate(true); server_.send(403, "text/plain", "Request origin rejected"); return; }
    if (otaErrorCode_) {
      const uint16_t status = otaErrorCode_;
      const String message = otaError_;
      releaseFirmwareUpdate(true);
      server_.send(status, "text/plain", message);
      return;
    }
    if (!otaActive_ || !otaUpdateBegun_ || !otaSignatureValid_ || !otaBytesWritten_) {
      releaseFirmwareUpdate(true);
      server_.send(400, "text/plain", "No valid signed firmware was received");
      return;
    }
    Preferences prefs;
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target || !prefs.begin("robodesk_ota", false)) {
      releaseFirmwareUpdate(true);
      server_.send(500, "text/plain", "Could not stage firmware version; update was cancelled");
      return;
    }
    const bool staged = prefs.putUInt("pending", otaVersion_) == sizeof(uint32_t) &&
        prefs.putUInt("pend_off", target->address) == sizeof(uint32_t);
    if (!staged) { prefs.remove("pending"); prefs.remove("pend_off"); }
    prefs.end();
    if (!staged) {
      releaseFirmwareUpdate(true);
      server_.send(500, "text/plain", "Could not stage firmware version; update was cancelled");
      return;
    }
    if (!Update.end(true) || !Update.isFinished()) {
      prefs.begin("robodesk_ota", false);
      prefs.remove("pending");
      prefs.remove("pend_off");
      prefs.end();
      releaseFirmwareUpdate(true);
      server_.send(400, "text/plain", "Firmware image validation failed");
      return;
    }
    otaUpdateBegun_ = false;
    otaActive_ = false;
    otaControlHeld_ = false;  // Keep audio and networking paused until reboot.
    server_.send(200, "text/plain", "Signed firmware accepted. RoboDesk is rebooting.");
    rebootAt_ = millis() + 900;
#endif
  }

  void handleGitHubOtaRequest(bool install) {
#if !defined(CONFIG_APP_ROLLBACK_ENABLE)
    server_.send(503, "application/json", "{\"error\":\"OTA rollback is disabled\"}");
#else
    if (!authorized()) return;
    if (!sameOriginRequest()) { server_.send(403, "application/json", "{\"error\":\"Request origin rejected\"}"); return; }
    const bool accepted = install ? githubOta_.startInstall() : githubOta_.startCheck();
    char message[112]; githubOta_.statusText(message, sizeof(message));
    String response = F("{\"accepted\":"); response += accepted ? "true" : "false";
    response += F(",\"message\":\""); response += esc(message); response += F("\"}");
    server_.send(accepted ? 202 : 409, "application/json", response);
#endif
  }

  void handleGitHubOtaStatus() {
    if (!authorized()) return;
    char message[112]; githubOta_.statusText(message, sizeof(message));
    String response = F("{\"state\":"); response += String(unsigned(githubOta_.state()));
    response += F(",\"version\":"); response += String(githubOta_.availableVersion());
    response += F(",\"message\":\""); response += esc(message); response += F("\"}");
    server_.send(200, "application/json", response);
  }

  String githubOtaCard() {
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    return F("<div class='card'><h2>Wi-Fi firmware update</h2><p class='muted'>Check the public RoboDesk release, then install a signed ESP32-S3 update over HTTPS. Keep RoboDesk powered and connected to Wi-Fi while it downloads. The robot restarts after verification.</p><button id='ghCheck' type='button'>Check for updates</button> <button id='ghInstall' type='button' disabled>Install update</button><p id='ghStatus' class='muted' aria-live='polite'>Loading update status...</p><script>(()=>{const c=document.getElementById('ghCheck'),i=document.getElementById('ghInstall'),s=document.getElementById('ghStatus');let busy=false;async function refresh(){try{const r=await fetch('/api/ota',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json();s.textContent=d.version?'Firmware '+d.version+': '+d.message:d.message;i.disabled=busy||d.state!==3;c.disabled=busy||d.state===1||d.state===4||d.state===5;}catch(e){s.textContent='Could not read update status.';}}async function send(path){busy=true;try{const r=await fetch(path,{method:'POST'});const d=await r.json();s.textContent=d.message||'Request failed.';}catch(e){s.textContent='Connection lost while starting the request.';}busy=false;await refresh();}c.onclick=()=>send('/ota/github/check');i.onclick=()=>send('/ota/github/install');setInterval(refresh,2500);refresh();})();</script></div>");
#else
    return String();
#endif
  }

  void handleRoot() {
    if (!authorized()) return;
    String h = pageHead("RoboDesk Settings");
    h += githubOtaCard();
    h += F("<div class='card'><h2>Status</h2><p>Wi-Fi: <b>");
    h += WiFi.status() == WL_CONNECTED ? "connected" : "offline";
    h += F("</b> &nbsp; RSSI: "); h += String(WiFi.RSSI());
    h += F(" dBm<br>IP: "); h += WiFi.localIP().toString();
    h += F("<br>Heap: "); h += String(ESP.getFreeHeap()); h += F(" bytes<br>PSRAM: "); h += String(ESP.getPsramSize()); h += F(" bytes"); if(brain_){h += F("<br>Memories: ");h += String(brain_->memory().count());h += F(" &nbsp; Mood: ");h += brain_->mind().moodName();} h += F("</p><p class='muted'>Secrets are never shown back by this page. Leave password/API key blank to keep the stored value.</p></div>");

    h += F("<form method='post' action='/save'>");
    h += F("<div class='card'><h2>Network</h2><label>Wi-Fi SSID</label><input name='ssid' maxlength='32' value='"); h += esc(settings_->wifiSsid); h += F("'><label>Wi-Fi password</label><input type='password' name='wpass' maxlength='64' placeholder='leave blank to keep current'></div>");

    h += F("<div class='card'><h2>Gemini Live + AI Brain</h2><label>Robot name</label><input name='rname' maxlength='31' value='"); h += esc(settings_->robotName); h += F("'><label>API key</label><input type='password' name='gkey' maxlength='159' placeholder='leave blank to keep current'><div class='grid'><div><label>Model</label><input name='model' maxlength='47' value='"); h += esc(settings_->geminiModel); h += F("'></div><div><label>Voice</label><input name='voice' maxlength='31' value='"); h += esc(settings_->geminiVoice); h += F("'></div></div><label>Speech style</label><textarea name='style' rows='3' maxlength='240'>"); h += esc(settings_->speechStyle); h += F("</textarea></div>");

    h += F("<div class='card'><h2>Voice interaction</h2><label>Input mode</label><select name='mode'><option value='0'"); if(settings_->inputMode==0)h+=F(" selected"); h+=F(">Always listening (Hybrid VAD)</option><option value='1'"); if(settings_->inputMode==1)h+=F(" selected"); h+=F(">Touch-to-talk</option><option value='2'"); if(settings_->inputMode==2)h+=F(" selected");
#if ROBODESK_WAKEWORD_ENGINE_ENABLE
    h += F(">Wake word (WakeNet)</option></select>");
#else
    h += F(" disabled>Wake word (prepared, dormant in this build)</option></select>");
#endif
    h += F("<p class='muted'>Wake phrase model: <b>"); h += ROBODESK_WAKEWORD_LABEL; h += F("</b>. Engine build: "); h += ROBODESK_WAKEWORD_ENGINE_ENABLE ? "enabled" : "disabled"; h += F("; PSRAM: "); h += String(ESP.getPsramSize()); h += F(" bytes. The phrase is baked into the ESP-SR model partition; changing this label does not retrain the model.</p>");
    h += F("<div class='grid'><div><label>Speaker gain ×1000 (50–1000)</label><input type='number' name='gain' min='50' max='1000' value='"); h+=String(settings_->speakerGainMilli); h+=F("'></div><div><label>Post-speak guard ms</label><input type='number' name='guard' min='0' max='2000' value='"); h+=String(settings_->postSpeakGuardMs); h+=F("'></div><div><label>VAD start ×100</label><input type='number' name='vstart' min='120' max='1000' value='"); h+=String(settings_->vadStartX100); h+=F("'></div><div><label>VAD end ×100</label><input type='number' name='vend' min='105' max='800' value='"); h+=String(settings_->vadEndX100); h+=F("'></div><div><label>End silence ms</label><input type='number' name='vendms' min='120' max='2500' value='"); h+=String(settings_->vadEndMs); h+=F("'></div><div><label>Wake follow-up window ms</label><input type='number' name='wakewin' min='3000' max='60000' value='"); h+=String(settings_->wakeFollowupMs); h+=F("'></div></div></div>");

    h += F("<div class='card'><h2>Sonic character</h2><label><input style='width:auto' type='checkbox' name='sndmaster' value='1'");if(settings_->masterSound)h+=F(" checked");h+=F("> Master sound</label><label><input style='width:auto' type='checkbox' name='sndspeech' value='1'");if(settings_->speechEnabled)h+=F(" checked");h+=F("> Gemini speech</label><label><input style='width:auto' type='checkbox' name='sndsfx' value='1'");if(settings_->characterSfx)h+=F(" checked");h+=F("> Local character SFX</label><div class='grid'><div><label>SFX intensity %</label><input type='number' name='sfxint' min='0' max='100' value='");h+=String(settings_->sfxIntensityX100);h+=F("'></div><div><label>SFX frequency</label><select name='sndfreq'><option value='0'");if(settings_->sonicFrequency==0)h+=F(" selected");h+=F(">Low</option><option value='1'");if(settings_->sonicFrequency==1)h+=F(" selected");h+=F(">Normal</option><option value='2'");if(settings_->sonicFrequency==2)h+=F(" selected");h+=F(">Expressive</option></select></div></div><label><input style='width:auto' type='checkbox' name='sndwake' value='1'");if(settings_->wakeSfx)h+=F(" checked");h+=F("> Wake acknowledgement</label><label><input style='width:auto' type='checkbox' name='sndtouch' value='1'");if(settings_->touchSfx)h+=F(" checked");h+=F("> Touch/affection sounds</label><label><input style='width:auto' type='checkbox' name='sndmotion' value='1'");if(settings_->motionSfx)h+=F(" checked");h+=F("> Motion/pickup/shake sounds</label><label><input style='width:auto' type='checkbox' name='sndnotif' value='1'");if(settings_->notificationSfx)h+=F(" checked");h+=F("> Notification/status sounds</label><label><input style='width:auto' type='checkbox' name='quieton' value='1'");if(settings_->quietHoursEnabled)h+=F(" checked");h+=F("> Quiet hours</label><div class='grid'><div><label>Quiet start minute</label><input type='number' name='quietstart' min='0' max='1439' value='");h+=String(settings_->quietStartMin);h+=F("'></div><div><label>Quiet end minute</label><input type='number' name='quietend' min='0' max='1439' value='");h+=String(settings_->quietEndMin);h+=F("'></div><div><label>Quiet SFX gain %</label><input type='number' name='quietgain' min='0' max='100' value='");h+=String(settings_->quietGainX100);h+=F("'></div></div><p class='muted'>1320 = 22:00, 420 = 07:00. Speech still answers the user; unsolicited/proactive voice is suppressed during quiet hours.</p><p><b>Test:</b></p><div class='grid'>");const char* tc[]={"wake","happy","curious","affection","thinking","surprise","sad","sleepy","error","success","pickup","shake"};for(unsigned i=0;i<sizeof(tc)/sizeof(tc[0]);i++){h+=F("<button type='submit' formaction='/sound/test' formmethod='post' name='cue' value='");h+=tc[i];h+=F("'>");h+=tc[i];h+=F("</button>");}h+=F("</div></div>");

    h += F("<div class='card'><h2>Living character</h2><label><input style='width:auto' type='checkbox' name='memory' value='1'"); if(settings_->memoryEnabled)h+=F(" checked"); h+=F("> Persistent AI memory</label><label><input style='width:auto' type='checkbox' name='pvisual' value='1'"); if(settings_->proactiveVisual)h+=F(" checked"); h+=F("> Proactive visual reactions</label><label><input style='width:auto' type='checkbox' name='pvoice' value='1'"); if(settings_->proactiveVoice)h+=F(" checked"); h+=F("> Proactive spoken greetings (uses Gemini/API quota)</label><label>Owner preferred name</label><input name='owner' maxlength='47' value='"); if(brain_)h+=esc(brain_->ownerName()); h+=F("'><label>Timezone offset minutes from UTC</label><input type='number' name='tzmin' min='-720' max='840' value='"); h+=String(settings_->timezoneOffsetMin);h+=F("'><p class='muted'>Jakarta = 420. Character clock drives routines and day/night behavior.</p></div>");
    h += F("<div class='card'><h2>Living face</h2><label><input style='width:auto' type='checkbox' name='flife' value='1'");if(settings_->faceLifeEnabled)h+=F(" checked");h+=F("> Dynamic face life (attention + micro-motion + elastic eye spacing)</label><label><input style='width:auto' type='checkbox' name='fpupil' value='1'");if(settings_->facePupils)h+=F(" checked");h+=F("> Pupils</label><label><input style='width:auto' type='checkbox' name='fbrow' value='1'");if(settings_->faceBrows)h+=F(" checked");h+=F("> Eyebrows available</label><label><input style='width:auto' type='checkbox' name='fabrow' value='1'");if(settings_->faceAutoBrows)h+=F(" checked");h+=F("> Show eyebrows only when expressive/attentive</label><label><input style='width:auto' type='checkbox' name='flash' value='1'");if(settings_->faceLashes)h+=F(" checked");h+=F("> Eyelashes available</label><label><input style='width:auto' type='checkbox' name='falash' value='1'");if(settings_->faceAutoLashes)h+=F(" checked");h+=F("> Eyelashes only for soft/social states</label><label><input style='width:auto' type='checkbox' name='fpact' value='1'");if(settings_->facePupilLessActing)h+=F(" checked");h+=F("> Strong eye-shape acting when pupils are hidden</label><div class='grid'><div><label>Micro-motion %</label><input type='number' name='fmicro' min='0' max='100' value='");h+=String(settings_->faceMicroX100);h+=F("'></div><div><label>No-pupil acting %</label><input type='number' name='fsil' min='0' max='135' value='");h+=String(settings_->faceSilhouetteX100);h+=F("'></div><div><label>Edge/corner gaze %</label><input type='number' name='fgaze' min='40' max='125' value='");h+=String(settings_->faceGazeReachX100);h+=F("'></div><div><label>Mouth policy</label><select name='mouth'><option value='0'");if(settings_->mouthMode==0)h+=F(" selected");h+=F(">Automatic (recommended)</option><option value='1'");if(settings_->mouthMode==1)h+=F(" selected");h+=F(">Always hidden</option><option value='2'");if(settings_->mouthMode==2)h+=F(" selected");h+=F(">Always visible</option></select></div></div><p class='muted'>Automatic mouth stays hidden at rest and appears only while speaking or for short special emotes such as laugh, surprise, sad pout, and yawn.</p></div>");
    if(brain_){h += F("<div class='card'><h2>Local memory</h2><p class='muted'>Stored only on this ESP32 NVS. Gemini can remember/recall/forget through local tools.</p>");for(unsigned i=0;i<livingeyes::SemanticMemory::Capacity;i++){const auto*e=brain_->memory().entry(i);if(!e)continue;h+=F("<p><b>");h+=esc(e->key);h+=F("</b> <span class='muted'>[");h+=livingeyes::SemanticMemory::kindName(e->kind);h+=F("]</span><br>");h+=esc(e->value);h+=F("</p>");}h+=F("<label>Memory key</label><input name='mkey' maxlength='27'><label>Value</label><input name='mvalue' maxlength='111'><label>Category</label><select name='mcat'><option>note</option><option>profile</option><option>preference</option><option>routine</option><option>place</option><option>event</option></select><button type='submit' formaction='/memory/add' formmethod='post'>Add memory</button><button type='submit' class='danger' formaction='/memory/clear' formmethod='post' onclick='return confirm(&quot;Clear all local AI memory?&quot;)'>Clear AI memory</button></div>");}

    h += F("<div class='card'><h2>Security</h2><label>Dashboard password</label><input type='password' name='pin' maxlength='31' placeholder='leave blank to keep current'><p class='muted'>Username is <b>admin</b>. Saving settings reboots the robot so network and Gemini settings are applied cleanly.</p><button type='submit'>Save & reboot</button></div></form>");
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    h += F("<div class='card'><h2>Firmware update</h2><p class='muted'>Works on the same trusted Wi-Fi network without internet. Dashboard login uses unencrypted HTTP; do not update over public/shared Wi-Fi. Choose a signed RoboDesk ESP32-S3 application .bin and its matching .sig file. The robot accepts updates only while audio is idle. Firmware versions must increase.</p><label>Application firmware (.bin)</label><input id='otaBin' type='file' accept='.bin,application/octet-stream'><label>Signature (.sig)</label><input id='otaSig' type='file' accept='.sig,text/plain'><button id='otaStart' type='button'>Install firmware</button><progress id='otaProgress' max='100' value='0' style='width:100%;display:none'></progress><p id='otaResult' class='muted'></p><script>document.getElementById('otaStart').onclick=async function(){const b=document.getElementById('otaBin').files[0],s=document.getElementById('otaSig').files[0],r=document.getElementById('otaResult'),p=document.getElementById('otaProgress'),button=this;if(!b||!s){r.textContent='Choose both files first.';return;}let sig;try{sig=(await s.text()).trim();}catch(e){r.textContent='Could not read signature.';return;}if(!/^[1-9][0-9]{0,9}:[0-9a-f]{128,160}$/i.test(sig)){r.textContent='Signature file is malformed.';return;}const f=new FormData();f.append('firmware',b,b.name);const x=new XMLHttpRequest();x.open('POST','/ota');x.setRequestHeader('X-Robo-Signature',sig);x.upload.onprogress=e=>{if(e.lengthComputable){p.style.display='block';p.value=Math.floor(100*e.loaded/e.total);}};x.onload=()=>{r.textContent=x.responseText||('Update failed ('+x.status+').');button.disabled=false;};x.onerror=()=>{r.textContent='Connection lost. The current firmware remains active unless a signed image was fully accepted.';button.disabled=false;};button.disabled=true;r.textContent='Uploading and verifying signature…';x.send(f);};</script></div>");
#else
    h += F("<div class='card'><h2>Firmware update</h2><p class='muted'>OTA is disabled because rollback support is not enabled in this firmware build.</p></div>");
#endif
    h += F("<div class='card'><form method='post' action='/reboot'><button>Reboot</button></form><form method='post' action='/factory' onsubmit=\"return confirm('Erase all saved settings?')\"><button class='danger'>Factory reset</button></form></div></main></body></html>");
    server_.send(200, "text/html; charset=utf-8", h);
  }

  void handleStatus() {
    if (!authorized()) return;
    String s = F("{\"wifi\":"); s += WiFi.status() == WL_CONNECTED ? "true" : "false";
    s += F(",\"rssi\":"); s += String(WiFi.RSSI());
    s += F(",\"heap\":"); s += String(ESP.getFreeHeap());
    s += F(",\"ssid\":\""); s += jsonEsc(settings_->wifiSsid); s += F("\",\"model\":\""); s += jsonEsc(settings_->geminiModel); s += F("\",\"voice\":\""); s += jsonEsc(settings_->geminiVoice); s += F("\"");
    char diagnostics[512] = {0};
    if (diagnostics_) diagnostics_(diagnosticsContext_, diagnostics, sizeof(diagnostics));
    if (diagnostics[0]) s += diagnostics;
    s += F("}");
    server_.send(200, "application/json", s);
  }

  void handleSave() {
    if (!authorized() || !settings_ || !store_) return;
    RuntimeSettings next = *settings_;
    if (server_.hasArg("ssid")) RuntimeSettings::copy(next.wifiSsid, sizeof(next.wifiSsid), server_.arg("ssid").c_str());
    if (server_.hasArg("wpass") && server_.arg("wpass").length()) RuntimeSettings::copy(next.wifiPassword, sizeof(next.wifiPassword), server_.arg("wpass").c_str());
    if (server_.hasArg("gkey") && server_.arg("gkey").length()) RuntimeSettings::copy(next.geminiApiKey, sizeof(next.geminiApiKey), server_.arg("gkey").c_str());
    if (server_.hasArg("model") && server_.arg("model").length()) RuntimeSettings::copy(next.geminiModel, sizeof(next.geminiModel), server_.arg("model").c_str());
    if (server_.hasArg("voice") && server_.arg("voice").length()) RuntimeSettings::copy(next.geminiVoice, sizeof(next.geminiVoice), server_.arg("voice").c_str());
    if (server_.hasArg("style")) RuntimeSettings::copy(next.speechStyle, sizeof(next.speechStyle), server_.arg("style").c_str());
    if (server_.hasArg("rname") && server_.arg("rname").length()) RuntimeSettings::copy(next.robotName, sizeof(next.robotName), server_.arg("rname").c_str());
    if (server_.hasArg("pin") && server_.arg("pin").length() >= 6) RuntimeSettings::copy(next.adminPin, sizeof(next.adminPin), server_.arg("pin").c_str());
    if (server_.hasArg("gain")) next.speakerGainMilli = parseU16(server_.arg("gain"), next.speakerGainMilli, 50, 1000);
    if (server_.hasArg("guard")) next.postSpeakGuardMs = parseU16(server_.arg("guard"), next.postSpeakGuardMs, 0, 2000);
    if (server_.hasArg("vstart")) next.vadStartX100 = parseU16(server_.arg("vstart"), next.vadStartX100, 120, 1000);
    if (server_.hasArg("vend")) next.vadEndX100 = parseU16(server_.arg("vend"), next.vadEndX100, 105, 800);
    if (server_.hasArg("vendms")) next.vadEndMs = parseU16(server_.arg("vendms"), next.vadEndMs, 120, 2500);
    if (server_.hasArg("wakewin")) next.wakeFollowupMs = parseU16(server_.arg("wakewin"), next.wakeFollowupMs, 3000, 60000);
    if (server_.hasArg("tzmin")) { long z=server_.arg("tzmin").toInt(); if(z>=-720&&z<=840)next.timezoneOffsetMin=int16_t(z);}
    if (brain_ && server_.hasArg("owner") && server_.arg("owner").length()) { brain_->setOwnerName(server_.arg("owner").c_str(),0); brain_->save(millis()); }
    next.memoryEnabled=server_.hasArg("memory")?1:0; next.proactiveVisual=server_.hasArg("pvisual")?1:0; next.proactiveVoice=server_.hasArg("pvoice")?1:0;
    next.faceLifeEnabled=server_.hasArg("flife")?1:0;next.facePupils=server_.hasArg("fpupil")?1:0;next.faceBrows=server_.hasArg("fbrow")?1:0;next.faceLashes=server_.hasArg("flash")?1:0;next.faceAutoBrows=server_.hasArg("fabrow")?1:0;next.faceAutoLashes=server_.hasArg("falash")?1:0;next.facePupilLessActing=server_.hasArg("fpact")?1:0;
    if(server_.hasArg("fmicro"))next.faceMicroX100=parseU16(server_.arg("fmicro"),next.faceMicroX100,0,100);
    if(server_.hasArg("fsil"))next.faceSilhouetteX100=parseU16(server_.arg("fsil"),next.faceSilhouetteX100,0,135);
    if(server_.hasArg("fgaze"))next.faceGazeReachX100=parseU16(server_.arg("fgaze"),next.faceGazeReachX100,40,125);
    if(server_.hasArg("mouth")){int m=server_.arg("mouth").toInt();if(m>=0&&m<=2)next.mouthMode=uint8_t(m);}
    next.masterSound=server_.hasArg("sndmaster")?1:0;next.speechEnabled=server_.hasArg("sndspeech")?1:0;next.characterSfx=server_.hasArg("sndsfx")?1:0;next.wakeSfx=server_.hasArg("sndwake")?1:0;next.touchSfx=server_.hasArg("sndtouch")?1:0;next.motionSfx=server_.hasArg("sndmotion")?1:0;next.notificationSfx=server_.hasArg("sndnotif")?1:0;next.quietHoursEnabled=server_.hasArg("quieton")?1:0;
    if(server_.hasArg("sfxint"))next.sfxIntensityX100=parseU16(server_.arg("sfxint"),next.sfxIntensityX100,0,100);
    if(server_.hasArg("sndfreq")){int f=server_.arg("sndfreq").toInt();if(f>=0&&f<=2)next.sonicFrequency=uint8_t(f);}
    if(server_.hasArg("quietstart"))next.quietStartMin=parseU16(server_.arg("quietstart"),next.quietStartMin,0,1439);
    if(server_.hasArg("quietend"))next.quietEndMin=parseU16(server_.arg("quietend"),next.quietEndMin,0,1439);
    if(server_.hasArg("quietgain"))next.quietGainX100=parseU16(server_.arg("quietgain"),next.quietGainX100,0,100);
    if (server_.hasArg("mode")) {
      const int requested = server_.arg("mode").toInt();
      if (requested == 1) next.inputMode = RuntimeSettings::TouchToTalk;
#if ROBODESK_WAKEWORD_ENGINE_ENABLE
      else if (requested == 2) next.inputMode = RuntimeSettings::WakeWord;
#endif
      else next.inputMode = RuntimeSettings::AlwaysListening;
    }

    if (!store_->save(next)) { server_.send(500, "text/plain", "Failed to save settings"); return; }
    *settings_ = next;
    server_.send(200, "text/html; charset=utf-8", pageHead("Settings saved") + F("<div class='card'><p>Saved. RoboDesk will reboot and apply the new settings.</p></div></main></body></html>"));
    rebootAt_ = millis() + 1200;
  }

  void handleSoundTest() {
    if (!authorized()) return;
    if (!soundTest_ || !server_.hasArg("cue")) { server_.send(503,"text/plain","Sound test unavailable"); return; }
    const String cue=server_.arg("cue");const bool ok=soundTest_(soundTestContext_,cue.c_str());
    server_.sendHeader("Location","/",true);server_.send(ok?302:409,"text/plain",ok?"":"Sound busy or disabled");
  }

  void handleFactory() {
    if (!authorized() || !store_) return;
    bool ok = store_->clear(); if(brain_)ok = brain_->clearMemory(millis()) && ok;
    server_.send(ok ? 200 : 500, "text/plain", ok ? "Factory settings cleared; rebooting" : "Failed to clear settings");
    if (ok) rebootAt_ = millis() + 800;
  }
  void handleMemoryClear() {
    if (!authorized() || !brain_) return;
    const bool ok=brain_->clearMemory(millis());
    server_.send(ok?200:500,"text/plain",ok?"AI memory cleared":"Failed to clear AI memory");
  }

  void handleMemoryAdd() {
    if (!authorized() || !brain_) return;
    if(!server_.hasArg("mkey")||!server_.hasArg("mvalue")){server_.send(400,"text/plain","Missing key/value");return;}
    String k=server_.arg("mkey"),v=server_.arg("mvalue"),c=server_.hasArg("mcat")?server_.arg("mcat"):String("note");
    if(!k.length()||!v.length()){server_.send(400,"text/plain","Empty key/value");return;}
    bool ok=brain_->memory().remember(livingeyes::SemanticMemory::kindFromName(c.c_str()),k.c_str(),v.c_str(),190,0,false);
    if(ok)brain_->save(millis());
    server_.sendHeader("Location","/",true);server_.send(ok?302:500,"text/plain",ok?"":"Failed");
  }

};
