#include "RoboBehaviorWire.h"
#include <cassert>
#include <cstdio>
int main(){RuntimeSettings source;RuntimeSettings::copy(source.geminiApiKey,sizeof(source.geminiApiKey),"private-key");RuntimeSettings::copy(source.wifiPassword,sizeof(source.wifiPassword),"private-wifi");source.privacyMicMuted=1;source.speakerGainMilli=720;source.pomodoroFocusMinutes=40;
  JsonDocument doc;roboBehaviorExport(source,doc.to<JsonObject>());assert(doc["schema"]==1);assert(!doc["geminiApiKey"].is<const char*>()&&!doc["wifiPassword"].is<const char*>()&&!doc["adminPin"].is<const char*>()&&!doc["phoneBlePeer"].is<const char*>());
  RuntimeSettings gatewayOnly=source;RuntimeSettings::copy(gatewayOnly.wifiSsid2,sizeof(gatewayOnly.wifiSsid2),"fallback-two");RuntimeSettings::copy(gatewayOnly.wifiPassword3,sizeof(gatewayOnly.wifiPassword3),"fallback-secret");RuntimeSettings::copy(gatewayOnly.geminiApiKey,sizeof(gatewayOnly.geminiApiKey),"gateway-key");RuntimeSettings::copy(gatewayOnly.adminPin,sizeof(gatewayOnly.adminPin),"private-pin");assert(roboBehaviorSettingsEqual(source,gatewayOnly));
  gatewayOnly.speakerGainMilli++;assert(!roboBehaviorSettingsEqual(source,gatewayOnly));
  RuntimeSettings receiver;RuntimeSettings::copy(receiver.geminiApiKey,sizeof(receiver.geminiApiKey),"gateway-only");assert(roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));assert(receiver.privacyMicMuted==1&&receiver.speakerGainMilli==720&&receiver.pomodoroFocusMinutes==40);assert(!strcmp(receiver.geminiApiKey,"gateway-only"));
  doc["phonePlatform"]=1;doc["navigationEnabled"]=0;doc["characterIntensity"]=2;doc["notificationSpeechAllowlist"]="com.example.allowed";
  assert(roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));assert(receiver.phonePlatform==1&&receiver.navigationEnabled==0&&receiver.characterIntensity==2&&!strcmp(receiver.notificationSpeechAllowlist,"com.example.allowed"));
  doc["phonePlatform"]=2;assert(!roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));assert(receiver.phonePlatform==1);doc["phonePlatform"]=1;
  doc["characterIntensity"]=3;assert(!roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));doc["characterIntensity"]=2;
  doc["navigationEnabled"]=2;assert(!roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));doc["navigationEnabled"]=0;
  doc["notificationSpeechAllowlist"]=std::string(192,'a');assert(!roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));
  doc.remove("phonePlatform");doc.remove("navigationEnabled");doc.remove("characterIntensity");doc.remove("notificationSpeechAllowlist");
  assert(roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));assert(receiver.phonePlatform==1&&receiver.characterIntensity==2); // Old schema-1 peers retain current optional settings.
  doc["schema"]=2;assert(!roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));doc["schema"]=1;doc["speakerGainMilli"]=5000;assert(!roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));assert(receiver.speakerGainMilli==720);
  doc["speakerGainMilli"]=720;doc["robotName"]=std::string(100,'a');assert(!roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));doc["robotName"]="RoboDesk";doc.remove("privacyMicMuted");assert(!roboBehaviorImport(doc.as<JsonObjectConst>(),receiver));
  std::puts("PASS: behavior wire round-trip, secret isolation, schema/bounds rejection, atomic import");}
