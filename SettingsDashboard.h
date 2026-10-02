#include "RoboLog.h"
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
#include "DashboardSecurity.h"
#include "PhoneNotificationBridge.h"

class SettingsDashboard {
 public:
  typedef bool (*SoundTestCallback)(void*, const char*);
  typedef bool (*FirmwareUpdateControlCallback)(void*, bool);
  typedef bool (*CompanionActionCallback)(void*, const char*, const char*, char*, size_t);
  typedef void (*DiagnosticsCallback)(void*, char*, size_t);
  SettingsDashboard() : server_(80) {}

  void begin(RuntimeSettings* settings, RuntimeSettingsStore* store, RoboBrain* brain, const char* setupApPassword, SoundTestCallback soundTest=0, void* soundTestContext=0, FirmwareUpdateControlCallback firmwareUpdateControl=0, void* firmwareUpdateContext=0, DiagnosticsCallback diagnostics=0, void* diagnosticsContext=0, CompanionActionCallback companionAction=0, void* companionActionContext=0, GitHubOtaUpdate::PreparationCallback otaPreparation=nullptr, PhoneNotificationBridge* notifications=nullptr) {
    settings_ = settings;
    store_ = store;
    brain_ = brain;
    notifications_=notifications;
    soundTest_ = soundTest;
    soundTestContext_ = soundTestContext;
    firmwareUpdateControl_ = firmwareUpdateControl;
    firmwareUpdateContext_ = firmwareUpdateContext;
    companionAction_ = companionAction;
    companionActionContext_ = companionActionContext;
    diagnostics_ = diagnostics;
    diagnosticsContext_ = diagnosticsContext;
    githubOta_.begin(firmwareUpdateControl, firmwareUpdateContext, otaPreparation);
    RuntimeSettings::copy(setupApPassword_, sizeof(setupApPassword_), setupApPassword && setupApPassword[0] ? setupApPassword : "robodesk123");

    static const char* requestHeaders[] = {"Origin", "Host", "X-Robo-Signature"};
    server_.collectHeaders(requestHeaders, sizeof(requestHeaders) / sizeof(requestHeaders[0]));
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/save", HTTP_POST, [this]() { handleSave(); });
    server_.on("/reboot", HTTP_POST, [this]() { if (!authorizeMutation()) return; server_.send(200, "text/plain", "Rebooting"); rebootAt_ = millis() + 400; });
    server_.on("/factory", HTTP_POST, [this]() { handleFactory(); });
    server_.on("/memory/clear", HTTP_POST, [this]() { handleMemoryClear(); });
    server_.on("/memory/delete",HTTP_POST,[this](){handleMemoryDelete();});
    server_.on("/api/memory",HTTP_GET,[this](){handleMemoryList();});
    server_.on("/api/reminders",HTTP_GET,[this](){handleReminderList();});
    server_.on("/api/notifications",HTTP_GET,[this](){handleNotifications();});
    server_.on("/reminder/add",HTTP_POST,[this](){handleReminderAdd();});
    server_.on("/reminder/cancel",HTTP_POST,[this](){handleReminderCancel();});
    server_.on("/memory/add", HTTP_POST, [this]() { handleMemoryAdd(); });
    server_.on("/memory/experiences/clear", HTTP_POST, [this]() { handleExperiencesClear(); });
    server_.on("/companion/action", HTTP_POST, [this]() { handleCompanionAction(); });
    server_.on("/sound/test", HTTP_POST, [this]() { handleSoundTest(); });
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    server_.on("/ota", HTTP_POST, [this]() { handleFirmwareUpdateComplete(); }, [this]() { handleFirmwareUpdateUpload(); });
    server_.on("/api/ota", HTTP_GET, [this]() { handleGitHubOtaStatus(); });
    server_.on("/ota/github/check", HTTP_POST, [this]() { handleGitHubOtaRequest(false); });
    server_.on("/ota/github/install", HTTP_POST, [this]() { handleGitHubOtaRequest(true); });
#endif
    server_.onNotFound([this]() { server_.sendHeader("Location", "/", true); server_.send(302, "text/plain", ""); });
    RoboLog.println("LEV,BOOT,DASHBOARD_ROUTES_READY");
  }

  void startHttp() {
    if (started_) return;
    server_.begin();
    started_ = true;
    RoboLog.println("LEV,CFG,DASHBOARD,HTTP=80");
  }

  void service(uint32_t now, bool allowHttp = true) {
    if (started_ && allowHttp) server_.handleClient();
    githubOta_.service();
    if (rebootAt_ && int32_t(now - rebootAt_) >= 0) ESP.restart();
    if (WiFi.status() == WL_CONNECTED && !mdnsStarted_) {
      if (MDNS.begin("robodesk")) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted_ = true;
        RoboLog.println("LEV,CFG,DASHBOARD,url=http://robodesk.local/");
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
      RoboLog.printf("LEV,CFG,AP,ssid=%s,ip=%s\n", ssid, WiFi.softAPIP().toString().c_str());
    } else {
      RoboLog.println("LEV,CFG,AP,FAILED");
    }
    return apStarted_;
  }

  bool apStarted() const { return apStarted_; }

  GitHubOtaUpdate::StartResult startGitHubOtaDiagnostics(uint32_t minimumExclusive,long timezoneSeconds) { return githubOta_.requestDiagnostics(minimumExclusive,timezoneSeconds); }
  GitHubOtaUpdate::Snapshot githubOtaSnapshot() const { return githubOta_.snapshot(); }
  bool githubOtaWorkerActive() const { return githubOta_.active(); }

 private:
  WebServer server_;
  RuntimeSettings* settings_ = 0;
  RuntimeSettingsStore* store_ = 0;
  RoboBrain* brain_ = 0;
  PhoneNotificationBridge* notifications_=nullptr;
  SoundTestCallback soundTest_ = 0;
  void* soundTestContext_ = 0;
  CompanionActionCallback companionAction_ = 0;
  void* companionActionContext_ = 0;
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

  bool authorizeMutation() {
    if (!authorized()) return false;
    if (sameOriginRequest()) return true;
    server_.send(403, "text/plain", "Request origin rejected");
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
      else if (c == '\'') out += F("&#39;");
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

  String pageHead(const char* title, bool full = true) {
    String h;
    h.reserve(24000);
    h += F("<!doctype html><html lang='id'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>");
    h += F("<title>RoboDesk Dashboard</title><style>"
      ":root{color-scheme:dark;--bg:#0a1020;--panel:#16233a;--panel2:#1b2a43;--line:#334560;--ink:#f1f6ff;--muted:#b6c4d8;--accent:#8bd9f8;--accent2:#a7f3d0;--warn:#fbbf24;--danger:#fb7185;--shadow:0 18px 48px #050b1b88}"
      "*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:radial-gradient(circle at 15% -10%,#245074 0,transparent 42rem),radial-gradient(circle at 100% 15%,#18324a 0,transparent 34rem),var(--bg);color:var(--ink);font:15px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
      "main{width:min(1180px,100%);margin:auto;padding:20px 16px 56px}.top{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;margin:8px 0 20px}.eyebrow{color:var(--accent);font-weight:800;letter-spacing:.12em;text-transform:uppercase;font-size:.72rem}.top h1{font-size:clamp(1.8rem,4vw,2.8rem);line-height:1.05;margin:5px 0 8px}.top p{margin:0;color:var(--muted);max-width:680px}.pill{display:inline-flex;align-items:center;gap:7px;border:1px solid var(--line);border-radius:999px;padding:7px 11px;color:var(--muted);white-space:nowrap}.dot{width:8px;height:8px;border-radius:50%;background:var(--warn)}.dot.good{background:var(--accent2)}.dot.bad{background:var(--danger)}"
      ".nav{position:sticky;top:0;z-index:4;display:flex;gap:7px;overflow:auto;padding:8px 0 12px;background:#0b1020e8;backdrop-filter:blur(12px)}.nav a{color:var(--muted);border:1px solid var(--line);border-radius:999px;padding:7px 12px;text-decoration:none;white-space:nowrap}.nav a:hover,.nav a:focus,.nav a[aria-current='location']{color:var(--ink);border-color:var(--accent);background:#15334a}"
      ".hero,.card{background:linear-gradient(155deg,var(--panel),#101a2dee);border:1px solid var(--line);border-radius:20px;box-shadow:var(--shadow)}.hero{padding:24px;margin-bottom:18px;scroll-margin-top:76px}.hero>.section-intro{max-width:68ch}.hero h2,.card h2{margin:0 0 6px;font-size:1.08rem}.hero p,.card p{margin:6px 0}.statgrid{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:9px;margin-top:15px}.stat{background:#0d1628;border:1px solid #263754;border-radius:13px;padding:12px;min-width:0}.stat .label{color:var(--muted);font-size:.76rem}.stat .value{font-size:1.12rem;font-weight:800;margin-top:3px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.stat .detail{font-size:.75rem;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.layout{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:16px}.homegrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:14px}.homeitem{background:#0d1628;border:1px solid #263754;border-radius:13px;padding:12px}.homeitem h3{margin:0 0 4px;color:var(--ink);font-size:.86rem}.homeitem p{margin:0;color:var(--muted);font-size:.84rem}.card{padding:20px;scroll-margin-top:76px}.card .card{background:#0e1a2d;box-shadow:none;margin:14px 0}.wide{grid-column:1/-1}.card h3{font-size:.9rem;margin:18px 0 7px;color:var(--accent)}.section-intro{color:var(--muted);font-size:.9rem;margin-top:0}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.field{min-width:0}.field.full{grid-column:1/-1}label,.legend{display:block;margin:11px 0 5px;color:var(--muted);font-size:.86rem;font-weight:650}input,select,textarea{width:100%;box-sizing:border-box;background:#0b1323;color:var(--ink);border:1px solid #354766;border-radius:10px;padding:10px 11px;font:inherit}input:focus,select:focus,textarea:focus,button:focus,a:focus{outline:3px solid #7dd3fc66;outline-offset:2px;border-color:var(--accent)}textarea{resize:vertical}.check{display:flex;align-items:flex-start;gap:9px;margin:11px 0;color:var(--ink);font-weight:550}.check input{width:auto;margin-top:4px;accent-color:var(--accent)}button{background:var(--accent);color:#07111d;border:0;border-radius:10px;padding:10px 14px;font:inherit;font-weight:800;cursor:pointer;margin:8px 7px 0 0}button.secondary{background:#263754;color:var(--ink)}button.danger{background:var(--danger);color:#260711}button:not(:disabled):hover{filter:brightness(1.12)}button:disabled{opacity:.55;cursor:not-allowed}.button-row{display:flex;flex-wrap:wrap;gap:4px}.muted{color:var(--muted);font-size:.88rem}.notice{border-left:3px solid var(--accent);background:#0b1628;padding:10px 12px;border-radius:8px;color:var(--muted);font-size:.88rem}.notice.warn{border-color:var(--warn)}.notice.danger{border-color:var(--danger);background:#261421}.diag{font:12px/1.45 ui-monospace,SFMono-Regular,Consolas,monospace;white-space:pre-wrap;color:var(--muted);max-height:220px;overflow:auto;background:#0a1120;border-radius:10px;padding:10px}.memory-item,.reminder-item{border-top:1px solid #263754;padding:10px 0}.memory-item:first-child,.reminder-item:first-child{border-top:0}.inline{display:inline}.inline button{padding:6px 9px;font-size:.82rem;margin:0 0 0 6px}.progress{width:100%;height:9px;accent-color:var(--accent);display:none}.footer{color:var(--muted);font-size:.8rem;margin-top:18px}.ota-steps{display:flex;gap:10px;list-style:none;padding:0;margin:16px 0}.ota-steps li{flex:1;border:1px solid var(--line);background:#101d30;border-radius:12px;padding:10px;color:var(--muted);font-size:.86rem}.ota-steps li.current{border-color:var(--accent);color:var(--ink)}.update-meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px;margin:14px 0}.update-meta div{padding:10px 12px;border:1px solid var(--line);border-radius:12px}.update-meta strong{display:block;color:var(--ink);font-size:1.02rem}.ota-help{display:none}.ota-help a{color:var(--accent)}@media(max-width:900px){.statgrid{grid-template-columns:repeat(3,minmax(0,1fr))}}@media(max-width:650px){main{padding:13px 10px 40px}.top{display:block}.top .pill{margin-top:13px}.layout{grid-template-columns:1fr}.wide{grid-column:auto}.grid{grid-template-columns:1fr}.homegrid{grid-template-columns:1fr}.field.full{grid-column:auto}.statgrid{grid-template-columns:repeat(2,minmax(0,1fr))}.ota-steps{flex-direction:column}.update-meta{grid-template-columns:1fr}.card,.hero{padding:14px;border-radius:14px}}@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}"
      "</style><style>"
      ":root{color-scheme:dark;--bg:#050814;--panel:rgba(14,20,44,.72);--panel2:rgba(14,20,44,.88);--line:rgba(0,229,255,.22);--ink:#e6f1ff;--muted:#91a0c5;--accent:#00e5ff;--accent2:#a66bff;--accent3:#2dffb0;--shadow:0 14px 36px #0006}"
      "@media(prefers-color-scheme:light){:root:not([data-theme='dark']){color-scheme:light;--bg:#eaf2fb;--panel:rgba(255,255,255,.82);--panel2:rgba(255,255,255,.94);--line:rgba(0,120,170,.24);--ink:#0b1d33;--muted:#5a6f8a;--accent:#0088c7;--accent2:#7a3dff;--accent3:#0a9f6a;--shadow:0 14px 36px #18314a22}}"
      ":root[data-theme='light']{color-scheme:light;--bg:#eaf2fb;--panel:rgba(255,255,255,.82);--panel2:rgba(255,255,255,.94);--line:rgba(0,120,170,.24);--ink:#0b1d33;--muted:#5a6f8a;--accent:#0088c7;--accent2:#7a3dff;--accent3:#0a9f6a;--shadow:0 14px 36px #18314a22}"
      ":root[data-theme='dark']{color-scheme:dark}body{display:flex;min-height:100vh;background:var(--bg);font:500 16px/1.5 Rajdhani,system-ui,-apple-system,'Segoe UI',sans-serif}body:before{content:'';position:fixed;inset:0;z-index:-1;pointer-events:none;background:radial-gradient(640px 420px at 88% -8%,color-mix(in srgb,var(--accent2) 14%,transparent),transparent),radial-gradient(700px 500px at 0% 100%,color-mix(in srgb,var(--accent) 15%,transparent),transparent),linear-gradient(color-mix(in srgb,var(--accent) 5%,transparent) 1px,transparent 1px) 0 0/42px 42px,linear-gradient(90deg,color-mix(in srgb,var(--accent) 5%,transparent) 1px,transparent 1px) 0 0/42px 42px}"
      "aside{width:228px;flex:none;position:sticky;top:0;height:100vh;padding:22px 14px;border-right:1px solid var(--line);background:var(--panel);backdrop-filter:blur(18px);display:flex;flex-direction:column;gap:4px;z-index:5}.brand{display:flex;align-items:center;gap:11px;margin:0 8px 18px;font-weight:800;letter-spacing:.13em}.brand i{width:38px;height:38px;display:grid;place-items:center;font-style:normal;border-radius:12px;background:linear-gradient(135deg,var(--accent),var(--accent2));color:#06101c;box-shadow:0 0 20px color-mix(in srgb,var(--accent) 36%,transparent)}.nav{position:static;display:flex;flex-direction:column;gap:4px;overflow:visible;padding:0;background:transparent;backdrop-filter:none}.nav a{display:flex;align-items:center;gap:11px;width:100%;padding:10px 12px;border:0;border-radius:5px;color:var(--muted);background:transparent;font-weight:650}.nav a span:first-child{width:22px;text-align:center;color:var(--accent)}.nav a:hover,.nav a:focus,.nav a[aria-current='location']{background:color-mix(in srgb,var(--accent) 10%,transparent);color:var(--accent);border:0;box-shadow:inset 2px 0 var(--accent);text-shadow:0 0 12px color-mix(in srgb,var(--accent) 45%,transparent)}.sidefoot{margin-top:auto;padding:10px 8px;border-top:1px solid var(--line);color:var(--muted);font:11px/1.6 ui-monospace,Consolas,monospace}"
      "main{width:auto;max-width:none;min-width:0;flex:1;margin:0;padding:26px 30px 56px}.top{align-items:center;padding:0 0 16px;border-bottom:1px solid var(--line);position:relative}.top:after{content:'';position:absolute;left:0;bottom:-1px;width:120px;height:2px;background:linear-gradient(90deg,var(--accent),transparent);box-shadow:0 0 14px var(--accent)}.top h1{font-size:clamp(1.35rem,2.3vw,1.8rem);font-weight:800;letter-spacing:.04em}.top p{color:var(--muted)}.eyebrow{color:var(--accent)}.pill{border-radius:5px;background:color-mix(in srgb,var(--accent) 8%,transparent)}.dot{background:var(--accent3);box-shadow:0 0 8px var(--accent3)}"
      ".nav a{scroll-margin-top:12px}.hero,.card{background:var(--panel);backdrop-filter:blur(14px);border-color:var(--line);border-radius:7px;box-shadow:var(--shadow);color:var(--ink)}.hero{position:relative;overflow:hidden}.hero:after{content:'';position:absolute;right:-80px;top:-120px;width:260px;height:260px;border-radius:50%;background:radial-gradient(circle,color-mix(in srgb,var(--accent) 12%,transparent),transparent 70%);pointer-events:none}.hero h2,.card h2{letter-spacing:.04em}.card h3{color:var(--accent);text-transform:uppercase;letter-spacing:.12em}.layout{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:16px}.layout>section{grid-column:span 12;min-width:0}.wide{grid-column:span 12}.stat{background:color-mix(in srgb,var(--bg) 68%,var(--panel));border-color:var(--line)}.stat .value{color:var(--accent)}input,select,textarea{background:color-mix(in srgb,var(--bg) 78%,var(--panel));color:var(--ink);border-color:var(--line)}button{background:linear-gradient(135deg,var(--accent),var(--accent2));color:#06101c}button.secondary{background:color-mix(in srgb,var(--accent) 12%,var(--panel));color:var(--ink);border:1px solid var(--line)}.diag{background:var(--bg)}.notice{background:color-mix(in srgb,var(--accent) 7%,var(--panel));border-color:var(--accent)}.nav a:focus-visible,button:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--accent);outline-offset:2px}"
      "@media(max-width:900px){body{display:block}aside{position:fixed;inset:auto 0 0;width:100%;height:auto;min-height:58px;flex-direction:row;overflow-x:auto;padding:5px 8px max(5px,env(safe-area-inset-bottom));border:0;border-top:1px solid var(--line);background:var(--panel);backdrop-filter:blur(18px)}.brand,.sidefoot{display:none}.nav{flex-direction:row;gap:2px;width:max-content}.nav a{width:auto;min-width:58px;flex:none;flex-direction:column;gap:0;padding:5px 9px;font-size:12px}.nav a span:first-child{height:20px}.nav a[aria-current='location']{box-shadow:inset 0 2px var(--accent)}main{padding:18px 14px 104px}.layout{grid-template-columns:1fr}.layout>section{grid-column:1}.wide{grid-column:1}.statgrid{grid-template-columns:repeat(2,minmax(0,1fr))}.ota-steps{flex-direction:column}}@media(max-width:480px){.top{display:block}.top .pill{margin-top:10px}.hero,.card{padding:15px}.nav a{min-width:48px;padding-inline:5px}}"
      "</style></head><body>");
    String top;
    top += F("<header class='top'><div><div class='eyebrow'>ROBO DESK / LOCAL CONTROL</div><h1>");
    top += esc(title);
    top += F("</h1><p>Configure the companion, inspect its sensors, and maintain firmware from one dashboard.</p></div>");
    if (full) top += F("<div class='button-row'><div id='connectionPill' class='pill' role='status'><span class='dot'></span><span>Checking connection...</span></div><button id='themeToggle' class='secondary' type='button' aria-label='Toggle color theme'>Theme</button></div>");
    top += F("</header><script>(()=>{const r=document.documentElement,b=document.getElementById('themeToggle'),links=[...document.querySelectorAll('.nav a')];let saved='';try{saved=localStorage.getItem('roboTheme')||''}catch(e){}if(saved==='light'||saved==='dark')r.dataset.theme=saved;const active=()=>{const target=location.hash||'#overview';links.forEach(a=>{if(a.getAttribute('href')===target)a.setAttribute('aria-current','location');else a.removeAttribute('aria-current')})};links.forEach(a=>a.addEventListener('click',()=>{links.forEach(x=>x.removeAttribute('aria-current'));a.setAttribute('aria-current','location')}));window.addEventListener('hashchange',active);active();if(b)b.onclick=()=>{const dark=r.dataset.theme?r.dataset.theme==='dark':matchMedia('(prefers-color-scheme: dark)').matches;const next=dark?'light':'dark';r.dataset.theme=next;try{localStorage.setItem('roboTheme',next)}catch(e){}}})();</script>");
    if (!full) { h += F("<main>"); h += top; return h; }
    h += F("<aside><div class='brand'><i>R</i><span>ROBO<b style='color:var(--accent)'>DESK</b></span></div><nav class='nav' aria-label='Dashboard sections'><a href='#overview'><span>&#8962;</span><span>Beranda</span></a><a href='#behavior'><span>&#9673;</span><span>Wajah</span></a><a href='#voice'><span>&#9684;</span><span>Suara</span></a><a href='#character'><span>&#9835;</span><span>Sonic</span></a>");
    if (brain_) h += F("<a href='#memory'><span>&#8984;</span><span>Ingatan</span></a>");
    h += F("<a href='#diag'><span>&#8767;</span><span>Diagnostik</span></a><a href='#ai'><span>&#9684;</span><span>Jaringan</span></a><a href='#maintenance'><span>&#9881;</span><span>Sistem</span></a></nav><div class='sidefoot'>SONIC CHARACTER<br>ESP32-S3 &middot; LOCAL DASHBOARD</div></aside><main>");
    h += top;
    h += F("<section id='overview' class='hero' aria-labelledby='overviewTitle'><h2 id='overviewTitle'>Beranda · Robot overview</h2><p class='section-intro'>Live readings update while this page is open. Passwords, API keys and conversation transcripts are not returned.</p><div class='statgrid'><div class='stat'><div class='label'>Wi-Fi</div><div id='stWifi' class='value'>—</div><div id='stWifiDetail' class='detail'>waiting</div></div><div class='stat'><div class='label'>Gemini</div><div id='stGemini' class='value'>—</div><div id='stGeminiDetail' class='detail'>waiting</div></div><div class='stat'><div class='label'>Memory</div><div id='stMemory' class='value'>—</div><div id='stMemoryDetail' class='detail'>local facts</div></div><div class='stat'><div class='label'>Free heap</div><div id='stHeap' class='value'>—</div><div class='detail'>bytes</div></div><div class='stat'><div class='label'>Companion</div><div id='stMood' class='value'>—</div><div id='stMoodDetail' class='detail'>state</div></div><div class='stat'><div class='label'>Sensors</div><div id='stSensors' class='value'>—</div><div id='stSensorsDetail' class='detail'>AHT20 · BMP280 · IMU</div></div></div><div class='homegrid'><div class='homeitem'><h3>What RoboDesk is doing</h3><p id='doingNow'>Waiting for live status…</p></div><div class='homeitem'><h3>Needs attention</h3><p id='needsAttention'>Checking readiness…</p></div><div class='homeitem'><h3>Reminder readiness</h3><p id='reminderSummary'>Checking reminders…</p></div><div class='homeitem'><h3>Sensor freshness</h3><p id='sensorSummary'>Checking AHT20, BMP280, and IMU…</p></div></div><p id='statusMessage' class='muted' aria-live='polite'>Loading diagnostics…</p><p id='statusUpdated' class='muted'>Waiting for latest reading…</p><div class='button-row'><button id='talkNow' type='button'>Open conversation</button><button id='pause15' type='button' class='secondary'>Pause initiative 15 min</button><button id='pause60' type='button' class='secondary'>Pause 1 hour</button><button id='resumeProactive' type='button' class='secondary'>Resume initiative</button><label class='inline' for='expressionChoice'>Expression <select id='expressionChoice'><option value='happy'>happy</option><option value='curious'>curious</option><option value='shy'>shy</option><option value='love'>love</option><option value='sad'>sad</option><option value='surprised'>surprised</option><option value='thinking'>thinking</option><option value='agree'>agree</option><option value='disagree'>disagree</option><option value='wink'>wink</option><option value='laugh'>laugh</option><option value='sleepy'>sleepy</option></select></label><button id='expressHappy' type='button' class='secondary'>Test expression</button></div><p id='actionMessage' class='muted' aria-live='polite'>Quick actions use the normal companion arbitration path.</p><details id='diag'><summary>Technical diagnostics</summary><pre id='diagnostics' class='diag'>No diagnostics received yet.</pre></details></section>");
    h += F("<form method='post' action='/save' id='settingsForm'>");
    return h;
  }

  bool sameOriginRequest() const {
    const String host = server_.header("Host");
    const String origin = server_.header("Origin");
    return roboSameOriginHttp(host.c_str(),origin.c_str());
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
      if (!firmwareUpdateControl_ || !firmwareUpdateControl_(firmwareUpdateContext_, true)) { failFirmwareUpdate(409, "Robot is busy or WakeNet is armed. Select Touch-to-talk, save and reboot before retrying"); return; }
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
    const auto startResult=install?githubOta_.requestInstall():githubOta_.requestCheck();
    const bool accepted=startResult==GitHubOtaUpdate::StartResult::Accepted;
    const auto status=githubOta_.snapshot();
    String response = F("{\"accepted\":"); response += accepted ? "true" : "false";
    response += F(",\"message\":\""); response += jsonEsc(status.message);
    response += F("\",\"startResult\":\"");response += roboOtaStartResultName(startResult);response += F("\"}");
    server_.send(accepted?202:(startResult==GitHubOtaUpdate::StartResult::NoMemory?503:409),"application/json",response);
#endif
  }

  void handleGitHubOtaStatus() {
    if (!authorized()) return;
    const auto status=githubOta_.snapshot();
    String response = F("{\"state\":"); response += String(unsigned(status.state));
    response += F(",\"version\":"); response += String(status.version);
    uint32_t tracked = 0;
    githubOta_.trackedVersion(&tracked);
    response += F(",\"installedVersion\":"); response += String(tracked);
    response += F(",\"message\":\""); response += jsonEsc(status.message);
    response += F("\",\"workerActive\":");response += status.active?"true":"false";
    response += F(",\"startResult\":\"");response += roboOtaStartResultName(status.startResult);
    response += F("\",\"stage\":\"");response += jsonEsc(status.stage);
    response += F("\",\"failure\":\"");response += jsonEsc(status.failure);response += F("\"}");
    server_.send(200, "application/json", response);
  }

  String githubOtaCard() {
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    String h = F("<div class='card'><h3>Update from GitHub</h3><p class='muted'>Check a signed release, download to the inactive app slot, then restart. Keep RoboDesk powered and on router Wi-Fi with internet access. The ESP-SR model partition is untouched.</p><ol class='ota-steps'><li id='ghStepCheck'>1 · Check signed release</li><li id='ghStepDownload'>2 · Download & verify</li><li id='ghStepRestart'>3 · Restart & self-test</li></ol><div class='update-meta'><div>Installed version (OTA record)<strong id='ghCurrent'>Checking…</strong></div><div>Available release<strong id='ghVersion'>—</strong></div></div><p id='ghReadiness' class='notice'>Checking Wi-Fi, clock and audio readiness…</p><p id='ghWakeHelp' class='notice warn ota-help'>WakeNet is using the microphone. Before installing, choose <a href='#voice'>Touch-to-talk</a>, save settings and let RoboDesk reboot. Check again afterward.</p><div class='button-row'><button id='ghCheck' type='button' disabled>Check for updates</button><button id='ghInstall' type='button' disabled>Install signed update</button></div><p id='ghStatus' class='muted' role='status'>Loading update status…</p><p class='muted'>The installed version is the OTA version record; a firmware written directly by USB may not have updated that record. Internet and TLS are verified during the check.</p>");
    h += F(R"OTA(<script>(()=>{
      const q=id=>document.getElementById(id),check=q('ghCheck'),install=q('ghInstall'),message=q('ghStatus');
      let pending=false,last=null,requestError='',errorState=-1;
      function render(d){
        const current=window.roboDashboardStatus||{},home=current.home||{};
        const wifi=current.wifi===true,clock=home.clock===true,wake=home.wakeArmed===1||home.wakeArmed===true;
        const busy=d.state===1||d.state===4||d.state===5;
        const ready=wifi&&clock;
        const reason=!window.roboDashboardStatus?'Waiting for live Wi-Fi and clock status.':!wifi?'Connect RoboDesk to router Wi-Fi first.':!clock?'Waiting for synchronized time before secure HTTPS.':'Router Wi-Fi and clock ready. Internet access will be checked securely.';
        q('ghReadiness').textContent=reason;
        q('ghWakeHelp').style.display=wake?'block':'none';
        q('ghCurrent').textContent=d.installedVersion?'v'+d.installedVersion:'Unavailable';
        q('ghVersion').textContent=d.state===3&&d.version?'v'+d.version:d.state===2&&d.version?'v'+d.version:'—';
        check.disabled=pending||busy||!ready;
        install.disabled=pending||d.state!==3||!ready||wake;
        for(const id of ['ghStepCheck','ghStepDownload','ghStepRestart']){q(id).classList.remove('current');q(id).removeAttribute('aria-current');}
        const stage=d.state===4?'ghStepDownload':d.state===5?'ghStepRestart':'ghStepCheck';
        q(stage).classList.add('current');q(stage).setAttribute('aria-current','step');
        if(requestError&&d.state!==errorState)requestError='';
        const text=requestError||d.message||'Update status unavailable.';
        if(message.textContent!==text)message.textContent=text;
      }
      async function refresh(){
        try{const r=await fetch('/api/ota',{cache:'no-store'});if(!r.ok)throw Error();last=await r.json();render(last);}
        catch(e){if(last&&last.state===5)return;const text=requestError||'Could not read update status. Check your dashboard connection.';if(message.textContent!==text)message.textContent=text;check.disabled=true;install.disabled=true;}
      }
      async function send(path){
        pending=true;if(last)render(last);
        try{const r=await fetch(path,{method:'POST'});const d=await r.json();requestError=r.ok?'':d.error||d.message||'Update request rejected.';errorState=last?last.state:-1;
          if(r.ok&&d.message&&message.textContent!==d.message)message.textContent=d.message;
        }catch(e){requestError='Connection lost while starting the update. Check RoboDesk before retrying.';errorState=last?last.state:-1;}
        pending=false;await refresh();
      }
      check.onclick=()=>send('/ota/github/check');install.onclick=()=>send('/ota/github/install');
      window.roboUpdateReady=()=>{if(last)render(last);};
      setInterval(refresh,2500);refresh();
    })();</script></div>)OTA");
    return h;
#else
    return String();
#endif
  }

  void handleRoot() {
    if (!authorized()) return;
    String h = pageHead("RoboDesk Settings");
    h += F("<div class='layout'>");

    h += F("<section id='ai' class='card'><h2>Connectivity & AI</h2><p class='section-intro'>Network priority and Gemini Live credentials. Passwords and API keys are write-only: leave them blank to keep the stored value.</p><h3>Wi-Fi priority</h3><p class='muted'>RoboDesk tries these networks in order and moves to the next if it cannot connect.</p><div class='grid'>");
    h += F("<div class='field'><label for='ssid'>Primary Wi-Fi SSID</label><input id='ssid' name='ssid' maxlength='32' value='"); h += esc(settings_->wifiSsid); h += F("'></div><div class='field'><label for='wpass'>Primary password</label><input id='wpass' type='password' name='wpass' maxlength='64' placeholder='Leave blank to keep current'></div>");
    h += F("<div class='field'><label for='ssid2'>Fallback SSID 2</label><input id='ssid2' name='ssid2' maxlength='32' value='"); h += esc(settings_->wifiSsid2); h += F("'></div><div class='field'><label for='wpass2'>Fallback password 2</label><input id='wpass2' type='password' name='wpass2' maxlength='64' placeholder='Leave blank to keep current'></div>");
    h += F("<div class='field'><label for='ssid3'>Fallback SSID 3</label><input id='ssid3' name='ssid3' maxlength='32' value='"); h += esc(settings_->wifiSsid3); h += F("'></div><div class='field'><label for='wpass3'>Fallback password 3</label><input id='wpass3' type='password' name='wpass3' maxlength='64' placeholder='Leave blank to keep current'></div></div><h3>Gemini Live</h3><div class='grid'>");
    h += F("<div class='field'><label for='rname'>Robot name</label><input id='rname' name='rname' maxlength='31' value='"); h += esc(settings_->robotName); h += F("'></div><div class='field'><label for='gkey'>Gemini API key</label><input id='gkey' type='password' name='gkey' maxlength='159' placeholder='Leave blank to keep current'></div><div class='field'><label for='model'>Conversation model</label><input id='model' name='model' maxlength='47' value='"); h += esc(settings_->geminiModel); h += F("'></div><div class='field'><label for='voice'>Gemini voice</label><input id='voice' name='voice' maxlength='31' value='"); h += esc(settings_->geminiVoice); h += F("'></div><div class='field full'><label for='style'>Speech style</label><textarea id='style' name='style' rows='3' maxlength='240'>"); h += esc(settings_->speechStyle); h += F("</textarea></div></div></section>");

    h += F("<section id='voice' class='card'><h2>Voice interaction</h2><p class='section-intro'>Choose how a conversation starts and tune the bounded speech detector.</p><label for='mode'>Input mode</label><select id='mode' name='mode'><option value='0'"); if(settings_->inputMode==0)h+=F(" selected"); h+=F(">Always listening (legacy setting)</option><option value='1'"); if(settings_->inputMode==1)h+=F(" selected"); h+=F(">Touch: tap to chat</option><option value='2'"); if(settings_->inputMode==2)h+=F(" selected");
#if ROBODESK_WAKEWORD_ENGINE_ENABLE
    h += F(">Wake word (WakeNet)</option></select>");
#else
    h += F(" disabled>Wake word (prepared, dormant in this build)</option></select>");
#endif
    h += F("<div class='notice'><b>Wake-word model:</b> "); h += ROBODESK_WAKEWORD_LABEL; h += F(". Engine build: "); h += ROBODESK_WAKEWORD_ENGINE_ENABLE ? "enabled" : "disabled"; h += F(". The phrase is baked into the ESP-SR model partition; changing this label does not retrain the model.</div><div class='grid'><div class='field'><label for='gain'>Speaker gain ×1000 (50–1000)</label><input id='gain' type='number' name='gain' min='50' max='1000' value='"); h+=String(settings_->speakerGainMilli); h+=F("'></div><div class='field'><label for='guard'>Post-speak guard (ms)</label><input id='guard' type='number' name='guard' min='0' max='2000' value='"); h+=String(settings_->postSpeakGuardMs); h+=F("'></div><div class='field'><label for='vstart'>VAD start ×100</label><input id='vstart' type='number' name='vstart' min='120' max='1000' value='"); h+=String(settings_->vadStartX100); h+=F("'></div><div class='field'><label for='vend'>VAD end ×100</label><input id='vend' type='number' name='vend' min='105' max='800' value='"); h+=String(settings_->vadEndX100); h+=F("'></div><div class='field'><label for='vendms'>End silence (ms)</label><input id='vendms' type='number' name='vendms' min='120' max='2500' value='"); h+=String(settings_->vadEndMs); h+=F("'></div><div class='field'><label for='wakewin'>Wake follow-up (ms)</label><input id='wakewin' type='number' name='wakewin' min='3000' max='60000' value='"); h+=String(settings_->wakeFollowupMs); h+=F("'></div></div></section>");

    h += F("<section id='robot-features' class='card wide'><h2>Robot features</h2><p class='section-intro'>Local sessions, room alerts, briefing and privacy controls. English voice commands: start five/ten/twenty five/forty five minute timer; start/pause/resume/stop focus session.</p><label class='check'><input type='checkbox' name='micmute' value='1'");if(settings_->privacyMicMuted)h+=F(" checked");h+=F("> Microphone privacy mode (software mute)</label><label class='check'><input type='checkbox' name='comfort' value='1'");if(settings_->comfortAlertsEnabled)h+=F(" checked");h+=F("> Room comfort alerts</label><div class='grid'><div class='field'><label>Minimum temperature ×10 °C</label><input type='number' name='cminT' min='-100' max='600' value='");h+=String(settings_->comfortMinTempX10);h+=F("'></div><div class='field'><label>Maximum temperature ×10 °C</label><input type='number' name='cmaxT' min='-100' max='600' value='");h+=String(settings_->comfortMaxTempX10);h+=F("'></div><div class='field'><label>Minimum humidity ×10 %</label><input type='number' name='cminH' min='0' max='1000' value='");h+=String(settings_->comfortMinHumidityX10);h+=F("'></div><div class='field'><label>Maximum humidity ×10 %</label><input type='number' name='cmaxH' min='0' max='1000' value='");h+=String(settings_->comfortMaxHumidityX10);h+=F("'></div></div><label class='check'><input type='checkbox' name='briefing' value='1'");if(settings_->dailyBriefingEnabled)h+=F(" checked");h+=F("> Daily local briefing</label><div class='grid'><div class='field'><label>Briefing minute of day</label><input type='number' name='briefmin' min='0' max='1439' value='");h+=String(settings_->dailyBriefingMinute);h+=F("'></div><div class='field'><label>Focus minutes</label><input type='number' name='pomfocus' min='1' max='90' value='");h+=String(settings_->pomodoroFocusMinutes);h+=F("'></div><div class='field'><label>Short break minutes</label><input type='number' name='pombreak' min='1' max='60' value='");h+=String(settings_->pomodoroBreakMinutes);h+=F("'></div><div class='field'><label>Long break minutes</label><input type='number' name='pomlong' min='1' max='90' value='");h+=String(settings_->pomodoroLongBreakMinutes);h+=F("'></div><div class='field full'><label>Phone notification app allowlist (one package/bundle ID per line)</label><textarea name='notifapps' rows='3' maxlength='191'>");h+=esc(settings_->notificationAllowlist);h+=F("</textarea></div></div><div class='button-row'><button class='secondary' type='submit' formaction='/companion/action' formmethod='post' name='action' value='privacy'>Toggle microphone privacy</button><button class='secondary' type='submit' formaction='/companion/action' formmethod='post' name='action' value='pomodoro_start'>Start Pomodoro</button><button class='secondary' type='submit' formaction='/companion/action' formmethod='post' name='action' value='pomodoro_pause'>Pause</button><button class='secondary' type='submit' formaction='/companion/action' formmethod='post' name='action' value='pomodoro_resume'>Resume</button><button class='secondary' type='submit' formaction='/companion/action' formmethod='post' name='action' value='pomodoro_stop'>Stop</button></div><p class='muted'>The allowlist is stored locally. Phone notification content is transient and is not stored.</p></section>");

    h += F("<section id='character' class='card'><h2>Character & sound</h2><p class='section-intro'>Shape the sonic personality without changing the visual renderer.</p><label class='check'><input type='checkbox' name='sndmaster' value='1'");if(settings_->masterSound)h+=F(" checked");h+=F("> Master sound output</label><label class='check'><input type='checkbox' name='sndspeech' value='1'");if(settings_->speechEnabled)h+=F(" checked");h+=F("> Gemini speech playback</label><label class='check'><input type='checkbox' name='sndsfx' value='1'");if(settings_->characterSfx)h+=F(" checked");h+=F("> Local character SFX</label><div class='grid'><div class='field'><label for='sfxint'>SFX intensity %</label><input id='sfxint' type='number' name='sfxint' min='0' max='100' value='");h+=String(settings_->sfxIntensityX100);h+=F("'></div><div class='field'><label for='sndfreq'>SFX frequency</label><select id='sndfreq' name='sndfreq'><option value='0'");if(settings_->sonicFrequency==0)h+=F(" selected");h+=F(">Low</option><option value='1'");if(settings_->sonicFrequency==1)h+=F(" selected");h+=F(">Normal</option><option value='2'");if(settings_->sonicFrequency==2)h+=F(" selected");h+=F(">Expressive</option></select></div></div><h3>Triggers and quiet hours</h3><label class='check'><input type='checkbox' name='sndwake' value='1'");if(settings_->wakeSfx)h+=F(" checked");h+=F("> Wake acknowledgement</label><label class='check'><input type='checkbox' name='sndtouch' value='1'");if(settings_->touchSfx)h+=F(" checked");h+=F("> Touch and affection sounds</label><label class='check'><input type='checkbox' name='sndmotion' value='1'");if(settings_->motionSfx)h+=F(" checked");h+=F("> Motion, pickup, and shake sounds</label><label class='check'><input type='checkbox' name='sndnotif' value='1'");if(settings_->notificationSfx)h+=F(" checked");h+=F("> Notification and status sounds</label><label class='check'><input type='checkbox' name='quieton' value='1'");if(settings_->quietHoursEnabled)h+=F(" checked");h+=F("> Quiet hours</label><div class='grid'><div class='field'><label for='quietstart'>Quiet start minute</label><input id='quietstart' type='number' name='quietstart' min='0' max='1439' value='");h+=String(settings_->quietStartMin);h+=F("'></div><div class='field'><label for='quietend'>Quiet end minute</label><input id='quietend' type='number' name='quietend' min='0' max='1439' value='");h+=String(settings_->quietEndMin);h+=F("'></div><div class='field'><label for='quietgain'>Quiet SFX gain %</label><input id='quietgain' type='number' name='quietgain' min='0' max='100' value='");h+=String(settings_->quietGainX100);h+=F("'></div></div><p class='muted'>1320 = 22:00 and 420 = 07:00. Speech still answers the user; unsolicited voice is suppressed during quiet hours.</p><h3>Sound test</h3><div class='button-row'>");
    const char* tc[]={"wake","happy","curious","affection","thinking","surprise","sad","sleepy","error","success","pickup","shake"};
    for(unsigned i=0;i<sizeof(tc)/sizeof(tc[0]);i++){h+=F("<button type='submit' class='secondary' formaction='/sound/test' formmethod='post' name='cue' value='");h+=tc[i];h+=F("'>");h+=tc[i];h+=F("</button>");}
    h += F("</div></section>");

    h += F("<section id='behavior' class='card'><h2>Behavior & face</h2><p class='section-intro'>Tune initiative, local diagnostics, identity, and the LivingEyes face controls.</p><label class='check'><input type='checkbox' name='memory' value='1'"); if(settings_->memoryEnabled)h+=F(" checked"); h+=F("> Persistent AI memory</label><label class='check'><input type='checkbox' name='pvisual' value='1'"); if(settings_->proactiveVisual)h+=F(" checked"); h+=F("> Proactive visual reactions</label><label class='check'><input type='checkbox' name='pvoice' value='1'"); if(settings_->proactiveVoice)h+=F(" checked"); h+=F("> Proactive spoken greetings (uses Gemini/API quota)</label><label class='check'><input type='checkbox' name='imetrics' value='1'");if(settings_->interactionMetrics)h+=F(" checked");h+=F("> Local aggregate interaction diagnostics (no transcript or cloud upload)</label><div class='grid'><div class='field'><label for='owner'>Owner preferred name</label><input id='owner' name='owner' maxlength='47' value='"); if(brain_)h+=esc(brain_->ownerName()); h+=F("'></div><div class='field'><label for='tzmin'>Timezone offset from UTC (minutes)</label><input id='tzmin' type='number' name='tzmin' min='-720' max='840' value='"); h+=String(settings_->timezoneOffsetMin);h+=F("'></div></div><p class='muted'>Jakarta = 420. Character clock drives routines and day/night behavior.</p><h3>Living face</h3><label class='check'><input type='checkbox' name='flife' value='1'");if(settings_->faceLifeEnabled)h+=F(" checked");h+=F("> Dynamic face life (attention, micro-motion, elastic eye spacing)</label><label class='check'><input type='checkbox' name='fpupil' value='1'");if(settings_->facePupils)h+=F(" checked");h+=F("> Pupils</label><label class='check'><input type='checkbox' name='fbrow' value='1'");if(settings_->faceBrows)h+=F(" checked");h+=F("> Eyebrows available</label><label class='check'><input type='checkbox' name='fabrow' value='1'");if(settings_->faceAutoBrows)h+=F(" checked");h+=F("> Show eyebrows only when expressive or attentive</label><label class='check'><input type='checkbox' name='flash' value='1'");if(settings_->faceLashes)h+=F(" checked");h+=F("> Eyelashes available</label><label class='check'><input type='checkbox' name='falash' value='1'");if(settings_->faceAutoLashes)h+=F(" checked");h+=F("> Eyelashes only for soft/social states</label><label class='check'><input type='checkbox' name='fpact' value='1'");if(settings_->facePupilLessActing)h+=F(" checked");h+=F("> Strong eye-shape acting when pupils are hidden</label><div class='grid'><div class='field'><label for='fmicro'>Micro-motion %</label><input id='fmicro' type='number' name='fmicro' min='0' max='100' value='");h+=String(settings_->faceMicroX100);h+=F("'></div><div class='field'><label for='fsil'>No-pupil acting %</label><input id='fsil' type='number' name='fsil' min='0' max='135' value='");h+=String(settings_->faceSilhouetteX100);h+=F("'></div><div class='field'><label for='fgaze'>Edge/corner gaze %</label><input id='fgaze' type='number' name='fgaze' min='40' max='125' value='");h+=String(settings_->faceGazeReachX100);h+=F("'></div><div class='field'><label for='mouth'>Mouth policy</label><select id='mouth' name='mouth'><option value='0'");if(settings_->mouthMode==0)h+=F(" selected");h+=F(">Automatic (recommended)</option><option value='1'");if(settings_->mouthMode==1)h+=F(" selected");h+=F(">Always hidden</option><option value='2'");if(settings_->mouthMode==2)h+=F(" selected");h+=F(">Always visible</option></select></div></div><p class='muted'>Automatic mouth stays hidden at rest and appears while speaking or for short special emotes.</p></section>");

    if(brain_){
      h += F("<section id='memory' class='card wide'><h2>Memory & routines</h2><p class='section-intro'>Saved in verified snapshots on this ESP32. Raw transcripts remain in RAM and stored/user-provided text is treated as data, not instructions.</p><h3>Local memory</h3>");
      bool hasMemory=false;
      for(unsigned i=0;i<companion::Memory::Capacity;i++){const auto*e=brain_->memory().entry(i);if(!e)continue;hasMemory=true;h+=F("<div class='memory-item'><b>");h+=esc(e->key);h+=F("</b> <span class='muted'>[");h+=livingeyes::SemanticMemory::kindName(e->kind);h+=F("]</span><br>");h+=esc(e->value);h+=F("<br><span class='muted'>Source ");h+=e->source==companion::Source::Explicit?"Added by you":e->source==companion::Source::UserTranscript?"Your conversation":"Imported memory";h+=F("; protected ");h+=e->pinned?"yes":"no";h+=F("</span><span class='inline'><button type='submit' class='secondary' formaction='/memory/delete' formmethod='post' name='deletekey' value='");h+=esc(e->key);h+=F("'>Delete</button></span></div>");}
      if(!hasMemory)h+=F("<p class='muted'>No local memory entries yet.</p>");
      h += F("<div class='grid'><div class='field'><label for='mkey'>Memory key</label><input id='mkey' name='mkey' maxlength='39'></div><div class='field'><label for='mcat'>Category</label><select id='mcat' name='mcat'><option>note</option><option>profile</option><option>preference</option><option>routine</option><option>place</option><option>event</option></select></div><div class='field full'><label for='mvalue'>Value</label><input id='mvalue' name='mvalue' maxlength='159'></div></div><button type='submit' formaction='/memory/add' formmethod='post'>Save / correct memory</button><button type='submit' class='secondary' formaction='/memory/experiences/clear' formmethod='post' onclick='return confirm(&quot;Clear recent experiences only? Facts and reminders will remain.&quot;)'>Clear experiences</button><button type='submit' class='danger' formaction='/memory/clear' formmethod='post' onclick='return confirm(&quot;Clear all local AI memory?&quot;)'>Clear AI memory</button><h3>Automatic memory</h3><label class='check'><input type='checkbox' name='automem' value='1'");if(settings_->autoMemory)h+=F(" checked");h+=F("> Save clear user statements automatically</label><div class='grid'><div class='field'><label for='summarymodel'>Summary model</label><input id='summarymodel' name='summarymodel' maxlength='47' value='");h+=esc(settings_->summaryModel);h+=F("'></div><div class='field'><label for='bgmax'>Background requests per day (0 disables)</label><input id='bgmax' type='number' name='bgmax' min='0' max='12' value='");h+=String(settings_->backgroundDailyLimit);h+=F("'></div></div><p class='muted'>Initiative is bounded: invitations are rate-limited and respect presence and quiet hours.</p><h3>Recent experiences</h3>");
      bool hasSummary=false; for(auto&summary:brain_->companionState().summaries)if(summary.id){hasSummary=true;h+=F("<p class='memory-item'>");h+=esc(summary.text);h+=F("</p>");} if(!hasSummary)h+=F("<p class='muted'>No recent experiences.</p>");
      h += F("<h3>Reminders and routines</h3>");
      bool hasReminder=false; for(auto&r:brain_->companionState().reminders)if(r.id){hasReminder=true;h+=F("<div class='reminder-item'>");h+=esc(r.text);h+=F(" <button type='submit' class='secondary inline' formaction='/reminder/cancel' formmethod='post' name='rid' value='");h+=String(r.id);h+=F("'>Cancel</button></div>");} if(!hasReminder)h+=F("<p class='muted'>No active reminders.</p>");
      h += F("<div class='grid'><div class='field full'><label for='rtext'>Reminder text</label><input id='rtext' name='rtext' maxlength='111'></div><div class='field'><label for='rkind'>Kind</label><select id='rkind' name='rkind'><option value='timer'>Timer</option><option value='once'>Once (UTC epoch)</option><option value='daily'>Daily routine</option></select></div><div class='field'><label for='rseconds'>Timer seconds</label><input id='rseconds' name='rseconds' type='number' min='1' max='86400' value='60'></div><div class='field'><label for='rwhenLocal'>One-time date and time</label><input id='rwhenLocal' type='datetime-local'><input id='repoch' name='repoch' type='hidden'><span class='muted'>Uses this browser's local time and converts to UTC.</span></div><div class='field'><label for='rclock'>Daily routine time</label><input id='rclock' type='time' value='08:00'><input id='rminute' name='rminute' type='hidden' value='480'><span class='muted'>Robot local time after clock sync.</span></div></div><p id='reminderFeedback' class='muted' aria-live='polite'></p><button type='submit' formaction='/reminder/add' formmethod='post'>Save reminder</button><p class='muted'>Calendar schedules wait for synchronized time. Timers run while powered. Offline alerts use the screen and local sound.</p></section>");
    }

    h += F("<section class='card wide'><h2>Save settings</h2><div class='notice warn'>Saving applies network, Gemini, audio, behavior, and face settings after a reboot. Keep this dashboard on a trusted local network.</div><label for='pin'>Dashboard password</label><input id='pin' type='password' name='pin' maxlength='31' placeholder='Leave blank to keep current'><p class='muted'>Username is <b>admin</b>. Use a long unique PIN and do not expose this HTTP dashboard to public/shared Wi-Fi.</p><button type='submit'>Save & reboot</button></section></form></div>");

    h += F("<section id='maintenance' class='card wide'><h2>Maintenance</h2><p class='section-intro'>Actions below are separate from settings. Firmware updates are signed and do not write the ESP-SR model partition.</p>");
    h += F("<section class='card'><h3>Phone notification pairing</h3><p class='muted'>Only one approved phone can connect. Start a 60-second pairing window, then approve the detected BLE address here. Pairing replaces the previous phone.</p><p id='phone-pair-state' class='notice'>Loading pairing status...</p><form method='post' action='/companion/action'><button class='secondary' name='action' value='ble_pair'>Forget current phone and start pairing</button></form><form method='post' action='/companion/action'><button class='secondary' name='action' value='ble_approve'>Approve detected phone</button></form><script>(async function(){const e=document.getElementById('phone-pair-state');async function poll(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json(),p=d.phoneBridge||{};e.textContent=p.candidate?'Phone detected: '+p.candidate+' — approve only if this is your phone.':p.pairing?'Pairing window is open; waiting for a phone.':p.paired?'Approved phone saved.':'No phone paired.';}catch(_){e.textContent='Pairing status unavailable.';}}poll();setInterval(poll,3000);})();</script></section>");
    h += F("<section class='card wide'><h2>Recent phone notifications</h2><div id='phone-notifications' class='muted'>Waiting for an allowlisted app over encrypted BLE.</div><script>(async function(){const e=document.getElementById('phone-notifications');async function poll(){try{const r=await fetch('/api/notifications',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json(),a=d.notifications||[];e.textContent=a.length?a.map(n=>n.app+': '+(n.title?n.title+' — ':'')+n.snippet).join(' · '):'No recent notifications.';}catch(_){e.textContent='Notification status unavailable.';}}poll();setInterval(poll,5000);})();</script></section>");
    h += githubOtaCard();
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    h += F("<div class='card'><h3>Signed local firmware update</h3><p class='muted'>Use only on a trusted network. Select one signed RoboDesk ESP32-S3 application .bin and its matching .sig file. The robot accepts updates only while audio is idle and verifies the signature before staging the inactive application slot. If WakeNet is armed, choose Touch-to-talk above, save, and let RoboDesk reboot first.</p><label for='otaBin'>Application firmware (.bin)</label><input id='otaBin' type='file' accept='.bin,application/octet-stream'><label for='otaSig'>Signature (.sig)</label><input id='otaSig' type='file' accept='.sig,text/plain'><button id='otaStart' type='button'>Install signed firmware</button><label for='otaProgress'>Upload transfer progress (verification follows)</label><progress id='otaProgress' class='progress' max='100' value='0'>0%</progress><p id='otaResult' class='muted' aria-live='polite'></p><script>document.getElementById('otaStart').onclick=async function(){const b=document.getElementById('otaBin').files[0],s=document.getElementById('otaSig').files[0],r=document.getElementById('otaResult'),p=document.getElementById('otaProgress'),button=this;if(!b||!s){r.textContent='Choose both files first.';return;}let sig;try{sig=(await s.text()).trim();}catch(e){r.textContent='Could not read signature.';return;}if(!/^[1-9][0-9]{0,9}:[0-9a-f]{128,160}$/i.test(sig)){r.textContent='Signature file is malformed.';return;}const f=new FormData();f.append('firmware',b,b.name);const x=new XMLHttpRequest();x.open('POST','/ota');x.setRequestHeader('X-Robo-Signature',sig);x.upload.onprogress=e=>{if(e.lengthComputable){p.style.display='block';p.value=Math.floor(100*e.loaded/e.total);r.textContent=p.value===100?'Transfer complete. RoboDesk is verifying and staging the signed image…':'Uploading signed firmware: '+p.value+'% transferred.';}};x.onload=()=>{r.textContent=x.responseText||('Update failed ('+x.status+').');button.disabled=false;};x.onerror=()=>{r.textContent='Connection lost. Check RoboDesk status before retrying; the upload may have completed.';button.disabled=false;};button.disabled=true;p.value=0;p.style.display='block';r.textContent='Uploading signed firmware…';x.send(f);};</script></div>");
#else
    h += F("<div class='card'><h3>Signed local firmware update</h3><p class='muted'>OTA is disabled because rollback support is not enabled in this firmware build.</p></div>");
#endif
    h += F("<div class='button-row'><form method='post' action='/reboot'><button class='secondary'>Reboot</button></form><form method='post' action='/factory' onsubmit=\"return confirm('Erase all saved settings?')\"><button class='danger'>Factory reset</button></form></div><p class='footer'>RoboDesk keeps raw conversation transcripts in RAM only. Host tests cover bounded logic; physical sensor, OLED, audio, Wi-Fi, and OTA qualification require device evidence.</p></section></main><script>(()=>{const q=id=>document.getElementById(id);const pill=q('connectionPill'),msg=q('statusMessage'),updated=q('statusUpdated'),diag=q('diagnostics'),actionMessage=q('actionMessage');window.roboDashboardStatus=null;let timer=0;async function action(name,value){actionMessage.textContent='Sending '+name+'…';try{const body=new URLSearchParams({action:name});if(value)body.set('value',value);const r=await fetch('/companion/action',{method:'POST',body:body});const d=await r.json();actionMessage.textContent=d.message||d.reason||((d.accepted?'Done: ':'Rejected: ')+name);}catch(e){actionMessage.textContent='Action unavailable; check that the robot is online.';}}q('talkNow').onclick=()=>action('talk');q('pause15').onclick=()=>action('pause','900');q('pause60').onclick=()=>action('pause','3600');q('resumeProactive').onclick=()=>action('resume');q('expressHappy').onclick=()=>action('expression',q('expressionChoice').value);if(q('rkind')){function syncReminderFields(){const k=q('rkind').value;q('rseconds').disabled=k!=='timer';q('rwhenLocal').disabled=k!=='once';q('rclock').disabled=k!=='daily';}q('rkind').onchange=syncReminderFields;q('rclock').onchange=()=>{const p=q('rclock').value.split(':');if(p.length===2)q('rminute').value=Number(p[0])*60+Number(p[1]);};q('settingsForm').addEventListener('submit',e=>{const k=q('rkind').value;if(k==='once'&&q('rwhenLocal').value){const stamp=Date.parse(q('rwhenLocal').value);if(Number.isFinite(stamp))q('repoch').value=Math.floor(stamp/1000);}if(k==='daily'&&q('rclock').value)q('rclock').onchange();});q('settingsForm').addEventListener('submit',async e=>{if(!e.submitter||e.submitter.getAttribute('formaction')!=='/reminder/add')return;e.preventDefault();const feedback=q('reminderFeedback');feedback.textContent='Saving reminder…';try{const r=await fetch('/reminder/add',{method:'POST',body:new URLSearchParams(new FormData(q('settingsForm')))});const d=await r.json();if(!r.ok)throw Error(d.status||d.error||'The reminder was rejected.');feedback.textContent='Reminder saved. Refreshing the list…';window.setTimeout(()=>window.location.reload(),900);}catch(err){feedback.textContent='Could not save reminder: '+err.message;}});syncReminderFields();}function text(v,fallback){return v===undefined||v===null||v===''?fallback:String(v);}function update(d){window.roboDashboardStatus=d;if(window.roboUpdateReady)window.roboUpdateReady();q('stWifi').textContent=d.wifi?'Router online':d.setupAp?'Setup AP':'No router';q('stWifiDetail').textContent=d.wifi?text(d.activeSsid,'Connected'):d.setupAp?'Local setup only':'Reconnect Wi-Fi';const audio=d.audio||{};q('stGemini').textContent=audio.ready?'Live ready':audio.wss?'Connecting':audio.geminiConfigured?'Not connected':'Not configured';q('stGeminiDetail').textContent=audio.geminiConfigured?text(d.model,'Model unknown'):'Add API key in settings';q('stMemory').textContent=text(d.memoryCount,'—');q('stMemoryDetail').textContent='local facts';q('stHeap').textContent=text(d.heap,'—');q('stMood').textContent=text(d.mood,'—');q('stMoodDetail').textContent=text((d.home||{}).conversation,'local companion');const ss=d.sensors||{};const sensorNames=['aht20','bmp280','imu'];const sensorOk=sensorNames.filter(k=>ss[k]&&ss[k].valid).length;q('stSensors').textContent=sensorOk+'/'+sensorNames.length+' fresh';q('stSensorsDetail').textContent=sensorOk===sensorNames.length?'all readings current':'inspect diagnostics';const h=d.home||{},rem=d.reminders||{};const attention=[];if(!d.wifi)attention.push('router Wi-Fi offline; GitHub update unavailable');if(d.wifi&&audio.geminiConfigured&&!audio.ready)attention.push('Gemini not ready');if((d.companion||{}).storageHealthy===false)attention.push('local storage needs attention');if(h.wakeArmed)attention.push('WakeNet armed; choose Touch-to-talk before installing');if(h.dnd)attention.push('initiative paused');if(h.clock===false)attention.push('clock not synchronized');if(h.wakeAvailable===false&&h.mode==='wake')attention.push('wake-word unavailable');if(rem.deferred)attention.push(rem.deferred+' reminder deferred');const conversation=text(h.conversation,'offline'),clockText=h.clock===false?'clock not synced':('local time '+String(Math.floor(Number(h.minute||0)/60)).padStart(2,'0')+':'+String(Number(h.minute||0)%60).padStart(2,'0'));q('doingNow').textContent=h.dnd?'Quiet mode is active; user-triggered conversation still works.':h.window?'Conversation window is open.':conversation==='Ready'?'Ready for a conversation.':'Standing by in '+text(h.mode,'touch')+' mode.';q('doingNow').textContent+=' · '+clockText;q('needsAttention').textContent=attention.length?attention.join(' · '):'Nothing urgent. Companion services are ready.';const nextReminder=rem.nextTimerMs?' · next timer in '+Math.ceil(Number(rem.nextTimerMs)/60000)+' min':(rem.nextDailyMinute<1440?' · daily at '+String(Math.floor(Number(rem.nextDailyMinute)/60)).padStart(2,'0')+':'+String(Number(rem.nextDailyMinute)%60).padStart(2,'0'):'');q('reminderSummary').textContent=text(rem.active,0)+' active reminder(s)'+nextReminder+(rem.deferred?' · '+rem.deferred+' deferred':'')+(h.clock===false?' · waiting for clock sync':'');q('sensorSummary').textContent=sensorOk===sensorNames.length?'AHT20, BMP280, and IMU are fresh.':sensorOk+'/'+sensorNames.length+' sensor groups are fresh; open diagnostics for ages.';const summary=attention.length?attention.join(' · '):'Companion ready · '+text(h.mode,'touch')+' input · '+conversation+' · '+text(rem.active,0)+' reminders';if(msg.textContent!==summary)msg.textContent=summary;updated.textContent='Last updated '+new Date().toLocaleTimeString();diag.textContent=JSON.stringify(d,null,2);pill.querySelector('.dot').className='dot '+(d.wifi?'good':d.setupAp?'':'bad');pill.querySelector('span:last-child').textContent=d.wifi?'Router connected':d.setupAp?'Local setup AP':'No router connection';}async function refresh(){if(document.hidden){timer=0;return;}try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error();update(await r.json());}catch(e){window.roboDashboardStatus=null;if(window.roboUpdateReady)window.roboUpdateReady();pill.querySelector('.dot').className='dot bad';pill.querySelector('span:last-child').textContent='Status unavailable';const warning='Could not refresh diagnostics; live readings may be stale.';if(msg.textContent!==warning)msg.textContent=warning;updated.textContent='Last successful reading may be stale.';}timer=window.setTimeout(refresh,5000);}document.addEventListener('visibilitychange',()=>{if(!document.hidden){if(timer)window.clearTimeout(timer);timer=0;refresh();}});refresh();})();</script></body></html>");
    server_.send(200, "text/html; charset=utf-8", h);
  }

  void handleStatus() {
    if (!authorized()) return;
    String s = F("{\"wifi\":"); s += WiFi.status() == WL_CONNECTED ? "true" : "false";
    s += F(",\"setupAp\":"); s += apStarted_ ? "true" : "false";
    s += F(",\"rssi\":"); s += String(WiFi.RSSI());
    s += F(",\"heap\":"); s += String(ESP.getFreeHeap());
    String activeSsid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("");
    s += F(",\"ssid\":\""); s += jsonEsc(settings_->wifiSsid); s += F("\",\"activeSsid\":\""); s += jsonEsc(activeSsid.c_str()); s += F("\",\"model\":\""); s += jsonEsc(settings_->geminiModel); s += F("\",\"voice\":\""); s += jsonEsc(settings_->geminiVoice);
    if (brain_) {
      s += F("\",\"memoryCount\":"); s += String(brain_->memory().count());
      s += F(",\"mood\":\""); s += jsonEsc(brain_->mind().moodName()); s += F("\"");
    } else {
      s += F("\",\"memoryCount\":0,\"mood\":\"unknown\"");
    }
    char diagnostics[2200] = {0};
    if (diagnostics_) diagnostics_(diagnosticsContext_, diagnostics, sizeof(diagnostics));
    if (diagnostics[0]) s += diagnostics;
    s += F(",\"features\":{\"micMuted\":");s += settings_->privacyMicMuted?"true":"false";
    s += F(",\"comfortAlerts\":");s += settings_->comfortAlertsEnabled?"true":"false";
    s += F(",\"briefing\":");s += settings_->dailyBriefingEnabled?"true":"false";
    s += F(",\"pomodoroInterrupted\":");s += settings_->pomodoroInterrupted?"true":"false";
    s += F("}");
    s += F("}");
    server_.send(200, "application/json", s);
  }

  void handleSave() {
    if (!authorizeMutation() || !settings_ || !store_) return;
    RuntimeSettings next = *settings_;
    if (server_.hasArg("ssid")) RuntimeSettings::copy(next.wifiSsid, sizeof(next.wifiSsid), server_.arg("ssid").c_str());
    if (server_.hasArg("wpass") && server_.arg("wpass").length()) RuntimeSettings::copy(next.wifiPassword, sizeof(next.wifiPassword), server_.arg("wpass").c_str());
    if (server_.hasArg("ssid2")) RuntimeSettings::copy(next.wifiSsid2, sizeof(next.wifiSsid2), server_.arg("ssid2").c_str());
    if (server_.hasArg("wpass2") && server_.arg("wpass2").length()) RuntimeSettings::copy(next.wifiPassword2, sizeof(next.wifiPassword2), server_.arg("wpass2").c_str());
    if (server_.hasArg("ssid3")) RuntimeSettings::copy(next.wifiSsid3, sizeof(next.wifiSsid3), server_.arg("ssid3").c_str());
    if (server_.hasArg("wpass3") && server_.arg("wpass3").length()) RuntimeSettings::copy(next.wifiPassword3, sizeof(next.wifiPassword3), server_.arg("wpass3").c_str());
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
    if (brain_ && server_.hasArg("owner") && server_.arg("owner").length() && strcmp(server_.arg("owner").c_str(),brain_->ownerName())!=0 && !brain_->manualRemember("owner.name",server_.arg("owner").c_str(),"profile",millis())) {server_.send(500,"text/plain","Owner name was not saved");return;}
    next.autoMemory=server_.hasArg("automem")?1:0;
    if(server_.hasArg("summarymodel")&&server_.arg("summarymodel").length())RuntimeSettings::copy(next.summaryModel,sizeof(next.summaryModel),server_.arg("summarymodel").c_str());
    if(server_.hasArg("bgmax"))next.backgroundDailyLimit=uint8_t(parseU16(server_.arg("bgmax"),next.backgroundDailyLimit,0,12));
    next.memoryEnabled=server_.hasArg("memory")?1:0; next.proactiveVisual=server_.hasArg("pvisual")?1:0; next.proactiveVoice=server_.hasArg("pvoice")?1:0;next.interactionMetrics=server_.hasArg("imetrics")?1:0;
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
    next.privacyMicMuted=server_.hasArg("micmute")?1:0;
    next.comfortAlertsEnabled=server_.hasArg("comfort")?1:0;
    if(server_.hasArg("cminT")){long v=server_.arg("cminT").toInt();if(v< -100||v>600){server_.send(400,"text/plain","Minimum temperature is outside its supported range");return;}next.comfortMinTempX10=int16_t(v);}
    if(server_.hasArg("cmaxT")){long v=server_.arg("cmaxT").toInt();if(v< -100||v>600){server_.send(400,"text/plain","Maximum temperature is outside its supported range");return;}next.comfortMaxTempX10=int16_t(v);}
    if(server_.hasArg("cminH"))next.comfortMinHumidityX10=parseU16(server_.arg("cminH"),next.comfortMinHumidityX10,0,1000);
    if(server_.hasArg("cmaxH"))next.comfortMaxHumidityX10=parseU16(server_.arg("cmaxH"),next.comfortMaxHumidityX10,0,1000);
    next.dailyBriefingEnabled=server_.hasArg("briefing")?1:0;
    if(server_.hasArg("briefmin"))next.dailyBriefingMinute=parseU16(server_.arg("briefmin"),next.dailyBriefingMinute,0,1439);
    if(server_.hasArg("pomfocus"))next.pomodoroFocusMinutes=uint8_t(parseU16(server_.arg("pomfocus"),next.pomodoroFocusMinutes,1,90));
    if(server_.hasArg("pombreak"))next.pomodoroBreakMinutes=uint8_t(parseU16(server_.arg("pombreak"),next.pomodoroBreakMinutes,1,60));
    if(server_.hasArg("pomlong"))next.pomodoroLongBreakMinutes=uint8_t(parseU16(server_.arg("pomlong"),next.pomodoroLongBreakMinutes,1,90));
    if(server_.hasArg("notifapps"))RuntimeSettings::copy(next.notificationAllowlist,sizeof(next.notificationAllowlist),server_.arg("notifapps").c_str());
    if(next.comfortMinTempX10>=next.comfortMaxTempX10||next.comfortMinHumidityX10>=next.comfortMaxHumidityX10){server_.send(400,"text/plain","Comfort minimums must be lower than maximums");return;}
    if (server_.hasArg("mode")) {
      const int requested = server_.arg("mode").toInt();
      if (requested == 1) next.inputMode = RuntimeSettings::TouchToTalk;
#if ROBODESK_WAKEWORD_ENGINE_ENABLE
      else if (requested == 2) next.inputMode = RuntimeSettings::WakeWord;
#endif
      else next.inputMode = RuntimeSettings::TouchToTalk;
    }

    if (!store_->save(next)) { server_.send(500, "text/plain", "Failed to save settings"); return; }
    *settings_ = next;
    server_.send(200, "text/html; charset=utf-8", pageHead("Settings saved", false) + F("<div class='card'><p>Saved. RoboDesk will reboot and apply the new settings.</p></div></main></body></html>"));
    rebootAt_ = millis() + 1200;
  }

  void handleSoundTest() {
    if (!authorizeMutation()) return;
    if (!soundTest_ || !server_.hasArg("cue")) { server_.send(503,"text/plain","Sound test unavailable"); return; }
    const String cue=server_.arg("cue");const bool ok=soundTest_(soundTestContext_,cue.c_str());
    server_.sendHeader("Location","/",true);server_.send(ok?302:409,"text/plain",ok?"":"Sound busy or disabled");
  }

  void handleFactory() {
    if (!authorizeMutation() || !store_) return;
    bool ok = store_->clear(); if(brain_)ok = brain_->clearAllState(millis()) && ok;
    server_.send(ok ? 200 : 500, "text/plain", ok ? "Factory settings cleared; rebooting" : "Failed to clear settings");
    if (ok) rebootAt_ = millis() + 800;
  }
  void handleMemoryClear() {
    if (!authorizeMutation() || !brain_) return;
    const bool ok=brain_->clearMemory(millis());
    server_.send(ok?200:500,"text/plain",ok?"AI memory cleared":"Failed to clear AI memory");
  }

  void handleExperiencesClear() {
    if (!authorizeMutation() || !brain_) return;
    const bool ok=brain_->clearExperiences(millis());
    server_.send(ok?200:500,"text/plain",ok?"Recent experiences cleared":"Failed to clear recent experiences");
  }

  void handleCompanionAction() {
    if (!authorizeMutation()) return;
    if (!companionAction_ || !server_.hasArg("action")) { server_.send(503,"application/json",R"({"accepted":false,"reason":"unavailable"})"); return; }
    const String action=server_.arg("action");
    const String value=server_.hasArg("value")?server_.arg("value"):String();
    char result[256]={0};
    const bool ok=companionAction_(companionActionContext_,action.c_str(),value.c_str(),result,sizeof(result));
    if (!result[0]) snprintf(result,sizeof(result),"{\"accepted\":%s}",ok?"true":"false");
    server_.send(ok?200:409,"application/json",result);
  }

  void handleMemoryAdd() {
    if (!authorizeMutation() || !brain_) return;
    if(!server_.hasArg("mkey")||!server_.hasArg("mvalue")){server_.send(400,"text/plain","Missing key/value");return;}
    String k=server_.arg("mkey"),v=server_.arg("mvalue"),c=server_.hasArg("mcat")?server_.arg("mcat"):String("note");
    if(!k.length()||!v.length()){server_.send(400,"text/plain","Empty key/value");return;}
    bool ok=brain_->manualRemember(k.c_str(),v.c_str(),c.c_str(),millis());
    server_.sendHeader("Location","/",true);server_.send(ok?302:500,"text/plain",ok?"":"Failed");
  }

  void handleMemoryDelete(){if(!authorizeMutation()||!brain_)return;if(!server_.hasArg("deletekey")){server_.send(400,"text/plain","Missing key");return;}bool ok=brain_->forget(server_.arg("deletekey").c_str(),millis());server_.send(ok?200:500,"text/plain",ok?"Deleted from memory and recovery":"Deletion failed or key missing");}
  void handleMemoryList(){if(!authorized()||!brain_)return;JsonDocument doc;auto facts=doc["facts"].to<JsonArray>();for(auto&e:brain_->memory().facts)if(e.id){auto item=facts.add<JsonObject>();item["key"]=e.key;item["value"]=e.value;item["source"]=unsigned(e.source);item["timestamp"]=e.timestamp;item["category"]=livingeyes::SemanticMemory::kindName(e.kind);item["importance"]=e.importance;item["protected"]=e.pinned;}auto summaries=doc["summaries"].to<JsonArray>();for(auto&s:brain_->companionState().summaries)if(s.id){auto item=summaries.add<JsonObject>();item["id"]=s.id;item["text"]=s.text;item["timestamp"]=s.timestamp;item["sourceTurn"]=s.sourceTurn;}String body;serializeJson(doc,body);server_.send(200,"application/json",body);}
  void handleReminderList(){if(!authorized()||!brain_)return;char result[2400];brain_->executeTool("list_reminders","{}",0,result,sizeof(result));server_.send(200,"application/json",result);}
  void handleNotifications(){if(!authorized())return;JsonDocument doc;auto list=doc["notifications"].to<JsonArray>();if(notifications_){notifications_->expire(millis());for(unsigned i=0;i<PhoneNotificationBridge::Capacity;++i){const auto&n=notifications_->at(i);if(!n.id)continue;auto item=list.add<JsonObject>();item["id"]=n.id;item["appId"]=n.appId;item["app"]=n.app;item["title"]=n.title;item["snippet"]=n.snippet;item["ageMs"]=uint32_t(millis()-n.receivedAt);}}String body;serializeJson(doc,body);server_.send(200,"application/json",body);}
  void handleReminderAdd(){if(!authorizeMutation()||!brain_)return;JsonDocument args;args["text"]=server_.arg("rtext");args["kind"]=server_.arg("rkind");args["seconds"]=server_.arg("rseconds").toInt();args["epoch"]=server_.arg("repoch").toInt();args["minute"]=server_.arg("rminute").toInt();String body;serializeJson(args,body);char result[512];brain_->executeTool("create_reminder",body.c_str(),0,result,sizeof(result));server_.send(strstr(result,"completed")?200:400,"application/json",result);}
  void handleReminderCancel(){if(!authorizeMutation()||!brain_)return;JsonDocument args;args["id"]=server_.arg("rid").toInt();String body;serializeJson(args,body);char result[512];brain_->executeTool("cancel_reminder",body.c_str(),0,result,sizeof(result));server_.send(strstr(result,"completed")?200:400,"application/json",result);}

};
