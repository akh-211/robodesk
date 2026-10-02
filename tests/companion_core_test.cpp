#include "CompanionCore.h"
#include <cassert>
#include <iostream>
using namespace companion;
int main(){
  Memory m;for(unsigned i=0;i<96;++i){char key[40];snprintf(key,sizeof(key),"pinned.%u",i);assert(m.remember(livingeyes::SemanticMemoryKind::Profile,key,"pemilik",200,1,true));}
  assert(!m.remember(livingeyes::SemanticMemoryKind::Note,"overflow","value",255,1));
  assert(m.remember(livingeyes::SemanticMemoryKind::Profile,"pinned.0","koreksi",255,2,true));
  assert(!m.remember(livingeyes::SemanticMemoryKind::Profile,"pinned.0","dugaan",255,2,false,Source::UserTranscript));
  assert(!strcmp(m.entry(0)->value,"koreksi"));assert(m.forget("pinned.0"));
  assert(m.remember(livingeyes::SemanticMemoryKind::Preference,"minuman","kopi susu",150,2));unsigned results[10];assert(m.recall("kopi",2,results,10)==1);assert(!strcmp(m.entry(results[0])->key,"minuman"));assert(m.recall("apa ya, kopi?",2,results,10)==1);assert(!strcmp(m.entry(results[0])->key,"minuman"));
  assert(m.recall("topik tidak dikenal",2,results,5)==0);
  assert(sensitive("Saya punya diabetes"));assert(sensitive("gaji saya besar"));assert(sensitive("tanggal lahir saya"));assert(sensitive("kata sandi saya abc"));assert(sensitive("API KEY 123"));assert(sensitive("rekening saya 12345678"));assert(!sensitive("Saya suka kopi"));
  char longValue[161];memset(longValue,'x',160);longValue[160]=0;assert(!m.remember(livingeyes::SemanticMemoryKind::Note,"long",longValue,1,1));
  Transcripts t;t.append(true,"Saya suka ",100,0);t.append(true,"kopi",120,0);t.append(false,"baik",140,0);t.finish(160,false);assert(t.supports(1,"Saya suka kopi"));assert(!t.supports(1,"baik"));assert(!t.supports(1,"teh"));
  assert(!t.due(299999,0));assert(t.due(300000,0));t.append(true,"Saya suka teh",170,0);t.finish(180,true);assert(!t.supports(2,"teh"));
  char huge[1100];memset(huge,'x',1099);huge[1099]=0;t.append(true,huge,200,0);t.finish(210,false);assert(!t.supports(3,"xxx"));
  for(unsigned i=0;i<9;++i){t.append(true,"Saya suka kopi",300+i,0);t.finish(300+i,false);}assert(!t.find(1));assert(t.active==-1);
  State state;uint32_t timer=state.createReminder("Minum air",Schedule::Timer,0x20u,0);assert(timer);assert(!state.due(state.reminders[0],0xfffffff0u,0,0,0));assert(state.due(state.reminders[0],0x21u,0,0,0));
  uint32_t daily=state.createReminder("Rutinitas pagi",Schedule::Daily,0,480);assert(daily);assert(!state.due(state.reminders[1],0,0,500,0));assert(state.due(state.reminders[1],0,1800000000,500,21000));state.reminders[1].lastFiredDay=21000;assert(!state.due(state.reminders[1],0,1800000000,500,21000));assert(state.due(state.reminders[1],0,1800000000,500,21001));assert(state.cancelReminder(daily));
  assert(!state.createReminder("bad",Schedule::Daily,0,1440));for(unsigned i=1;i<8;++i)assert(state.createReminder("test",Schedule::Once,1800000000,0));assert(!state.createReminder("full",Schedule::Once,1800000000,0));
  state.resetBudget(21000);state.backgroundRequests=12;state.proactiveCount=4;state.resetBudget(21000);assert(state.backgroundRequests==12);state.resetBudget(0);assert(state.backgroundRequests==12);state.resetBudget(21001);assert(!state.backgroundRequests&&!state.proactiveCount);assert(state.valid());
  assert(state.addSummary("Saya suka kopi",10,1800000000,150));assert(state.addSummary("Kopi setelah olahraga",11,1800000200,140));assert(state.addSummary("Jalan sore di taman",12,1800000300,130));unsigned episodes[3];assert(state.recallSummaries("apa kabar kopi?",1800000400,episodes,3)==2);assert(state.recallSummaries("cerita film",1800000400,episodes,3)==0);assert(state.recallSummaries("",1800000400,episodes,2)==2);assert(!state.addSummary("password abc",10,0,1));
  Actions actions;Action a;assert(actions.push(ActionKind::Expression,Priority::Ambient,"sleepy",.5f,100,4000));assert(actions.push(ActionKind::Alert,Priority::Reminder,"Pengingat",1,100,4000));assert(actions.push(ActionKind::Expression,Priority::User,"happy",2,100,4000));assert(!actions.push(ActionKind::Expression,Priority::User,"happy",1,100,4000));assert(actions.pop(200,true,a)&&a.priority==Priority::User&&a.intensity==1);assert(!actions.pop(200,true,a));assert(actions.pop(200,false,a)&&a.priority==Priority::Reminder);actions.cancel(Priority::Ambient);assert(!actions.pop(200,false,a));
  Actions reminderActions;assert(reminderActions.push(ActionKind::Alert,Priority::Reminder,"Same text",1,100,4000,101));assert(reminderActions.push(ActionKind::Alert,Priority::Reminder,"Same text",1,100,4000,102));assert(!reminderActions.push(ActionKind::Alert,Priority::Reminder,"Same text",1,100,4000,101));assert(reminderActions.cancelSource(ActionKind::Alert,102));assert(reminderActions.pop(200,false,a)&&a.sourceId==101);assert(!reminderActions.pop(200,false,a));
  assert(actions.push(ActionKind::Expression,Priority::Ambient,"expired",1,0xfffffff0u,32));assert(!actions.pop(0x11u,false,a));
  assert(checksum("123456789",9)==0xcbf43926u);
  std::cout<<"PASS: companion capacity, protection, recall, evidence, transcript bounds, schedules, quotas, priority and CRC\n";
}
