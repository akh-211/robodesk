#pragma once
#include "RoboBehaviorWire.h"
#include "RoboSignedImage.h"
#include "RoboExternalFetchS3.h"

inline RoboSignedImage roboRobotImage;
inline uint32_t roboRobotRebootAt=0;
inline bool roboRobotUpdating=false;
#if ROBODESK_PHONE_BLE_ROBOT
inline bool roboRobotBleAttempted=false;
// PhoneBleTransport dispatches authenticated envelopes from its main-loop
// service, so these typed actions use the same owner thread as dashboard actions.
inline bool roboRobotDispatchPhoneCommand(uint32_t session,uint32_t counter,uint32_t ageMs,const uint8_t*payload,size_t size){
  PhoneCompanionCommand::Command command{};
  if(!session||!counter||ageMs>=30000||firmwareUpdateActive||!phoneBle.sessionActive(session)||!PhoneCompanionCommand::decode(payload,size,command))return false;
  bool ok=false;
  if(command.operation==PhoneCompanionCommand::Operation::SetIntensity||command.operation==PhoneCompanionCommand::Operation::SetQuietHours){
    RuntimeSettings next=runtimeSettings;
    if(command.operation==PhoneCompanionCommand::Operation::SetIntensity)next.characterIntensity=command.intensity;
    else{next.quietHoursEnabled=command.quietEnabled;if(command.hasQuietTimes){next.quietStartMin=command.quietStartMin;next.quietEndMin=command.quietEndMin;}}
    ok=settingsStore.save(next);if(ok){runtimeSettings=next;applySonicSettings();}
  }else{
    const char*action=nullptr;const char*value="";
    switch(command.operation){
      case PhoneCompanionCommand::Operation::StartActivity:{const auto*d=companion::activityDefinition(command.activityId);if(d){action="activity_start";value=d->name;}break;}
      case PhoneCompanionCommand::Operation::PauseActivity:action="activity_pause";break;
      case PhoneCompanionCommand::Operation::ResumeActivity:action="activity_resume";break;
      case PhoneCompanionCommand::Operation::CancelActivity:action="activity_cancel";break;
      default:break;
    }
    char result[256]{};if(action)ok=dashboardCompanionAction(nullptr,action,value,result,sizeof(result));
  }
  return phoneBle.publishCompanionResult(session,counter,ok?0:1,ok?0:2);
}
inline void roboRobotBeginBle(){
  // A legacy/C3-owner gateway never sets bit 3. Wait for a fresh declaration
  // from the connected gateway before enabling the S3 radio.
  if(roboRobotBleAttempted||!RoboDual.link.connected()||!RoboDual.clockFresh()||!(RoboDual.phoneState.load()&8))return;
  roboRobotBleAttempted=true;
  const auto memory=roboMemorySnapshot();
  if(memory.internalFree<53248||memory.internalLargestBlock<24576||memory.psramFree<65536){phoneBleMemory("s3-reserve-low");return;}
  phoneBle.setRemoteRevokePersist(phoneBlePersistRemoteRevoke);
  phoneBle.setCompanionCommandDispatch(roboRobotDispatchPhoneCommand);
  phoneBleReady=phoneBle.begin(&phoneNotifications,&runtimeSettings,&phoneNavigation);
  esp_rom_printf("[RoboDeskBLE] owner=s3 ready=%u psram=%u\n",unsigned(phoneBleReady),unsigned(ESP.getFreePsram()));
}
#endif
inline void roboRobotResume(){roboRobotUpdating=false;dashboardFirmwareUpdateControl(nullptr,false);if(runtimeSettings.inputMode==RuntimeSettings::WakeWord&&!privacyMicMuted)restoreWakeWordAfterPrivacy(millis());}
inline void roboRobotRoleBegin(){
  if(!RoboDual.begin())fatalStartup("BOARD_LINK");
  RoboLog.printf("LEV,PAIR,BATTERY,MONITOR=%u,pin=%d,divider=100k:100k\n",unsigned(roboBattery.begin(ROBODESK_PIN_BATTERY)),ROBODESK_PIN_BATTERY);
  // Preserve the old settings before retiring radio credentials from this role.
  Preferences migration;if(migration.begin("robodesk_pair",false)){
    if(migration.getBool("reset",false)){if(migration.isKey("backup")&&!migration.remove("backup"))fatalStartup("PAIR_RESET");}
    else if(!migration.getBool("done",false)){if(!migration.isKey("backup")&&migration.putBytes("backup",&runtimeSettings,sizeof(runtimeSettings))!=sizeof(runtimeSettings))fatalStartup("PAIR_BACKUP");if(!migration.getUInt("id",0)){uint32_t id=esp_random();if(!id)id=1;if(migration.putUInt("id",id)!=4)fatalStartup("PAIR_MIGRATION_ID");}}migration.end();
  }
  runtimeSettings.wifiSsid[0]=runtimeSettings.wifiSsid2[0]=runtimeSettings.wifiSsid3[0]=0;
  runtimeSettings.wifiPassword[0]=runtimeSettings.wifiPassword2[0]=runtimeSettings.wifiPassword3[0]=0;
  runtimeSettings.adminPin[0]=0;
#if !ROBODESK_PHONE_BLE_ROBOT
  runtimeSettings.phoneBlePeer[0]=0;
#endif
  RuntimeSettings::copy(runtimeSettings.geminiApiKey,sizeof(runtimeSettings.geminiApiKey),"__ROBODESK_GATEWAY__");
  // Normal settings RPC excludes secrets. UART migration is a physical-service
  // operation requiring a short window opened from the S3 USB serial console.
  if(!settingsStore.save(runtimeSettings))fatalStartup("PAIR_SETTINGS");
  RoboLog.printf("LEV,PAIR,IR,ready=%u,rx=1,tx=2\n",unsigned(roboIr.begin()));
  auto&s=settingsDashboard.server();
  s.on("/_migration/get",HTTP_GET,[](){auto&s=settingsDashboard.server();Preferences p;RuntimeSettings old;
    if(!p.begin("robodesk_pair",true)){s.send(500,"text/plain","Migration store unavailable");return;}
    const size_t backupSize=p.getBytesLength("backup");
    if(p.getBool("done",false)||(backupSize!=sizeof(old)&&backupSize!=offsetof(RuntimeSettings,phonePlatform))){p.end();s.send(204,"text/plain","");return;}
    if(p.getBool("reset",false)){p.end();s.send(204,"text/plain","");return;}
    if(!roboMigrationUntil||int32_t(roboMigrationUntil-millis())<=0){p.end();s.send(403,"text/plain","Open migration from S3 USB: pair_migrate");return;}
    const uint32_t id=p.getUInt("id",0);if(p.getBytes("backup",&old,backupSize)!=backupSize){p.end();s.send(500,"text/plain","Migration backup unreadable");return;}p.end();JsonDocument doc;doc["id"]=id;
    doc["wifiSsid"]=old.wifiSsid;doc["wifiPassword"]=old.wifiPassword;doc["wifiSsid2"]=old.wifiSsid2;doc["wifiPassword2"]=old.wifiPassword2;doc["wifiSsid3"]=old.wifiSsid3;doc["wifiPassword3"]=old.wifiPassword3;doc["geminiApiKey"]=old.geminiApiKey;doc["adminPin"]=old.adminPin;
    String body;serializeJson(doc,body);s.send(200,"application/json",body);
  });
  s.on("/_migration/commit",HTTP_POST,[](){auto&s=settingsDashboard.server();Preferences p;if(!p.begin("robodesk_pair",false)){s.send(500,"text/plain","Migration store unavailable");return;}
    if(!roboMigrationUntil||int32_t(roboMigrationUntil-millis())<=0||p.getBool("reset",false)){p.end();s.send(403,"text/plain","Migration window closed");return;}
    const uint32_t id=uint32_t(strtoul(s.arg("id").c_str(),nullptr,10));if(!id||id!=p.getUInt("id",0)){p.end();s.send(409,"text/plain","Migration ID mismatch");return;}
    bool ok=p.putBytes("behavior",&runtimeSettings,sizeof(runtimeSettings))==sizeof(runtimeSettings);
    if(ok&&p.isKey("backup"))ok=p.remove("backup");if(ok)ok=p.putBool("done",true)==1;p.end();if(ok)roboMigrationUntil=0;s.send(ok?200:500,"text/plain",ok?"Gateway acknowledged; robot secret backup erased":"Migration commit failed");
  });
  s.on("/_settings/get",HTTP_GET,[](){JsonDocument doc;roboBehaviorExport(runtimeSettings,doc.to<JsonObject>());String out;serializeJson(doc,out);settingsDashboard.server().send(200,"application/json",out);});
  s.on("/_settings/apply",HTTP_POST,[](){auto&s=settingsDashboard.server();JsonDocument doc;RuntimeSettings next=runtimeSettings;
    if(deserializeJson(doc,s.arg("settings"))||!roboBehaviorImport(doc.as<JsonObjectConst>(),next)){s.send(400,"text/plain","Invalid behavior settings");return;}
    next.pomodoroWasActive=runtimeSettings.pomodoroWasActive;next.pomodoroInterrupted=runtimeSettings.pomodoroInterrupted;
    if(s.hasArg("owner")&&s.arg("owner").length()&&strcmp(s.arg("owner").c_str(),brain.ownerName())&&!brain.manualRemember("owner.name",s.arg("owner").c_str(),"profile",millis())){s.send(500,"text/plain","Owner name was not saved");return;}
    if(next.phonePlatform!=runtimeSettings.phonePlatform&&!dashboardPhonePlatformChanged(nullptr,runtimeSettings.phonePlatform,next.phonePlatform)){s.send(500,"text/plain","Phone credential revocation failed");return;}
    // Revocation may have cleared the owner peer in runtimeSettings.
    RuntimeSettings::copy(next.phoneBlePeer,sizeof(next.phoneBlePeer),runtimeSettings.phoneBlePeer);
    if(!settingsStore.save(next)){s.send(500,"text/plain","Robot settings were not saved");return;}runtimeSettings=next;privacyMicMuted=next.privacyMicMuted!=0;
    if(privacyMicMuted){if(wakeWord.running())wakeWord.disarmToManual(MicI2S);micTaskPaused=true;if(micQueue)xQueueReset(micQueue);resetGeminiOffline(millis());}
    s.send(200,"text/plain","Robot settings saved");roboRobotRebootAt=millis()+1000;
  });
  s.on("/_companion/settings",HTTP_POST,[](){auto&s=settingsDashboard.server();
    auto readBounded=[&s](const char*name,long minimum,long maximum,long&value){if(!s.hasArg(name))return false;const String raw=s.arg(name);if(!raw.length())return false;char*end=nullptr;const long parsed=strtol(raw.c_str(),&end,10);if(end==raw.c_str()||*end||parsed<minimum||parsed>maximum)return false;value=parsed;return true;};
    RuntimeSettings next=runtimeSettings;long value=0;
    if(s.hasArg("intensity")){
      if(s.args()!=1||!readBounded("intensity",0,2,value)){s.send(400,"text/plain","Invalid character intensity");return;}next.characterIntensity=uint8_t(value);
    }else{
      long enabled=0,start=0,end=0;
      if(!readBounded("enabled",0,1,enabled)||(s.args()!=1&&s.args()!=3)||(s.args()==3&&(!readBounded("startMin",0,1439,start)||!readBounded("endMin",0,1439,end)))){s.send(400,"text/plain","Invalid quiet-hours settings");return;}
      next.quietHoursEnabled=uint8_t(enabled);if(s.args()==3){next.quietStartMin=uint16_t(start);next.quietEndMin=uint16_t(end);}
    }
    if(!settingsStore.save(next)){s.send(500,"text/plain","Companion settings could not be saved");return;}
    runtimeSettings=next;applySonicSettings();s.send(200,"text/plain","Companion settings saved and applied");
  });
  s.on("/_phone/capabilities",HTTP_GET,[](){settingsDashboard.server().send(200,"application/json",ROBODESK_PHONE_BLE_ROBOT?"{\"navigationV1\":true,\"bleOwner\":\"s3\"}":"{\"navigationV1\":true,\"bleOwner\":\"c3\"}");});
#if ROBODESK_PHONE_BLE_ROBOT
  s.on("/_phone/forget",HTTP_POST,[](){const bool ok=dashboardPhonePlatformChanged(nullptr,0,0);settingsDashboard.server().send(ok?200:500,"text/plain",ok?"Phone credentials revoked":"Phone revocation failed");});
#endif
  s.on("/_phone/navigation",HTTP_POST,[](){auto&s=settingsDashboard.server();bool ok=runtimeSettings.navigationEnabled&&runtimeSettings.phonePlatform==0&&phoneNavigation.accept(s.arg("wire").c_str(),millis(),uint32_t(s.arg("ageMs").toInt()));s.send(ok?200:409,"text/plain",ok?"Accepted":"Navigation rejected");});
  s.on("/_phone/push",HTTP_POST,[](){auto&s=settingsDashboard.server();if(s.arg("op")=="remove"){phoneNotifications.remove(s.arg("key").c_str(),s.arg("appId").c_str());s.send(200,"text/plain","Removed");return;}bool ok=phoneNotifications.accept(s.arg("appId").c_str(),s.arg("app").c_str(),s.arg("title").c_str(),s.arg("snippet").c_str(),runtimeSettings.notificationAllowlist,millis(),s.arg("key").c_str(),uint32_t(s.arg("ageMs").toInt()));s.send(ok?200:409,"text/plain",ok?"Accepted":"Notification rejected");});
  s.on("/_external/fetch",HTTP_POST,[](){auto&s=settingsDashboard.server();const String u=s.arg("url");char host[robotunnel::HostMax+1];uint16_t port=0;
    if(!robotunnel::parseHttpsUrl(u.c_str(),host,sizeof(host),port)||u.length()>400||s.arg("bearer").length()>200){s.send(400,"text/plain","Invalid external request");return;}
    if(gemini.connecting()||gemini.connected()||memoryWorker.running()||!RoboDual.link.connected()||!RoboDual.internet||!roboextfetch::start(u,s.arg("bearer"))){s.send(503,"text/plain","Tunnel busy");return;}
    s.send(202,"text/plain","Started");});
  s.on("/_external/fetch/result",HTTP_GET,[](){auto&s=settingsDashboard.server();const uint8_t st=roboextfetch::state.load(std::memory_order_acquire);
    if(st==roboextfetch::Running){s.send(204,"text/plain","");return;}
    if(st!=roboextfetch::Done){s.send(410,"text/plain","No result");return;}
    String out;if(roboextfetch::body&&roboextfetch::bodySize)out.concat(roboextfetch::body,roboextfetch::bodySize);const uint16_t code=roboextfetch::resultCode;roboextfetch::state.store(roboextfetch::Idle);s.send(code,"text/plain",out);});
  s.on("/_external/context",HTTP_POST,[](){auto&s=settingsDashboard.server();String weather=s.arg("weather"),homeAssistant=s.arg("home_assistant"),calendar=s.arg("calendar");if(weather.length()>179||homeAssistant.length()>399||calendar.length()>239){s.send(413,"text/plain","External context exceeds category limit");return;}brain.setExternalContext(weather.c_str(),homeAssistant.c_str(),calendar.c_str());s.send(200,"text/plain","External context updated in RAM");});
  s.on("/_ota/status",HTTP_GET,[](){uint32_t version=0;bool read=RoboSignedImage::version(&version);char out[192];snprintf(out,sizeof(out),"{\"version\":%u,\"bootHealthy\":%s,\"protocol\":1,\"written\":%u}",unsigned(version),(!otaPendingVerify&&!otaSelfTestFailed)?"true":"false",unsigned(roboRobotImage.written()));settingsDashboard.server().send(read?200:500,"application/json",out);});
  s.on("/_ota/begin",HTTP_POST,[](){auto&s=settingsDashboard.server();JsonDocument doc;if(deserializeJson(doc,s.arg("manifest"))){s.send(400,"text/plain","Invalid update manifest");return;}
    if(wakeWord.running()&&!utteranceActive&&speakerDrained(millis())){if(!wakeWord.disarmToManual(MicI2S)){s.send(409,"text/plain","Wake engine busy");return;}resumeMicCaptureTask();}
    if(!dashboardFirmwareUpdateControl(nullptr,true)){s.send(409,"text/plain","Robot is busy");return;}
    if(!roboRobotImage.begin(doc["version"]|0u,doc["size"]|0u,doc["sha256"]|"",doc["signature"]|"")){roboRobotResume();s.send(400,"text/plain",roboRobotImage.error);return;}roboRobotUpdating=true;s.send(200,"text/plain","Inactive robot slot ready");
  });
  s.setBinaryHandler([](const uint8_t*p,size_t n){auto&s=settingsDashboard.server();if(n<5||p[0]!=1||!roboRobotUpdating){s.send(400,"text/plain","Invalid image chunk");return;}bool ok=roboRobotImage.write(robolink::get32(p+1),p+5,n-5);s.send(ok?200:400,"text/plain",ok?"Written":roboRobotImage.error);});
  s.on("/_ota/finish",HTTP_POST,[](){auto&s=settingsDashboard.server();bool ok=roboRobotUpdating&&roboRobotImage.finish();s.send(ok?200:400,"text/plain",ok?"Robot image verified":roboRobotImage.error);if(ok)roboRobotRebootAt=millis()+1200;else roboRobotResume();});
  s.on("/_ota/abort",HTTP_POST,[](){roboRobotImage.abort();if(roboRobotUpdating)roboRobotResume();settingsDashboard.server().send(200,"text/plain","Robot update aborted");});
  RoboLog.println("LEV,PAIR,ROBOT,wifi=disabled,uart=921600,tx=17,rx=18");
}
inline void roboRobotRoleService(){RoboDual.service();const uint32_t now=millis();roboBattery.service(now);if(!firmwareUpdateActive)roboIr.service(now);
#if !ROBODESK_PHONE_BLE_ROBOT
  phoneBleReady=(RoboDual.phoneState.load()&1)!=0;
#else
  if(phoneBleReady&&RoboDual.link.connected()&&RoboDual.clockFresh()&&!(RoboDual.phoneState.load()&8)){phoneBleReady=false;if(!phoneBle.end())ESP.restart();else roboRobotBleAttempted=false;}
  roboRobotBeginBle();
#endif
  uint32_t displayedId=0;if(RoboDual.takePairingPasskeyDisplayAck(displayedId)){uint8_t payload[4];robolink::put32(payload,displayedId);if(!RoboDual.link.send(robolink::PairingPasskeyDisplayed,payload,sizeof(payload),robolink::Reliable,10))RoboDual.retryPairingPasskeyDisplayAck(displayedId);}
  if(RoboDual.pairingUntil.load()&&int32_t(now-RoboDual.pairingUntil.load())>=0)RoboDual.clearPairingPasskey();
  if(roboRobotUpdating&&(!RoboDual.link.connected()||uint32_t(millis()-roboRobotImage.lastProgress)>60000u)){roboRobotImage.abort();roboRobotResume();}
  if(roboRobotRebootAt&&int32_t(millis()-roboRobotRebootAt)>=0&&RoboDual.link.idle())ESP.restart();
}
