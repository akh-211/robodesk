#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>
#include <string.h>

struct RuntimeSettings {
  enum InputMode : uint8_t { AlwaysListening = 0, TouchToTalk = 1, WakeWord = 2 };

  char wifiSsid[33];
  char wifiPassword[65];
  char wifiSsid2[33];
  char wifiPassword2[65];
  char wifiSsid3[33];
  char wifiPassword3[65];
  char geminiApiKey[160];
  char geminiModel[48];
  char geminiVoice[32];
  char summaryModel[48];
  uint8_t autoMemory;
  uint8_t backgroundDailyLimit;
  char adminPin[32];
  char speechStyle[241];
  char robotName[32];


  uint16_t speakerGainMilli;       // 450 = 0.45
  uint16_t vadStartX100;           // 350 = 3.50x noise floor
  uint16_t vadEndX100;             // 190 = 1.90x noise floor
  uint16_t vadEndMs;               // silence before end-of-turn
  uint16_t postSpeakGuardMs;
  uint16_t wakeFollowupMs;          // follow-up listening window after wake trigger
  int16_t timezoneOffsetMin;
  uint8_t inputMode;
  uint8_t memoryEnabled;
  uint8_t proactiveVisual;
  uint8_t proactiveVoice;
  uint8_t interactionMetrics;
  uint8_t privacyMicMuted;
  uint8_t comfortAlertsEnabled;
  int16_t comfortMinTempX10;
  int16_t comfortMaxTempX10;
  uint16_t comfortMinHumidityX10;
  uint16_t comfortMaxHumidityX10;
  uint8_t dailyBriefingEnabled;
  uint16_t dailyBriefingMinute;
  uint8_t pomodoroFocusMinutes;
  uint8_t pomodoroBreakMinutes;
  uint8_t pomodoroLongBreakMinutes;
  uint8_t pomodoroWasActive;
  uint8_t pomodoroInterrupted;
  char notificationAllowlist[192];
  char phoneBlePeer[18];
  uint8_t faceLifeEnabled;
  uint8_t facePupils;
  uint8_t faceBrows;
  uint8_t faceLashes;
  uint8_t faceAutoBrows;
  uint8_t faceAutoLashes;
  uint8_t mouthMode;               // 0 automatic, 1 hidden, 2 always
  uint8_t facePupilLessActing;     // silhouette remains expressive without pupils
  uint16_t faceMicroX100;          // 70 = 0.70
  uint16_t faceSilhouetteX100;     // 90 = 0.90 acting gain
  uint16_t faceGazeReachX100;      // 100 = reach OLED sides/corners

  // v0.15 Sonic Character
  uint8_t masterSound;             // speaker output master switch
  uint8_t speechEnabled;           // Gemini native audio playback
  uint8_t characterSfx;            // local procedural character sounds
  uint8_t wakeSfx;
  uint8_t touchSfx;
  uint8_t motionSfx;
  uint8_t notificationSfx;
  uint8_t sonicFrequency;          // 0 low, 1 normal, 2 expressive
  uint8_t quietHoursEnabled;
  uint16_t sfxIntensityX100;       // 0..100
  uint16_t quietStartMin;          // local minute of day
  uint16_t quietEndMin;
  uint16_t quietGainX100;          // 0..100 applied to local SFX
  // Append-only: keep the original migration-backup prefix readable.
  uint8_t phonePlatform;          // 0 Android bridge, 1 iPhone ANCS
  uint8_t navigationEnabled;
  uint8_t characterIntensity;     // 0 subtle, 1 calm active, 2 expressive
  char notificationSpeechAllowlist[192]; // explicit permission for cloud readout

  RuntimeSettings() { clear(); }

  void clear() {
    memset(this, 0, sizeof(*this));
    copy(geminiModel, sizeof(geminiModel), "gemini-3.8-live");
    copy(geminiVoice, sizeof(geminiVoice), "Iapetus");
    copy(adminPin, sizeof(adminPin), "robodesk");
    copy(speechStyle, sizeof(speechStyle),
         "Speak natural conversational Indonesian at a normal speaking pace. Use concise sentences and short natural pauses. Do not deliberately slow down unless the user explicitly asks.");
    copy(robotName, sizeof(robotName), "RoboDesk");
    navigationEnabled=1;characterIntensity=1;
    speakerGainMilli = 450;
    vadStartX100 = 350;
    vadEndX100 = 190;
    vadEndMs = 600;
    postSpeakGuardMs = 300;
    wakeFollowupMs = 15000;
    timezoneOffsetMin = 420;
    comfortMinTempX10=180; comfortMaxTempX10=300;
    comfortMinHumidityX10=300; comfortMaxHumidityX10=700;
    dailyBriefingMinute=480;
    pomodoroFocusMinutes=25; pomodoroBreakMinutes=5; pomodoroLongBreakMinutes=15;
    inputMode = WakeWord;
    autoMemory = 1;
    backgroundDailyLimit = 12;
    copy(summaryModel,sizeof(summaryModel),"gemini-3.5-flash-lite");
    memoryEnabled = 1;
    proactiveVisual = 1;
    proactiveVoice = 1;
    faceLifeEnabled = 1;
    facePupils = 1;
    faceBrows = 1;
    faceLashes = 0;
    faceAutoBrows = 1;
    faceAutoLashes = 1;
    mouthMode = 0;
    facePupilLessActing = 1;
    faceMicroX100 = 70;
    faceSilhouetteX100 = 90;
    faceGazeReachX100 = 100;
    masterSound = 1;
    speechEnabled = 1;
    characterSfx = 1;
    wakeSfx = 1;
    touchSfx = 1;
    motionSfx = 1;
    notificationSfx = 1;
    sonicFrequency = 1;
    quietHoursEnabled = 1;
    sfxIntensityX100 = 68;
    quietStartMin = 22 * 60;
    quietEndMin = 7 * 60;
    quietGainX100 = 24;
  }

  static void copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
  }

  bool wifiConfigured() const {
    return wifiNetworkConfigured(0) || wifiNetworkConfigured(1) || wifiNetworkConfigured(2);
  }
  bool wifiNetworkConfigured(uint8_t index) const {
    const char* ssid = wifiSsidAt(index);
    return ssid && ssid[0] && strcmp(ssid, "CHANGE_ME") != 0 && strcmp(ssid, "YOUR_WIFI_NAME") != 0;
  }
  int8_t findWifiNetworkIndex(uint8_t firstIndex) const {
    for (uint8_t offset=0;offset<3;++offset) {
      const uint8_t index=uint8_t((firstIndex+offset)%3);
      if (wifiNetworkConfigured(index)) return int8_t(index);
    }
    return -1;
  }
  const char* wifiSsidAt(uint8_t index) const {
    switch (index) { case 0: return wifiSsid; case 1: return wifiSsid2; case 2: return wifiSsid3; default: return nullptr; }
  }
  const char* wifiPasswordAt(uint8_t index) const {
    switch (index) { case 0: return wifiPassword; case 1: return wifiPassword2; case 2: return wifiPassword3; default: return nullptr; }
  }
  bool geminiConfigured() const {
    return geminiApiKey[0] && strcmp(geminiApiKey, "CHANGE_ME") != 0 && strcmp(geminiApiKey, "YOUR_GEMINI_API_KEY") != 0;
  }
  float speakerGain() const { return float(speakerGainMilli) / 1000.0f; }
  float vadStartMultiplier() const { return float(vadStartX100) / 100.0f; }
  float vadEndMultiplier() const { return float(vadEndX100) / 100.0f; }
  uint16_t vadEndFrames(uint32_t frameMs = 20) const {
    uint16_t f = uint16_t((uint32_t(vadEndMs) + frameMs - 1) / frameMs);
    return f ? f : 1;
  }
};

class RuntimeSettingsStore {
 public:
  uint8_t wifiSaveFailedSlot() const { return wifiSaveFailedSlot_; }
  uint8_t wifiLegacyMirrorFailedSlot() const { return wifiLegacyMirrorFailedSlot_; }
  bool wifiLegacyMirrorRestoreFailed() const { return wifiLegacyMirrorRestoreFailed_; }
  bool wifiCredentialsCommittedThisSave() const { return wifiCredentialsCommittedThisSave_; }
  size_t wifiFreeEntriesAfterSave() const { return wifiFreeEntriesAfterSave_; }

  bool load(RuntimeSettings& out, const RuntimeSettings& defaults) {
    out = defaults;
    Preferences p;
    if (!p.begin("robodesk", true)) return false;
    const bool initialized = p.getBool("init", false);
    const bool hasWifiRecord=readWifiRecord_(p,out);
    if (initialized) {
      if(!hasWifiRecord){
        readString(p, "ssid", out.wifiSsid, sizeof(out.wifiSsid));
        readString(p, "wpass", out.wifiPassword, sizeof(out.wifiPassword));
        readString(p, "ssid2", out.wifiSsid2, sizeof(out.wifiSsid2));
        readString(p, "wpass2", out.wifiPassword2, sizeof(out.wifiPassword2));
        readString(p, "ssid3", out.wifiSsid3, sizeof(out.wifiSsid3));
        readString(p, "wpass3", out.wifiPassword3, sizeof(out.wifiPassword3));
      }
      readString(p, "gkey", out.geminiApiKey, sizeof(out.geminiApiKey));
      readString(p, "model", out.geminiModel, sizeof(out.geminiModel));
      readString(p, "summarymodel", out.summaryModel, sizeof(out.summaryModel));
      out.autoMemory=p.getUChar("automem",out.autoMemory)?1:0;
      out.backgroundDailyLimit=uint8_t(clampU16(p.getUChar("bgmax",out.backgroundDailyLimit),0,12));
      readString(p, "voice", out.geminiVoice, sizeof(out.geminiVoice));
      readString(p, "pin", out.adminPin, sizeof(out.adminPin));
      readString(p, "style", out.speechStyle, sizeof(out.speechStyle));
      readString(p, "rname", out.robotName, sizeof(out.robotName));
      out.speakerGainMilli = clampU16(p.getUShort("gain", out.speakerGainMilli), 50, 1000);
      out.vadStartX100 = clampU16(p.getUShort("vstart", out.vadStartX100), 120, 1000);
      out.vadEndX100 = clampU16(p.getUShort("vend", out.vadEndX100), 105, 800);
      out.vadEndMs = clampU16(p.getUShort("vendms", out.vadEndMs), 120, 2500);
      out.postSpeakGuardMs = clampU16(p.getUShort("guard", out.postSpeakGuardMs), 0, 2000);
      out.wakeFollowupMs = clampU16(p.getUShort("wakewin", out.wakeFollowupMs), 3000, 60000);
      out.timezoneOffsetMin = clampI16(int16_t(p.getUShort("tzmin", uint16_t(out.timezoneOffsetMin + 720))) - 720, -720, 840);
      out.memoryEnabled = p.getUChar("memory", out.memoryEnabled) ? 1 : 0;
      out.proactiveVisual = p.getUChar("pvisual", out.proactiveVisual) ? 1 : 0;
      out.proactiveVoice = p.getUChar("pvoice", out.proactiveVoice) ? 1 : 0;
      out.interactionMetrics = p.getUChar("imetrics", out.interactionMetrics) ? 1 : 0;
      out.privacyMicMuted=p.getUChar("micmute",out.privacyMicMuted)?1:0;
      out.comfortAlertsEnabled=p.getUChar("comfort",out.comfortAlertsEnabled)?1:0;
      out.comfortMinTempX10=clampI16(int16_t(p.getShort("cminT",out.comfortMinTempX10)),-100,600);
      out.comfortMaxTempX10=clampI16(int16_t(p.getShort("cmaxT",out.comfortMaxTempX10)),-100,600);
      out.comfortMinHumidityX10=clampU16(p.getUShort("cminH",out.comfortMinHumidityX10),0,1000);
      out.comfortMaxHumidityX10=clampU16(p.getUShort("cmaxH",out.comfortMaxHumidityX10),0,1000);
      out.dailyBriefingEnabled=p.getUChar("briefing",out.dailyBriefingEnabled)?1:0;
      out.dailyBriefingMinute=clampU16(p.getUShort("briefmin",out.dailyBriefingMinute),0,1439);
      out.pomodoroFocusMinutes=uint8_t(clampU16(p.getUChar("pomfocus",out.pomodoroFocusMinutes),1,90));
      out.pomodoroBreakMinutes=uint8_t(clampU16(p.getUChar("pombreak",out.pomodoroBreakMinutes),1,60));
      out.pomodoroLongBreakMinutes=uint8_t(clampU16(p.getUChar("pomlong",out.pomodoroLongBreakMinutes),1,90));
      out.pomodoroWasActive=p.getUChar("pomactive",out.pomodoroWasActive)?1:0;
      out.pomodoroInterrupted=p.getUChar("pomint",out.pomodoroInterrupted)?1:0;
      readString(p,"notifapps",out.notificationAllowlist,sizeof(out.notificationAllowlist));
      readString(p,"blepeer",out.phoneBlePeer,sizeof(out.phoneBlePeer));
      out.phonePlatform=uint8_t(clampU16(p.getUChar("phoneplat",out.phonePlatform),0,1));
      out.navigationEnabled=p.getUChar("navon",out.navigationEnabled)?1:0;
      out.characterIntensity=uint8_t(clampU16(p.getUChar("charlevel",out.characterIntensity),0,2));
      readString(p,"notifvoice",out.notificationSpeechAllowlist,sizeof(out.notificationSpeechAllowlist));
      out.faceLifeEnabled = p.getUChar("flife", out.faceLifeEnabled) ? 1 : 0;
      out.facePupils = p.getUChar("fpupil", out.facePupils) ? 1 : 0;
      out.faceBrows = p.getUChar("fbrow", out.faceBrows) ? 1 : 0;
      out.faceLashes = p.getUChar("flash", out.faceLashes) ? 1 : 0;
      out.faceAutoBrows = p.getUChar("fabrow", out.faceAutoBrows) ? 1 : 0;
      out.faceAutoLashes = p.getUChar("falash", out.faceAutoLashes) ? 1 : 0;
      { const uint8_t m=p.getUChar("mouth",out.mouthMode); out.mouthMode=m<=2?m:0; }
      out.facePupilLessActing = p.getUChar("fpact", out.facePupilLessActing) ? 1 : 0;
      out.faceMicroX100 = clampU16(p.getUShort("fmicro", out.faceMicroX100), 0, 100);
      out.faceSilhouetteX100 = clampU16(p.getUShort("fsil", out.faceSilhouetteX100), 0, 135);
      out.faceGazeReachX100 = clampU16(p.getUShort("fgaze", out.faceGazeReachX100), 40, 125);
      out.masterSound = p.getUChar("sndmaster", out.masterSound) ? 1 : 0;
      out.speechEnabled = p.getUChar("sndspeech", out.speechEnabled) ? 1 : 0;
      out.characterSfx = p.getUChar("sndsfx", out.characterSfx) ? 1 : 0;
      out.wakeSfx = p.getUChar("sndwake", out.wakeSfx) ? 1 : 0;
      out.touchSfx = p.getUChar("sndtouch", out.touchSfx) ? 1 : 0;
      out.motionSfx = p.getUChar("sndmotion", out.motionSfx) ? 1 : 0;
      out.notificationSfx = p.getUChar("sndnotif", out.notificationSfx) ? 1 : 0;
      { const uint8_t f=p.getUChar("sndfreq",out.sonicFrequency);out.sonicFrequency=f<=2?f:1; }
      out.quietHoursEnabled = p.getUChar("quieton", out.quietHoursEnabled) ? 1 : 0;
      out.sfxIntensityX100 = clampU16(p.getUShort("sfxint", out.sfxIntensityX100), 0, 100);
      out.quietStartMin = clampU16(p.getUShort("quietstart", out.quietStartMin), 0, 1439);
      out.quietEndMin = clampU16(p.getUShort("quietend", out.quietEndMin), 0, 1439);
      out.quietGainX100 = clampU16(p.getUShort("quietgain", out.quietGainX100), 0, 100);
      const uint8_t mode = p.getUChar("mode", out.inputMode);
      out.inputMode = mode <= uint8_t(RuntimeSettings::WakeWord) ? mode : uint8_t(RuntimeSettings::AlwaysListening);
    }
    p.end();
    return initialized||hasWifiRecord;
  }

  bool save(const RuntimeSettings& s) {
    wifiSaveFailedSlot_=0;
    wifiLegacyMirrorFailedSlot_=0;
    wifiLegacyMirrorRestoreFailed_=false;
    wifiCredentialsCommittedThisSave_=false;
    wifiFreeEntriesAfterSave_=0;
    Preferences p;
    if (!p.begin("robodesk", false)) return false;
    bool ok = true;
    WifiCredentialSet previous{};
    const bool previousRecord=readWifiRecord_(p,previous);
    if(!previousRecord)readLegacyWifi_(p,previous);
    WifiCredentialSet next{};
    makeWifiRecord_(s,next);
    const bool wifiChanged=!sameWifi_(previous,next);
    const bool writeRecord=!previousRecord||wifiChanged;
    if(writeRecord&&!writeWifiRecord_(p,next)){
      wifiSaveFailedSlot_=changedWifiSlot_(previous,next);
      wifiFreeEntriesAfterSave_=p.freeEntries();
      p.end();
      return false;
    }
    wifiCredentialsCommittedThisSave_=previousRecord||writeRecord;
    const bool recordReady=wifiCredentialsCommittedThisSave_;
    if(recordReady&&!legacyWifiMatches_(p,next)&&!mirrorLegacyWifi_(p,next)){
      wifiLegacyMirrorFailedSlot_=changedWifiSlot_(previous,next);
      // Restore the complete legacy set for older firmware/OTA rollback. The
      // atomic record remains authoritative for this firmware either way.
      if(!mirrorLegacyWifi_(p,previous))wifiLegacyMirrorRestoreFailed_=true;
    }
    ok &= p.putString("gkey", s.geminiApiKey) > 0 || s.geminiApiKey[0] == 0;
    ok &= p.putString("model", s.geminiModel) > 0;
    ok &= p.putString("summarymodel",s.summaryModel)>0;
    ok &= p.putUChar("automem",s.autoMemory)>0;
    ok &= p.putUChar("bgmax",s.backgroundDailyLimit)>0;
    ok &= p.putString("voice", s.geminiVoice) > 0;
    const size_t pinWritten=p.putString("pin", s.adminPin);
#if ROBODESK_DUAL_ROBOT
    // This role retires its local login. Preferences returns the string length,
    // so a successfully persisted empty PIN is zero bytes; verify the readback.
    ok &= pinWritten>0 || (s.adminPin[0]==0 && p.isKey("pin") && p.getString("pin").isEmpty());
#else
    ok &= pinWritten>0;
#endif
    p.putString("style", s.speechStyle);
    p.putString("rname", s.robotName);
    p.putUShort("gain", s.speakerGainMilli);
    p.putUShort("vstart", s.vadStartX100);
    p.putUShort("vend", s.vadEndX100);
    p.putUShort("vendms", s.vadEndMs);
    p.putUShort("guard", s.postSpeakGuardMs);
    p.putUShort("wakewin", s.wakeFollowupMs);
    p.putUShort("tzmin", uint16_t(s.timezoneOffsetMin + 720));
    p.putUChar("memory", s.memoryEnabled);
    p.putUChar("pvisual", s.proactiveVisual);
    p.putUChar("pvoice", s.proactiveVoice);
    ok &= p.putUChar("imetrics", s.interactionMetrics) > 0;
    ok &= p.putUChar("micmute",s.privacyMicMuted)>0;
    ok &= p.putUChar("comfort",s.comfortAlertsEnabled)>0;
    ok &= p.putShort("cminT",s.comfortMinTempX10)>0;
    ok &= p.putShort("cmaxT",s.comfortMaxTempX10)>0;
    ok &= p.putUShort("cminH",s.comfortMinHumidityX10)>0;
    ok &= p.putUShort("cmaxH",s.comfortMaxHumidityX10)>0;
    ok &= p.putUChar("briefing",s.dailyBriefingEnabled)>0;
    ok &= p.putUShort("briefmin",s.dailyBriefingMinute)>0;
    ok &= p.putUChar("pomfocus",s.pomodoroFocusMinutes)>0;
    ok &= p.putUChar("pombreak",s.pomodoroBreakMinutes)>0;
    ok &= p.putUChar("pomlong",s.pomodoroLongBreakMinutes)>0;
    ok &= p.putUChar("pomactive",s.pomodoroWasActive)>0;
    ok &= p.putUChar("pomint",s.pomodoroInterrupted)>0;
    ok &= p.putString("notifapps",s.notificationAllowlist)>0||s.notificationAllowlist[0]==0;
    ok &= p.putString("blepeer",s.phoneBlePeer)>0||s.phoneBlePeer[0]==0;
    ok &= p.putUChar("phoneplat",s.phonePlatform)==1;
    ok &= p.putUChar("navon",s.navigationEnabled)==1;
    ok &= p.putUChar("charlevel",s.characterIntensity)==1;
    ok &= p.putString("notifvoice",s.notificationSpeechAllowlist)>0||s.notificationSpeechAllowlist[0]==0;
    p.putUChar("flife", s.faceLifeEnabled);
    p.putUChar("fpupil", s.facePupils);
    p.putUChar("fbrow", s.faceBrows);
    p.putUChar("flash", s.faceLashes);
    p.putUChar("fabrow", s.faceAutoBrows);
    p.putUChar("falash", s.faceAutoLashes);
    p.putUChar("mouth", s.mouthMode);
    p.putUChar("fpact", s.facePupilLessActing);
    p.putUShort("fmicro", s.faceMicroX100);
    p.putUShort("fsil", s.faceSilhouetteX100);
    p.putUShort("fgaze", s.faceGazeReachX100);
    p.putUChar("sndmaster", s.masterSound);
    p.putUChar("sndspeech", s.speechEnabled);
    p.putUChar("sndsfx", s.characterSfx);
    p.putUChar("sndwake", s.wakeSfx);
    p.putUChar("sndtouch", s.touchSfx);
    p.putUChar("sndmotion", s.motionSfx);
    p.putUChar("sndnotif", s.notificationSfx);
    p.putUChar("sndfreq", s.sonicFrequency);
    p.putUChar("quieton", s.quietHoursEnabled);
    p.putUShort("sfxint", s.sfxIntensityX100);
    p.putUShort("quietstart", s.quietStartMin);
    p.putUShort("quietend", s.quietEndMin);
    p.putUShort("quietgain", s.quietGainX100);
    p.putUChar("mode", s.inputMode);
    if(ok)ok &= p.putBool("init",true)>0;
    wifiFreeEntriesAfterSave_=p.freeEntries();
    p.end();
    return ok;
  }

  bool clear() {
    Preferences p;
    if (!p.begin("robodesk", false)) return false;
    const bool ok = p.clear();
    p.end();
    return ok;
  }

 private:
  struct WifiCredentialSet {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char ssid[3][33];
    char password[3][65];
    uint32_t checksum;
  };

  static constexpr uint32_t WifiRecordMagic=0x57494649u;
  static constexpr uint16_t WifiRecordVersion=1;
  static uint32_t wifiChecksum_(const WifiCredentialSet&record){
    const uint8_t*bytes=reinterpret_cast<const uint8_t*>(&record);uint32_t hash=2166136261u;
    for(size_t i=0;i<offsetof(WifiCredentialSet,checksum);++i){hash^=bytes[i];hash*=16777619u;}
    return hash;
  }
  static bool validWifiRecord_(const WifiCredentialSet&record){
    if(record.magic!=WifiRecordMagic||record.version!=WifiRecordVersion||record.checksum!=wifiChecksum_(record))return false;
    for(uint8_t i=0;i<3;++i)if(!memchr(record.ssid[i],0,sizeof(record.ssid[i]))||!memchr(record.password[i],0,sizeof(record.password[i])))return false;
    return true;
  }
  static void makeWifiRecord_(const RuntimeSettings&s,WifiCredentialSet&record){
    memset(&record,0,sizeof(record));record.magic=WifiRecordMagic;record.version=WifiRecordVersion;
    RuntimeSettings::copy(record.ssid[0],sizeof(record.ssid[0]),s.wifiSsid);RuntimeSettings::copy(record.password[0],sizeof(record.password[0]),s.wifiPassword);
    RuntimeSettings::copy(record.ssid[1],sizeof(record.ssid[1]),s.wifiSsid2);RuntimeSettings::copy(record.password[1],sizeof(record.password[1]),s.wifiPassword2);
    RuntimeSettings::copy(record.ssid[2],sizeof(record.ssid[2]),s.wifiSsid3);RuntimeSettings::copy(record.password[2],sizeof(record.password[2]),s.wifiPassword3);
    record.checksum=wifiChecksum_(record);
  }
  static void readLegacyWifi_(Preferences&p,WifiCredentialSet&record){
    memset(&record,0,sizeof(record));record.magic=WifiRecordMagic;record.version=WifiRecordVersion;
    const char*ssidKeys[]={"ssid","ssid2","ssid3"};const char*passwordKeys[]={"wpass","wpass2","wpass3"};
    for(uint8_t i=0;i<3;++i){String ssid=p.getString(ssidKeys[i]);String password=p.getString(passwordKeys[i]);RuntimeSettings::copy(record.ssid[i],sizeof(record.ssid[i]),ssid.c_str());RuntimeSettings::copy(record.password[i],sizeof(record.password[i]),password.c_str());}
    record.checksum=wifiChecksum_(record);
  }
  static bool readWifiRecord_(Preferences&p,RuntimeSettings&out){
    WifiCredentialSet record{};if(p.getBytesLength("wifiset")!=sizeof(record)||p.getBytes("wifiset",&record,sizeof(record))!=sizeof(record)||!validWifiRecord_(record))return false;
    RuntimeSettings::copy(out.wifiSsid,sizeof(out.wifiSsid),record.ssid[0]);RuntimeSettings::copy(out.wifiPassword,sizeof(out.wifiPassword),record.password[0]);
    RuntimeSettings::copy(out.wifiSsid2,sizeof(out.wifiSsid2),record.ssid[1]);RuntimeSettings::copy(out.wifiPassword2,sizeof(out.wifiPassword2),record.password[1]);
    RuntimeSettings::copy(out.wifiSsid3,sizeof(out.wifiSsid3),record.ssid[2]);RuntimeSettings::copy(out.wifiPassword3,sizeof(out.wifiPassword3),record.password[2]);return true;
  }
  static bool readWifiRecord_(Preferences&p,WifiCredentialSet&record){
    return p.getBytesLength("wifiset")==sizeof(record)&&p.getBytes("wifiset",&record,sizeof(record))==sizeof(record)&&validWifiRecord_(record);
  }
  static bool writeWifiRecord_(Preferences&p,WifiCredentialSet&record){
    record.checksum=wifiChecksum_(record);if(p.putBytes("wifiset",&record,sizeof(record))!=sizeof(record))return false;
    WifiCredentialSet verify{};return readWifiRecord_(p,verify)&&memcmp(&record,&verify,sizeof(record))==0;
  }
  static bool sameWifi_(const WifiCredentialSet&a,const WifiCredentialSet&b){
    for(uint8_t i=0;i<3;++i)if(strcmp(a.ssid[i],b.ssid[i])||strcmp(a.password[i],b.password[i]))return false;
    return true;
  }
  static bool legacyWifiMatches_(Preferences&p,const WifiCredentialSet&record){WifiCredentialSet legacy{};readLegacyWifi_(p,legacy);return sameWifi_(legacy,record);}
  static bool writeWifiString_(Preferences&p,const char*key,const char*value){
    const size_t length=strlen(value);const size_t written=p.putString(key,value);
    if(length&&written!=length)return false;
    if(!length&&!p.isKey(key))return false;
    const String verify=p.getString(key);return strcmp(verify.c_str(),value)==0;
  }
  static bool mirrorLegacyWifi_(Preferences&p,const WifiCredentialSet&record){
    const char*ssidKeys[]={"ssid","ssid2","ssid3"};const char*passwordKeys[]={"wpass","wpass2","wpass3"};bool ok=true;
    for(uint8_t i=0;i<3;++i){const bool ssidOk=writeWifiString_(p,ssidKeys[i],record.ssid[i]);const bool passwordOk=writeWifiString_(p,passwordKeys[i],record.password[i]);ok&=ssidOk&&passwordOk;}
    return ok;
  }
  static uint8_t changedWifiSlot_(const WifiCredentialSet&oldRecord,const WifiCredentialSet&newRecord){
    for(uint8_t i=0;i<3;++i)if(strcmp(oldRecord.ssid[i],newRecord.ssid[i])||strcmp(oldRecord.password[i],newRecord.password[i]))return uint8_t(i+1);
    return 1;
  }

  uint8_t wifiSaveFailedSlot_=0;
  uint8_t wifiLegacyMirrorFailedSlot_=0;
  bool wifiLegacyMirrorRestoreFailed_=false;
  bool wifiCredentialsCommittedThisSave_=false;
  size_t wifiFreeEntriesAfterSave_=0;

  static int16_t clampI16(int16_t v, int16_t lo, int16_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
  static uint16_t clampU16(uint16_t v, uint16_t lo, uint16_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }

  static void readString(Preferences& p, const char* key, char* dst, size_t cap) {
    String v = p.getString(key, dst);
    RuntimeSettings::copy(dst, cap, v.c_str());
  }
};
