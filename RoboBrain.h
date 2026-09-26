#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <LivingEyes.h>
#include <livingeyes/SemanticMemory.h>
#include <livingeyes/CharacterMind.h>
#include <livingeyes/LongTermSocialMemory.h>
#include <livingeyes/CharacterEvolution.h>
#include "BrainMemoryStore.h"

class RoboBrain {
 public:
  static const uint32_t OwnerId=1;
  void setMemoryEnabled(bool on){memoryEnabled_=on;}
  bool memoryEnabled()const{return memoryEnabled_;}
  void begin(livingeyes::Character* robot,livingeyes::RelationshipMemory* relationships,uint32_t now){robot_=robot;relationships_=relationships;memory_.reset();mind_.reset(now);longSocial_.reset();if(relationships_)relationships_->reset(now);basePersonality_=robot_?robot_->personalityProfile():livingeyes::PersonalityProfile();evolution_.reset(basePersonality_,longSocial_.totals());restored_=relationships_?store_.load(memory_,mind_,*relationships_,longSocial_,evolution_,now):false;if(relationships_)relationships_->setOwner(OwnerId,true,now);if(robot_)robot_->setPersonalityProfile(evolution_.profile());applyMindToRobot();lastSaveAt_=lastSaveAttemptAt_=now;saveFailed_=false;}
  livingeyes::SemanticMemory& memory(){return memory_;}const livingeyes::SemanticMemory& memory()const{return memory_;}const livingeyes::CharacterMind& mind()const{return mind_;}
  bool dirty()const{return dirty_;}bool restored()const{return restored_;}uint32_t remembers()const{return remembers_;}uint32_t recalls()const{return recalls_;}uint32_t forgets()const{return forgets_;}unsigned relationshipCount()const{return relationships_?relationships_->count():0;}unsigned longSocialCount()const{return longSocial_.count();}uint16_t evolutionDays()const{return evolution_.days();}
  void update(uint32_t now,bool presence,bool touch,bool pickedUp,uint16_t day,bool proactiveVisual=true){
    mind_.tick(now,presence);
    if(presence&&!presence_){mind_.event(livingeyes::MindEvent::Presence,.7f);if(robot_&&proactiveVisual)robot_->post(livingeyes::Interaction::FaceAppeared,.55f,1600,1);} presence_=presence;
    if(touch&&!touch_){mind_.event(livingeyes::MindEvent::Touch,1.f);if(relationships_){relationships_->observe(OwnerId,true,now,true);relationships_->interact(OwnerId,livingeyes::RelationshipInteractionKind::Touch,1.f,now);}longSocial_.record(OwnerId,livingeyes::SocialMomentKind::Touch,1.f,day);dirty_=true;} touch_=touch;
    if(pickedUp&&!pickedUp_){mind_.event(livingeyes::MindEvent::PickedUp,.8f);dirty_=true;} if(!pickedUp&&pickedUp_){mind_.event(livingeyes::MindEvent::PutDown,.6f);dirty_=true;} pickedUp_=pickedUp;
    // HOTFIX16: day==0 means wall clock has not synchronized yet. Feeding that
    // synthetic day into CharacterEvolution makes every reboot look like a new
    // day, then the real synchronized day looks like another one.
    if(day!=0 && day!=lastEvolutionDay_){bool evolved=evolution_.update(longSocial_,day);if(evolved&&robot_)robot_->setPersonalityProfile(evolution_.profile());lastEvolutionDay_=day;if(evolved)dirty_=true;}
    if(now-lastMoodSyncAt_>=3000){lastMoodSyncAt_=now;applyMindToRobot();}
  }
  void onWake(uint32_t now){(void)now;mind_.event(livingeyes::MindEvent::Wake,1.f);dirty_=true;if(robot_)robot_->play(livingeyes::Clip::Wake,3);}
  void onConversationStart(uint32_t now,uint16_t day){mind_.event(livingeyes::MindEvent::ConversationStart,1.f);if(relationships_){relationships_->observe(OwnerId,true,now,true);relationships_->interact(OwnerId,livingeyes::RelationshipInteractionKind::Talk,.85f,now);}longSocial_.record(OwnerId,livingeyes::SocialMomentKind::Talk,.85f,day);dirty_=true;}
  void onConversationComplete(uint32_t now,uint16_t day,bool success=true){(void)now;mind_.event(success?livingeyes::MindEvent::ConversationSuccess:livingeyes::MindEvent::ConversationInterrupted,1.f);longSocial_.record(OwnerId,success?livingeyes::SocialMomentKind::Success:livingeyes::SocialMomentKind::Interruption,.8f,day);dirty_=true;}
  void onInterrupted(){mind_.event(livingeyes::MindEvent::ConversationInterrupted,.7f);dirty_=true;}
  bool persistenceDue(uint32_t now)const{uint32_t interval=saveFailed_?60000u:5000u;return dirty_&&uint32_t(now-lastSaveAttemptAt_)>=interval;}
  bool save(uint32_t now){lastSaveAttemptAt_=now;if(!relationships_||!store_.save(memory_,mind_,*relationships_,longSocial_,evolution_,now)){saveFailed_=true;return false;}dirty_=false;saveFailed_=false;lastSaveAt_=now;return true;}
  bool clearMemory(uint32_t now){memory_.reset();mind_.reset(now);longSocial_.reset();if(relationships_){relationships_->reset(now);relationships_->setOwner(OwnerId,true,now);}evolution_.reset(basePersonality_,longSocial_.totals());if(robot_)robot_->setPersonalityProfile(evolution_.profile());restored_=false;dirty_=true;return store_.clear();}

  const char* ownerName()const{int i=memory_.findKey("owner.name");const auto*e=i>=0?memory_.entry(unsigned(i)):0;return e?e->value:"";}
  bool setOwnerName(const char* name,uint16_t day){if(!name||!name[0])return false;bool ok=memory_.remember(livingeyes::SemanticMemoryKind::Profile,"owner.name",name,255,day,true);if(ok){dirty_=true;++remembers_;}return ok;}

  size_t buildSystemPrompt(char*out,size_t cap,const char*robotName,const char*speechStyle,uint16_t day)const{
    if(!out||!cap)return 0;
    size_t n=0;append(out,cap,n,"You are %s, a small embodied desktop robot with a continuing identity. RESPOND IN BAHASA INDONESIA (id-ID). YOU MUST RESPOND UNMISTAKABLY IN BAHASA INDONESIA unless the user explicitly asks for another language. Do not switch languages merely because an automatic speech transcript resembles French, Spanish, Italian, or another language; ASR can be wrong. Speak at a natural conversational pace, not deliberately slow, with concise sentences and short natural pauses. Be warm, curious, playful, concise, emotionally restrained, and never claim sensors that are not provided. %s ",robotName&&robotName[0]?robotName:"RoboDesk",speechStyle?speechStyle:"");
    if(memoryEnabled_)append(out,cap,n,"You have LOCAL persistent memory on the robot. Use recall_memory when prior facts may matter. Use remember_fact for stable user facts/preferences or when the user explicitly asks you to remember something. Never store passwords, API keys, authentication tokens, PINs, or secrets. Use forget_memory when the user asks to forget a remembered fact. ");else append(out,cap,n,"Persistent personal memory is disabled by the owner; do not claim that you saved facts across reboots. ");append(out,cap,n,"Use express_character sparingly to coordinate nonverbal emotion with speech. ");
    append(out,cap,n,"Current inner state: mood=%s energy=%.2f curiosity=%.2f affection=%.2f boredom=%.2f valence=%.2f social_need=%.2f trust=%.2f. ",mind_.moodName(),mind_.state().energy,mind_.state().curiosity,mind_.state().affection,mind_.state().boredom,mind_.state().valence,mind_.state().socialNeed,mind_.state().trust);
    const char* owner=ownerName();if(memoryEnabled_&&owner[0])append(out,cap,n,"The primary owner's remembered name is %s. ",owner);
    if(memoryEnabled_)append(out,cap,n,"Persistent memory summary: ");
    bool used[livingeyes::SemanticMemory::Capacity]={};unsigned emitted=0;
    while(memoryEnabled_&&emitted<10){int best=-1;int score=-1;for(unsigned i=0;i<livingeyes::SemanticMemory::Capacity;i++){const auto*e=memory_.entry(i);if(!e||used[i])continue;int s=int(e->importance)+(e->pinned?256:0);if(s>score){score=s;best=int(i);}}if(best<0)break;used[unsigned(best)]=true;const auto*e=memory_.entry(unsigned(best));append(out,cap,n,"[%s:%s=%s] ",livingeyes::SemanticMemory::kindName(e->kind),e->key,e->value);++emitted;}
    append(out,cap,n,"TodayIndex=%u. Preserve your character continuity across reconnects and reboots using local memory rather than pretending the cloud session is permanent.",unsigned(day));return n;
  }

  size_t buildTurnContext(char*out,size_t cap,bool pickedUp,bool touch,bool pir,float temp,float hum,float pressure,float g,float gyro,uint8_t activity)const{
    if(!out||!cap)return 0;
    return size_t(snprintf(out,cap,"[Embodied state: mood=%s energy=%.2f curiosity=%.2f affection=%.2f boredom=%.2f social_need=%.2f trust=%.2f; picked_up=%s touch=%s presence=%s temperature_c=%.2f humidity_pct=%.1f pressure_hpa=%.2f g=%.3f gyro=%.3f activity=%u. Use only when relevant.]",mind_.moodName(),mind_.state().energy,mind_.state().curiosity,mind_.state().affection,mind_.state().boredom,mind_.state().socialNeed,mind_.state().trust,pickedUp?"true":"false",touch?"true":"false",pir?"true":"false",temp,hum,pressure,g,gyro,unsigned(activity)));
  }

  const char* toolsJson()const{
    return "[{\"functionDeclarations\":["
      "{\"name\":\"remember_fact\",\"description\":\"Store a stable user fact or preference in local persistent robot memory. Never store credentials or secrets.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"key\":{\"type\":\"STRING\"},\"value\":{\"type\":\"STRING\"},\"category\":{\"type\":\"STRING\",\"enum\":[\"profile\",\"preference\",\"event\",\"routine\",\"place\",\"note\"]},\"importance\":{\"type\":\"INTEGER\",\"minimum\":1,\"maximum\":100}},\"required\":[\"key\",\"value\"]}},"
      "{\"name\":\"recall_memory\",\"description\":\"Search local persistent robot memory for information relevant to a query.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"query\":{\"type\":\"STRING\"}},\"required\":[\"query\"]}},"
      "{\"name\":\"forget_memory\",\"description\":\"Delete one local memory by its key when the user asks to forget it.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"key\":{\"type\":\"STRING\"}},\"required\":[\"key\"]}},"
      "{\"name\":\"set_owner_name\",\"description\":\"Persist the primary user's preferred name.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"name\":{\"type\":\"STRING\"}},\"required\":[\"name\"]}},"
      "{\"name\":\"express_character\",\"description\":\"Choose a brief nonverbal facial expression or gesture matching the conversation.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"emotion\":{\"type\":\"STRING\",\"enum\":[\"happy\",\"curious\",\"shy\",\"love\",\"sad\",\"surprised\",\"thinking\",\"agree\",\"disagree\",\"wink\",\"laugh\",\"sleepy\"]},\"intensity\":{\"type\":\"NUMBER\"}},\"required\":[\"emotion\"]}}"
      "]}]";
  }

  bool executeTool(const char*name,const char*args,uint16_t day,char*out,size_t cap){if(!out||!cap)return false;out[0]=0;if(!name)return false;
    if(strcmp(name,"remember_fact")==0){if(!memoryEnabled_){snprintf(out,cap,"{\"status\":\"disabled\"}");return true;}char key[40],value[160],cat[24];jsonString(args,"key",key,sizeof(key));jsonString(args,"value",value,sizeof(value));jsonString(args,"category",cat,sizeof(cat));int imp=jsonInt(args,"importance",70);if(secretLike(key)||secretLike(value)){snprintf(out,cap,"{\"status\":\"rejected\",\"reason\":\"sensitive_secret\"}");return true;}bool ok=memory_.remember(livingeyes::SemanticMemory::kindFromName(cat),key,value,uint8_t(imp<1?1:imp>100?255:int(imp*255/100)),day,false);if(ok){dirty_=true;++remembers_;}snprintf(out,cap,"{\"status\":\"%s\"}",ok?"remembered":"invalid");return true;}
    if(strcmp(name,"set_owner_name")==0){if(!memoryEnabled_){snprintf(out,cap,"{\"status\":\"disabled\"}");return true;}char value[96];jsonString(args,"name",value,sizeof(value));bool ok=setOwnerName(value,day);snprintf(out,cap,"{\"status\":\"%s\"}",ok?"remembered":"invalid");return true;}
    if(strcmp(name,"forget_memory")==0){if(!memoryEnabled_){snprintf(out,cap,"{\"status\":\"disabled\"}");return true;}char key[40];jsonString(args,"key",key,sizeof(key));bool ok=memory_.forget(key);if(ok){dirty_=true;++forgets_;}snprintf(out,cap,"{\"status\":\"%s\"}",ok?"forgotten":"not_found");return true;}
    if(strcmp(name,"recall_memory")==0){if(!memoryEnabled_){snprintf(out,cap,"{\"status\":\"disabled\",\"memories\":[]}");return true;}char q[96];jsonString(args,"query",q,sizeof(q));unsigned idx[5];unsigned n=memory_.recall(q,day,idx,5);++recalls_;size_t p=0;append(out,cap,p,"{\"status\":\"ok\",\"memories\":[");for(unsigned i=0;i<n;i++){const auto*e=memory_.entry(idx[i]);if(i)append(out,cap,p,",");appendEsc(out,cap,p,"{\"key\":\"");appendEscaped(out,cap,p,e->key);appendEsc(out,cap,p,"\",\"value\":\"");appendEscaped(out,cap,p,e->value);appendEsc(out,cap,p,"\",\"category\":\"");appendEscaped(out,cap,p,livingeyes::SemanticMemory::kindName(e->kind));appendEsc(out,cap,p,"\"}");}appendEsc(out,cap,p,"]}");return true;}
    if(strcmp(name,"express_character")==0){char e[32];jsonString(args,"emotion",e,sizeof(e));bool ok=express(e);snprintf(out,cap,"{\"status\":\"%s\",\"emotion\":\"%s\"}",ok?"shown":"unknown",e);return true;}
    snprintf(out,cap,"{\"status\":\"unknown_tool\"}");return false;}

 private:
  bool memoryEnabled_=true;livingeyes::Character* robot_=0;livingeyes::RelationshipMemory* relationships_=0;livingeyes::SemanticMemory memory_;livingeyes::CharacterMind mind_;livingeyes::LongTermSocialMemory longSocial_;livingeyes::CharacterEvolution evolution_;livingeyes::PersonalityProfile basePersonality_;BrainMemoryStore store_;bool dirty_=false,restored_=false,presence_=false,touch_=false,pickedUp_=false,saveFailed_=false;uint32_t lastSaveAt_=0,lastSaveAttemptAt_=0,lastMoodSyncAt_=0,remembers_=0,recalls_=0,forgets_=0;uint16_t lastEvolutionDay_=0xffff;
  void applyMindToRobot(){if(!robot_)return;const auto current=robot_->mood();auto m=mind_.mood();m.energy=current.energy*.45f+m.energy*.55f;m.curiosity=current.curiosity*.45f+m.curiosity*.55f;m.affection=current.affection*.45f+m.affection*.55f;m.boredom=current.boredom*.45f+m.boredom*.55f;m.valence=current.valence*.45f+m.valence*.55f;robot_->restoreMood(m);}
  static bool secretLike(const char*s){if(!s)return false;char b[192];size_t n=0;for(;*s&&n+1<sizeof(b);++s){char c=*s;b[n++]=c>='A'&&c<='Z'?char(c+32):c;}b[n]=0;return strstr(b,"password")||strstr(b,"api_key")||strstr(b,"api key")||strstr(b,"secret")||strstr(b,"token")||strstr(b,"pin=")||strstr(b,"wifi password");}
  static bool jsonString(const char*src,const char*key,char*out,size_t cap){if(!out||!cap){return false;}out[0]=0;if(!src||!key)return false;char pat[64];snprintf(pat,sizeof(pat),"\"%s\"",key);const char*p=strstr(src,pat);if(!p)return false;p=strchr(p,':');if(!p)return false;++p;while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;if(*p!='\"')return false;++p;size_t n=0;while(*p&&*p!='\"'){char c=*p++;if(c=='\\'&&*p){char e=*p++;if(e=='n')c='\n';else if(e=='r')c='\r';else if(e=='t')c='\t';else c=e;}if(n+1<cap)out[n++]=c;}out[n]=0;return n>0;}
  static int jsonInt(const char*src,const char*key,int fallback){if(!src)return fallback;char pat[64];snprintf(pat,sizeof(pat),"\"%s\"",key);const char*p=strstr(src,pat);if(!p)return fallback;p=strchr(p,':');if(!p)return fallback;return atoi(p+1);}
  bool express(const char*e){if(!robot_||!e)return false;livingeyes::Clip c=livingeyes::Clip::Curious;if(!strcmp(e,"happy"))c=livingeyes::Clip::Happy;else if(!strcmp(e,"curious"))c=livingeyes::Clip::Curious;else if(!strcmp(e,"shy"))c=livingeyes::Clip::Shy;else if(!strcmp(e,"love"))c=livingeyes::Clip::Love;else if(!strcmp(e,"sad"))c=livingeyes::Clip::Sad;else if(!strcmp(e,"surprised"))c=livingeyes::Clip::Surprise;else if(!strcmp(e,"thinking"))c=livingeyes::Clip::Think;else if(!strcmp(e,"agree"))c=livingeyes::Clip::Agree;else if(!strcmp(e,"disagree"))c=livingeyes::Clip::Disagree;else if(!strcmp(e,"wink"))c=livingeyes::Clip::Wink;else if(!strcmp(e,"laugh"))c=livingeyes::Clip::Giggle;else if(!strcmp(e,"sleepy"))c=livingeyes::Clip::Sleepy;else return false;return robot_->play(c,2);}
  static void append(char*out,size_t cap,size_t&n,const char*fmt,...){if(n>=cap)return;va_list ap;va_start(ap,fmt);int w=vsnprintf(out+n,cap-n,fmt,ap);va_end(ap);if(w>0)n+=size_t(w)<cap-n?size_t(w):cap-n-1;}
  static void appendEsc(char*out,size_t cap,size_t&n,const char*s){if(!s)return;while(*s&&n+1<cap)out[n++]=*s++;out[n]=0;}
  static void appendEscaped(char*out,size_t cap,size_t&n,const char*s){if(!s)return;for(;*s&&n+2<cap;s++){char c=*s;if(c=='\"'||c=='\\'){out[n++]='\\';out[n++]=c;}else if(uint8_t(c)>=0x20)out[n++]=c;}out[n]=0;}
};
