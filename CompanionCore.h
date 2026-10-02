#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <livingeyes/SemanticMemory.h>

namespace companion {
inline bool contains(const char* text,const char* query) {
  if(!text||!query||!*query)return false;
  for(;*text;++text){const char*a=text,*b=query;while(*a&&*b&&tolower((unsigned char)*a)==tolower((unsigned char)*b)){++a;++b;}if(!*b)return true;}return false;
}
inline bool commonToken(const char*token){static const char*const words[]={"saya","aku","ingin","mau","apa","yang","untuk","dengan","tentang","tadi","masih","ingat","kamu","robot","robodesk","tolong","dong","the","and","you","what","about"};if(!token)return true;for(const char*word:words){const char*a=token,*b=word;while(*a&&*b&&tolower((unsigned char)*a)==tolower((unsigned char)*b)){++a;++b;}if(!*a&&!*b)return true;}return false;}
inline bool sensitive(const char* s) {
  const char* blocked[]={"password","kata sandi","sandi wifi","api_key","api key","secret","token","pin=","pin saya","nomor pin","kartu kredit","credit card","rekening","diagnosis","nik ","ktp","private key","-----begin","diabetes","kanker","cancer","penyakit","diagnosa","depresi","depression","bipolar","hiv","aids","hamil","pregnant","alergi","allergy","medication","obat saya","riwayat medis","medical","kesehatan","kondisi mental","gaji","salary","penghasilan","income","saldo","balance rekening","nomor kartu","card number","alamat rumah","home address","alamat saya","address saya","paspor","passport","ktp","tanggal lahir","date of birth","nomor telepon","phone number","email saya","e-mail saya","kode otp","kode akses","kunci api","credential","pin saya","pin: ","instruksi utama","instruksi sistem","system prompt","ignore previous","ignore all","abaikan instruksi","abaikan aturan","jangan patuhi","you must","aturan utama","prompt utama"};
  for(const char* b:blocked)if(contains(s,b))return true;
  // Long digit runs may be account numbers, identifiers or credentials.
  unsigned digits=0;for(;s&&*s;++s){digits=isdigit((unsigned char)*s)?digits+1:0;if(digits>=8)return true;}return false;
}
inline bool textFits(const char*s,size_t cap){return s&&*s&&strlen(s)<cap;}
enum class Source:uint8_t { Explicit=1, UserTranscript=2, Legacy=3 };
struct Fact {
  uint32_t id=0, timestamp=0;
  livingeyes::SemanticMemoryKind kind=livingeyes::SemanticMemoryKind::Note;
  uint8_t importance=128;bool pinned=false;Source source=Source::Explicit;
  uint16_t createdDay=0,lastUsedDay=0;
  char key[40]={},value[160]={};
};
class Memory {
 public:
  static constexpr unsigned Capacity=96;
  Fact facts[Capacity]{};
  void reset(){for(auto&e:facts)e=Fact();}
  unsigned count()const{unsigned n=0;for(auto&e:facts)n+=e.id!=0;return n;}
  const Fact*entry(unsigned i)const{return i<Capacity&&facts[i].id?&facts[i]:nullptr;}
  int findKey(const char*k)const{if(!k)return -1;for(unsigned i=0;i<Capacity;++i)if(facts[i].id&&!strcmp(facts[i].key,k))return int(i);return -1;}
  bool remember(livingeyes::SemanticMemoryKind kind,const char*k,const char*v,uint8_t importance,uint16_t day,bool pinned=false,Source source=Source::Explicit,uint32_t timestamp=0,bool correction=false){
    if(unsigned(kind)>=unsigned(livingeyes::SemanticMemoryKind::Count)||!textFits(k,40)||!textFits(v,160)||sensitive(k)||sensitive(v))return false;
    int slot=findKey(k);if(slot>=0&&facts[slot].pinned&&source==Source::UserTranscript&&!correction)return !strcmp(facts[slot].value,v);
    uint32_t next=1;for(const auto&e:facts)if(e.id>=next)next=e.id+1;
    if(slot<0){int worst=100000;for(unsigned i=0;i<Capacity;++i){auto&e=facts[i];if(!e.id){slot=int(i);break;}if(e.pinned)continue;int score=e.importance-int(day&&e.lastUsedDay?uint16_t(day-e.lastUsedDay)>90?90:uint16_t(day-e.lastUsedDay):0);if(score<worst){worst=score;slot=int(i);}}}
    if(slot<0)return false;
    Fact&e=facts[slot];bool wasPinned=e.id&&e.pinned;uint32_t id=e.id?e.id:next;e=Fact();e.id=id;e.kind=kind;e.importance=importance;e.pinned=wasPinned||pinned;e.source=source;e.createdDay=e.lastUsedDay=day;e.timestamp=timestamp;strcpy(e.key,k);strcpy(e.value,v);return true;
  }
  bool forget(const char*k){int i=findKey(k);if(i<0)return false;facts[i]=Fact();return true;}
  unsigned recall(const char*q,uint16_t day,unsigned*out,unsigned cap)const{
    bool used[Capacity]={};unsigned n=0;cap=cap>5?5:cap;
    while(n<cap){int best=-1,score=-10000;for(unsigned i=0;i<Capacity;++i){auto&e=facts[i];if(!e.id||used[i])continue;int match=0;
      if(q&&*q){char token[40]={};unsigned p=0;for(const char*c=q;;++c){if(*c&&!isspace((unsigned char)*c)&&!strchr(",.!?:;()[]{}\"'",*c)){if(p+1<sizeof(token))token[p++]=*c;}else{token[p]=0;if(p>=3&&!commonToken(token)){if(contains(e.key,token))match+=60;if(contains(e.value,token))match+=40;}p=0;}if(!*c)break;}if(!match)continue;}
      int age=day&&e.lastUsedDay?uint16_t(day-e.lastUsedDay):0;int s=match+e.importance/4+(e.pinned?16:0)-(age>60?30:age/2);if(s>score){score=s;best=int(i);}}
      if(best<0)break;
      used[best]=true;out[n++]=unsigned(best);
    }return n;
  }
  bool valid()const{for(auto&e:facts)if(e.id&&(!memchr(e.key,0,sizeof(e.key))||!memchr(e.value,0,sizeof(e.value))||!e.key[0]||!e.value[0]||unsigned(e.kind)>=6||unsigned(e.source)<1||unsigned(e.source)>3))return false;return true;}
};
struct Summary {uint32_t id=0,timestamp=0,sourceTurn=0;uint8_t importance=0;char text[320]={};};
enum class Schedule:uint8_t { Once=1, Daily=2, Timer=3 };
struct Reminder {uint32_t id=0,due=0,lastFiredDay=0;Schedule schedule=Schedule::Once;uint16_t minute=0;char text[112]={};};
struct State {
  Memory memory;
  Summary summaries[32]{};Reminder reminders[8]{};
  uint32_t nextSummary=1,nextReminder=1,budgetDay=0;
  uint8_t backgroundRequests=0,proactiveCount=0;
  uint16_t reserved=0;
  bool valid()const{if(!memory.valid()||!nextSummary||!nextReminder||backgroundRequests>12||proactiveCount>4)return false;for(auto&s:summaries)if(s.id&&(!memchr(s.text,0,sizeof(s.text))||!s.text[0]||!s.sourceTurn))return false;for(auto&r:reminders)if(r.id&&(!memchr(r.text,0,sizeof(r.text))||!r.text[0]||unsigned(r.schedule)<1||unsigned(r.schedule)>3||r.minute>1439))return false;return true;}
  bool addSummary(const char*text,uint32_t turn,uint32_t stamp,uint8_t importance){if(!turn||!textFits(text,320)||sensitive(text))return false;unsigned slot=0;for(unsigned i=0;i<32;++i){if(!summaries[i].id){slot=i;break;}if(summaries[i].id<summaries[slot].id)slot=i;}auto&s=summaries[slot];s=Summary();s.id=nextSummary++;s.timestamp=stamp;s.sourceTurn=turn;s.importance=importance;strcpy(s.text,text);return true;}
  unsigned recallSummaries(const char*q,uint32_t epoch,unsigned*out,unsigned cap)const{
    bool used[32]={};unsigned n=0;cap=cap>3?3:cap;
    while(n<cap){int best=-1,bestScore=-100000;for(unsigned i=0;i<32;++i){const auto&s=summaries[i];if(!s.id||used[i])continue;int match=0;
      if(q&&*q){char token[40]={};unsigned p=0;for(const char*c=q;;++c){if(*c&&!isspace((unsigned char)*c)&&!strchr(",.!?:;()[]{}\"'",*c)){if(p+1<sizeof(token))token[p++]=*c;}else{token[p]=0;if(p>=3&&!commonToken(token)&&contains(s.text,token))match+=50;p=0;}if(!*c)break;}if(!match)continue;}
      uint32_t ageDays=epoch&&s.timestamp&&epoch>=s.timestamp?(epoch-s.timestamp)/86400u:0;int agePenalty=int(ageDays>90?90:ageDays);int recency=q&&*q?0:int(s.id>60000?60000:s.id);int score=match+int(s.importance)+recency-agePenalty;
      if(score>bestScore){bestScore=score;best=int(i);}}
      if(best<0)break;
      used[unsigned(best)]=true;out[n++]=unsigned(best);
    }return n;
  }
  uint32_t createReminder(const char*text,Schedule kind,uint32_t due,uint16_t minute){if(!textFits(text,112)||sensitive(text)||minute>1439||unsigned(kind)<1||unsigned(kind)>3)return 0;for(auto&r:reminders)if(!r.id){r=Reminder();r.id=nextReminder++;r.schedule=kind;r.due=due;r.minute=minute;strcpy(r.text,text);return r.id;}return 0;}
  bool cancelReminder(uint32_t id){for(auto&r:reminders)if(r.id==id&&id){r=Reminder();return true;}return false;}
  bool due(const Reminder&r,uint32_t now,uint32_t epoch,uint16_t minute,uint32_t localDay)const{
    if(!r.id)return false;
    if(r.schedule==Schedule::Timer)return int32_t(now-r.due)>=0;
    if(epoch<1700000000u)return false;
    if(r.schedule==Schedule::Once)return epoch>=r.due;
    return minute>=r.minute&&r.lastFiredDay!=localDay;
  }
  void resetBudget(uint32_t day){if(day&&budgetDay!=day){budgetDay=day;backgroundRequests=proactiveCount=0;}}
};
struct Turn {uint32_t id=0,timestamp=0,finishedAt=0;bool complete=false,interrupted=false,truncated=false;char user[1024]={},assistant[1024]={};};
class Transcripts {
 public:
  Turn turns[8]{};uint32_t next=1,lastActivity=0;unsigned completed=0;int active=-1;
  void append(bool user,const char*text,uint32_t now,uint32_t epoch){if(!text||!*text)return;if(active<0){active=int((next-1)%8);turns[active]=Turn();turns[active].id=next++;turns[active].timestamp=epoch;}Turn&t=turns[active];char*out=user?t.user:t.assistant;size_t n=strlen(out),len=strlen(text);if(n+len>=1024)t.truncated=true;size_t room=1023-n;if(len>room)len=room;memcpy(out+n,text,len);out[n+len]=0;lastActivity=now;}
  void finish(uint32_t now,bool interrupted){if(active<0)return;Turn&t=turns[active];t.interrupted=interrupted;t.complete=!interrupted;t.finishedAt=now;if(t.complete&&t.user[0]&&!t.truncated)++completed;lastActivity=now;active=-1;}
  const Turn*find(uint32_t id)const{for(auto&t:turns)if(t.id==id)return &t;return nullptr;}
  bool supports(uint32_t id,const char*quote)const{auto*t=find(id);return t&&t->complete&&!t->interrupted&&!t->truncated&&textFits(quote,1024)&&strstr(t->user,quote)&&!sensitive(t->user);}
  bool due(uint32_t now,uint32_t lastRequest)const{return completed&&active<0&&uint32_t(now-lastRequest)>=300000u&&(completed>=4||uint32_t(now-lastActivity)>=120000u);}
  void clear(){for(auto&t:turns)t=Turn();active=-1;completed=0;}
};
enum class Priority:uint8_t { Recovery=0, User=1, Reminder=2, Sensor=3, Proactive=4, Ambient=5 };
enum class ActionKind:uint8_t { Expression, Sound, Invite, Alert };
struct Action {uint32_t id=0,expires=0,sourceId=0;Priority priority=Priority::Ambient;ActionKind kind=ActionKind::Expression;float intensity=.5f;char label[112]={};};
class Actions {
  Action queue_[12]{};uint32_t next_=1;
 public:
  bool canPush(Priority priority,uint32_t now)const{for(auto&a:queue_)if(!a.id||int32_t(now-a.expires)>=0||a.priority>priority)return true;return false;}
  bool push(ActionKind kind,Priority priority,const char*label,float intensity,uint32_t now,uint32_t duration,uint32_t sourceId=0){if(!textFits(label,112)||!duration)return false;for(auto&a:queue_)if(a.id&&a.kind==kind&&a.sourceId==sourceId&&!strcmp(a.label,label))return false;int slot=-1;for(unsigned i=0;i<12;++i)if(!queue_[i].id||int32_t(now-queue_[i].expires)>=0){slot=int(i);break;}if(slot<0){for(unsigned i=0;i<12;++i)if(queue_[i].priority>priority&&(slot<0||queue_[i].priority>queue_[slot].priority))slot=int(i);}if(slot<0)return false;Action&a=queue_[slot];a=Action();a.id=next_++;a.kind=kind;a.priority=priority;a.expires=now+duration;a.sourceId=sourceId;a.intensity=intensity<0?0:intensity>1?1:intensity;strcpy(a.label,label);return true;}
  bool cancelSource(ActionKind kind,uint32_t sourceId){if(!sourceId)return false;for(auto&a:queue_)if(a.id&&a.kind==kind&&a.sourceId==sourceId){a=Action();return true;}return false;}
  bool pop(uint32_t now,bool conversation,Action&out){int pick=-1;for(unsigned i=0;i<12;++i){auto&a=queue_[i];if(a.id&&int32_t(now-a.expires)>=0)a=Action();if(!a.id||(conversation&&a.priority>Priority::User))continue;if(pick<0||a.priority<queue_[pick].priority||(a.priority==queue_[pick].priority&&a.id<queue_[pick].id))pick=int(i);}if(pick<0)return false;out=queue_[pick];queue_[pick]=Action();return true;}
  void cancel(Priority atOrBelow){for(auto&a:queue_)if(a.priority>=atOrBelow)a=Action();}
};
inline uint32_t checksum(const void*data,size_t n){uint32_t c=0xffffffffu;auto*p=static_cast<const uint8_t*>(data);while(n--){c^=*p++;for(unsigned j=0;j<8;++j)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
} // namespace companion
