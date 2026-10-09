#include "RoboBrain.h"
#include <cassert>
#include <iostream>
static RoboBrain brain;
static livingeyes::RelationshipMemory relationships;
static char result[2400];
static const char*call(const char*name,const char*args){assert(brain.executeTool(name,args,0,result,sizeof(result)));return result;}
int main(){
  brain.begin(nullptr,&relationships,100);brain.setClock(1800000000u,480,20833);
  assert(strstr(call("remember_fact",R"({"key":"drink","value":"kopi","source_quote":"Saya suka kopi","category":"preference"})"),"source_rejected"));
  brain.transcripts().append(false,"Saya suka kopi",100,1800000000);brain.transcripts().finish(110,false);
  assert(!brain.acceptFact(1,"Saya suka kopi","drink","kopi","preference",100));
  brain.transcripts().append(true,"Saya suka kopi",120,1800000000);
  assert(strstr(call("remember_fact",R"({"key":"drink","value":"kopi","source_quote":"Saya suka kopi","category":"preference"})"),"completed"));brain.transcripts().finish(130,false);
  assert(brain.companionState().addSummary("Kopi saat sarapan bersama keluarga",1,1800000000u,150));assert(brain.companionState().addSummary("Hujan turun saat perjalanan pulang",2,1800000001u,120));
  const char*topicRecall=call("recall_memory",R"({"query":""})");assert(strstr(topicRecall,"drink")&&strstr(topicRecall,"Kopi saat sarapan"));
  char prompt[4600];brain.buildSystemPrompt(prompt,sizeof(prompt),"RoboDesk","",20833);assert(strstr(prompt,"Kopi saat sarapan")&&strstr(prompt,"Hujan turun"));
  char turn[900],longSensorContext[500];memset(longSensorContext,'x',sizeof(longSensorContext)-1);longSensorContext[sizeof(longSensorContext)-1]=0;brain.setSensorContext(longSensorContext);
  companion::ActivityTracker activity;
  activity.sync(uint8_t(companion::CompanionActivityId::CuriousLook),100,true);auto activityContext=activity.promptContext(150);
  const size_t contextLength=brain.buildTurnContext(turn,sizeof(turn),false,false,false,0,0,0,0,0,activityContext);
  assert(contextLength<sizeof(turn)&&strlen(turn)==contextLength);
  assert(strstr(turn,"state=running current_activity=curious_look current_age_ms=50"));
  assert(strstr(turn,"Only describe an activity as currently running when state=running"));
  assert(strstr(turn,"stale history is not a recent event."));
  activity.sync(0,200,false,companion::ActivityBlock::Busy);activityContext=activity.promptContext(220);
  brain.buildTurnContext(turn,sizeof(turn),false,false,false,0,0,0,0,0,activityContext);
  assert(strstr(turn,"state=interrupted current_activity=none"));
  assert(strstr(turn,"last_outcome=interrupted last_activity=curious_look last_outcome_age_ms=20"));
  activityContext=activity.promptContext(60201);
  brain.buildTurnContext(turn,sizeof(turn),false,false,false,0,0,0,0,0,activityContext);
  assert(strstr(turn,"last_outcome=stale last_activity=none"));
  assert(!strstr(turn,"last_outcome=interrupted"));
  companion::ActivityTracker rebooted;activityContext=rebooted.promptContext(90000);
  brain.buildTurnContext(turn,sizeof(turn),false,false,false,0,0,0,0,0,activityContext);
  assert(strstr(turn,"state=idle current_activity=none"));
  assert(strstr(turn,"last_outcome=none last_activity=none"));
  assert(brain.manualRemember("owner.name","Ayu","profile",140));
  brain.transcripts().append(true,"Saya bernama Budi",150,1800000000);brain.transcripts().finish(160,false);
  assert(!brain.acceptFact(3,"Saya bernama Budi","owner.name","Budi","profile",100));assert(!strcmp(brain.ownerName(),"Ayu"));
  brain.transcripts().append(true,"Koreksi, sebenarnya nama saya Budi",170,1800000000);brain.transcripts().finish(180,false);
  assert(brain.acceptFact(4,"Koreksi, sebenarnya nama saya Budi","owner.name","Budi","profile",100));assert(!strcmp(brain.ownerName(),"Budi"));assert(brain.save(190));
  assert(strstr(call("create_reminder",R"({"text":"Minum air","kind":"timer","seconds":60})"),"completed"));assert(brain.companionState().reminders[0].id);
  brain.setClock(0,0,0);assert(strstr(call("create_reminder",R"({"text":"Pagi","kind":"daily","minute":480})"),"time_invalid"));
  assert(strstr(call("create_reminder",R"({"text":"Salah","kind":"timer","seconds":-1})"),"time_invalid_or_full"));
  mockWriteBudget=0;assert(strstr(call("create_reminder",R"({"text":"Gagal","kind":"timer","seconds":30})"),"storage_failed"));assert(!brain.companionState().reminders[1].id);mockWriteBudget=-1;
  assert(brain.save(195));uint32_t dailyRoutine=brain.companionState().createReminder("Siapkan sarapan",companion::Schedule::Daily,0,480);assert(dailyRoutine);brain.setClock(1800000000u,465,20833);char routineContext[128];assert(brain.upcomingRoutine(routineContext,sizeof(routineContext))&&strstr(routineContext,"Siapkan sarapan"));assert(!brain.inviteDue(1800001,false,false,false));assert(brain.inviteDue(1800001,true,false,false));brain.setClock(1800000000u,450,20833);assert(!brain.upcomingRoutine(routineContext,sizeof(routineContext)));assert(brain.companionState().cancelReminder(dailyRoutine));
  brain.setInteractionMetrics(true);brain.setClock(1800000000u,480,20833);assert(strstr(call("create_reminder",R"({"text":"Minum air","kind":"timer","seconds":60})"),"completed"));
  for(unsigned i=0;i<12;++i){char label[24];snprintf(label,sizeof(label),"hold.%u",i);assert(brain.actions().push(companion::ActionKind::Expression,companion::Priority::User,label,1,61000,60000));}
  brain.serviceReminders(61001);assert(brain.companionState().reminders[0].id&&brain.companionState().reminders[1].id);assert(brain.remindersDeferred()==2);
  companion::Action action;unsigned drained=0;while(brain.actions().pop(61002,false,action))++drained;assert(drained==12);
  mockWriteBudget=0;brain.serviceReminders(61003);assert(brain.companionState().reminders[0].id&&brain.companionState().reminders[1].id);assert(brain.reminderPersistFailures()==2);assert(!brain.actions().pop(61004,false,action));mockWriteBudget=-1;
  brain.serviceReminders(121004);assert(!brain.companionState().reminders[0].id&&!brain.companionState().reminders[1].id);assert(brain.actions().pop(121005,false,action));assert(action.kind==companion::ActionKind::Alert&&!strcmp(action.label,"Minum air"));uint32_t firstSource=action.sourceId;assert(brain.actions().pop(121006,false,action));assert(action.kind==companion::ActionKind::Alert&&!strcmp(action.label,"Minum air")&&action.sourceId!=firstSource);assert(!brain.actions().pop(121007,false,action));
  assert(brain.commitInvite(63000));assert(brain.companionState().proactiveCount==1);assert(brain.rollbackInvite(63000));assert(brain.companionState().proactiveCount==0);assert(!brain.inviteDue(63001,true,false,false));
  brain.setInteractionMetrics(true);assert(brain.commitInvite(63002));brain.confirmInvite(63002);assert(brain.invitesSent()==1);brain.onConversationStart(63003,20833);assert(brain.inviteFollowups()==1);
  assert(brain.commitInvite(63004));brain.confirmInvite(63004);brain.rejectedInvite(63005);assert(brain.invitesDeclined()==1);
  assert(brain.commitInvite(63006));brain.confirmInvite(63006);brain.update(123007,false,false,false,20833,false);assert(brain.invitesExpired()==1);
  mockWriteBudget=0;assert(!brain.reserveBackground(64000,12));assert(brain.companionState().backgroundRequests==0);mockWriteBudget=-1;assert(brain.save(64001));assert(brain.reserveBackground(64002,12));assert(brain.companionState().backgroundRequests==1);assert(brain.releaseBackgroundReservation(64003));assert(brain.companionState().backgroundRequests==0);
  assert(strstr(call("express_character",R"({"emotion":"happy","intensity":0.2})"),"accepted"));assert(brain.actions().pop(200,true,action)&&action.intensity==.2f);assert(strstr(call("express_character",R"({"emotion":"unknown"})"),"rejected"));
  assert(brain.forget("owner.name",63000));assert(!*brain.ownerName());static RoboBrain restarted;static livingeyes::RelationshipMemory restoredRelationships;restarted.begin(nullptr,&restoredRelationships,64000);assert(restarted.restored());assert(!*restarted.ownerName());assert(restarted.memory().findKey("drink")>=0);
  Preferences meta;meta.begin("brainmeta");meta.clear();meta.end();mockFiles.clear();
  livingeyes::SemanticMemory legacy;legacy.reset();assert(legacy.remember(livingeyes::SemanticMemoryKind::Profile,"owner.name","Lama",255,0,true));uint8_t bytes[livingeyes::SemanticMemory::WireSize];size_t written=0;assert(legacy.serialize(bytes,sizeof(bytes),written));Preferences old;old.begin("robobrain");old.putBytes("mem",bytes,written);old.end();
  mockWriteBudget=8;static RoboBrain migrating;static livingeyes::RelationshipMemory migratingRelationships;assert(migrating.begin(nullptr,&migratingRelationships,100));assert(!strcmp(migrating.ownerName(),"Lama"));mockWriteBudget=-1;
  static RoboBrain migrationRestart;static livingeyes::RelationshipMemory migrationRelationships;assert(migrationRestart.begin(nullptr,&migrationRelationships,100));assert(!strcmp(migrationRestart.ownerName(),"Lama"));assert(migrationRestart.storageGeneration()==1);old.begin("robobrain");assert(old.getBytesLength("mem")==written);old.end();
  JsonDocument tools;assert(!deserializeJson(tools,brain.toolsJson()));assert(tools[0]["functionDeclarations"].size()==10);auto externalTool=tools[0]["functionDeclarations"][6];assert(externalTool["parameters"]["required"][0]=="category");assert(externalTool["parameters"]["properties"]["category"]["enum"].size()==3);
  brain.setExternalContext("Weather cache: sunny. ","Home Assistant light.living_room=on. ","Calendar cache: private meeting. ");
  brain.transcripts().append(true,"Can you show me the weather forecast?",65010,1800000000u);const char*weatherResult=call("get_external_context",R"({"category":"weather"})");assert(strstr(weatherResult,"completed")&&strstr(weatherResult,"Weather cache: sunny")&&!strstr(weatherResult,"Home Assistant")&&!strstr(weatherResult,"private meeting"));assert(strstr(call("get_external_context",R"({"category":"calendar"})"),"privacy_gate"));brain.transcripts().finish(65011,false);
  brain.transcripts().append(true,"Is the living room light on?",65012,1800000000u);const char*homeResult=call("get_external_context",R"({"category":"home_assistant"})");assert(strstr(homeResult,"completed")&&strstr(homeResult,"light.living_room=on")&&!strstr(homeResult,"private meeting")&&!strstr(homeResult,"Weather cache"));brain.transcripts().finish(65013,false);
  brain.transcripts().append(true,"What is on my calendar tomorrow?",65014,1800000000u);assert(strstr(call("get_external_context",R"({"category":"calendar"})"),"Calendar cache: private meeting"));brain.transcripts().finish(65015,false);assert(strstr(call("get_external_context",R"({"category":"calendar"})"),"privacy_gate"));
  brain.transcripts().append(true,"Schedule a reminder to water plants",65016,1800000000u);assert(strstr(call("get_external_context",R"({"category":"calendar"})"),"privacy_gate"));brain.transcripts().finish(65017,false);
  brain.transcripts().append(true,"What's the weather temperature outside?",65018,1800000000u);assert(strstr(call("get_external_context",R"({"category":"home_assistant"})"),"privacy_gate"));brain.transcripts().finish(65019,false);
  brain.transcripts().append(true,"Can you prevent the robot from sleeping?",65019,1800000000u);assert(strstr(call("get_external_context",R"({"category":"calendar"})"),"privacy_gate"));brain.transcripts().finish(65020,false);
  brain.transcripts().append(true,"What time is it on the clock?",65020,1800000000u);assert(strstr(call("get_external_context",R"({"category":"home_assistant"})"),"privacy_gate"));brain.transcripts().finish(65021,false);
  brain.transcripts().append(true,"Can you check the robot sensor status?",65021,1800000000u);assert(strstr(call("get_external_context",R"({"category":"home_assistant"})"),"privacy_gate"));brain.transcripts().finish(65022,false);
  brain.transcripts().append(true,"What does the word weather mean?",65022,1800000000u);assert(strstr(call("get_external_context",R"({"category":"weather"})"),"privacy_gate"));brain.transcripts().finish(65023,false);
  brain.transcripts().append(true,"Say the weather forecast",65023,1800000000u);assert(strstr(call("get_external_context",R"({"category":"weather"})"),"Weather cache: sunny"));brain.transcripts().finish(65024,false);
  // GeminiStreamParser may deliver turnComplete before toolCall in one server message. Queue only category intent bits, not the private request text.
  mockNow=65021;brain.transcripts().append(true,"What is on my calendar tomorrow?",mockNow,1800000000u);brain.transcripts().finish(mockNow,false);uint8_t externalIntentMask=0;assert(brain.captureExternalIntent(externalIntentMask,mockNow));assert(externalIntentMask&2u);assert(brain.executeTool("get_external_context",R"({"category":"calendar"})",0,result,sizeof(result),externalIntentMask,mockNow,true));assert(strstr(result,"Calendar cache: private meeting"));assert(brain.executeTool("get_external_context",R"({"category":"weather"})",0,result,sizeof(result),externalIntentMask,mockNow,true));assert(strstr(result,"privacy_gate"));assert(brain.executeTool("get_external_context",R"({"category":"calendar"})",0,result,sizeof(result),externalIntentMask,0,true));assert(strstr(result,"privacy_gate"));
  assert(strstr(call("create_reminder",R"({"text":"Keep reminder","kind":"daily","minute":600})"),"completed"));uint32_t preservedReminder=brain.companionState().reminders[0].id;assert(preservedReminder);assert(brain.companionState().addSummary("Experience to clear",3,1800000002u,100));const uint32_t revisionBeforeExperiences=brain.memoryRevision();assert(brain.clearExperiences(64999));assert(brain.memory().findKey("drink")>=0);assert(!brain.companionState().summaries[0].id);assert(brain.companionState().reminders[0].id==preservedReminder);assert(brain.memoryRevision()!=revisionBeforeExperiences);assert(brain.clearMemory(65000));assert(brain.companionState().reminders[0].id==preservedReminder);
  static RoboBrain afterClear;static livingeyes::RelationshipMemory afterClearRelationships;assert(afterClear.begin(nullptr,&afterClearRelationships,65001));assert(afterClear.companionState().reminders[0].id==preservedReminder);
  assert(brain.clearAllState(65002));for(const auto&r:brain.companionState().reminders)assert(!r.id);
  brain.setMemoryEnabled(false);assert(!brain.manualRemember("disabled","value","note",65003));
  std::cout<<"PASS: brain tools, provenance, reminder retries/deduplication, memory clear isolation, snapshots and settings\n";
}
