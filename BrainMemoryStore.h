#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <livingeyes/SemanticMemory.h>
#include <livingeyes/CharacterMind.h>
#include <livingeyes/RelationshipMemory.h>
#include <livingeyes/LongTermSocialMemory.h>
#include <livingeyes/CharacterEvolution.h>

// Application-owned persistence for the living-character brain. Core modules
// remain storage-agnostic; this adapter stores their bounded persistent states
// in one NVS namespace and validates exact blob sizes before restoring them.
class BrainMemoryStore {
 public:
  bool load(livingeyes::SemanticMemory& mem,
            livingeyes::CharacterMind& mind,
            livingeyes::RelationshipMemory& relationships,
            livingeyes::LongTermSocialMemory& longSocial,
            livingeyes::CharacterEvolution& evolution,
            uint32_t now){
    Preferences p;if(!p.begin("robobrain",true))return false;bool any=false;
    size_t len=p.getBytesLength("mem");
    if(len==livingeyes::SemanticMemory::WireSize){static uint8_t blob[livingeyes::SemanticMemory::WireSize];if(p.getBytes("mem",blob,sizeof(blob))==sizeof(blob)&&mem.deserialize(blob,sizeof(blob)))any=true;}
    livingeyes::CharacterMindState ms=mind.state();
    if(p.getBool("mindinit",false)){
      ms.energy=p.getFloat("energy",ms.energy);ms.curiosity=p.getFloat("curious",ms.curiosity);ms.affection=p.getFloat("affect",ms.affection);ms.boredom=p.getFloat("boredom",ms.boredom);ms.valence=p.getFloat("valence",ms.valence);ms.socialNeed=p.getFloat("social",ms.socialNeed);ms.trust=p.getFloat("trust",ms.trust);ms.interactions=p.getULong("interact",ms.interactions);ms.conversations=p.getULong("convos",ms.conversations);mind.restore(ms,now);any=true;
    }
    livingeyes::RelationshipPersistentState rs;
    if(p.getBytesLength("rel")==sizeof(rs)&&p.getBytes("rel",&rs,sizeof(rs))==sizeof(rs)&&relationships.restorePersistentState(rs,now))any=true;
    livingeyes::LongTermSocialPersistentState ls;
    if(p.getBytesLength("ltsocial")==sizeof(ls)&&p.getBytes("ltsocial",&ls,sizeof(ls))==sizeof(ls)&&longSocial.restorePersistentState(ls))any=true;
    livingeyes::CharacterEvolutionPersistentState es;
    if(p.getBytesLength("evo")==sizeof(es)&&p.getBytes("evo",&es,sizeof(es))==sizeof(es)&&evolution.restorePersistentState(es,longSocial.totals()))any=true;
    p.end();return any;
  }

  bool save(const livingeyes::SemanticMemory& mem,
            const livingeyes::CharacterMind& mind,
            const livingeyes::RelationshipMemory& relationships,
            const livingeyes::LongTermSocialMemory& longSocial,
            const livingeyes::CharacterEvolution& evolution,
            uint32_t now){
    static uint8_t blob[livingeyes::SemanticMemory::WireSize];size_t n=0;
    if(!mem.serialize(blob,sizeof(blob),n)){Serial.println("LEV,BRAIN,NVS_FAIL,stage=serialize");return false;}
    const livingeyes::RelationshipPersistentState rs=relationships.persistentState(now);
    const livingeyes::LongTermSocialPersistentState ls=longSocial.persistentState();
    const livingeyes::CharacterEvolutionPersistentState es=evolution.persistentState();
    Preferences p;if(!p.begin("robobrain",false)){Serial.println("LEV,BRAIN,NVS_FAIL,stage=begin");return false;}
    auto fail=[&](const char*stage,size_t got,size_t want)->bool{Serial.printf("LEV,BRAIN,NVS_FAIL,stage=%s,got=%u,want=%u\n",stage,unsigned(got),unsigned(want));p.end();return false;};

    // An empty SemanticMemory does not need a ~3.7 KB NVS blob. Avoiding that
    // write saves NVS space and wear; if an old empty blob exists, remove it.
    const size_t oldMemLen=p.getBytesLength("mem");
    if(mem.count()==0){
      if(oldMemLen&& !p.remove("mem"))return fail("mem_remove",0,1);
    }else{
      size_t w=p.putBytes("mem",blob,n);if(w!=n)return fail("mem",w,n);
    }

    const auto&ms=mind.state();size_t w=0;
    w=p.putBool("mindinit",true);if(!w)return fail("mindinit",w,1);
    w=p.putFloat("energy",ms.energy);if(w!=sizeof(float))return fail("energy",w,sizeof(float));
    w=p.putFloat("curious",ms.curiosity);if(w!=sizeof(float))return fail("curious",w,sizeof(float));
    w=p.putFloat("affect",ms.affection);if(w!=sizeof(float))return fail("affect",w,sizeof(float));
    w=p.putFloat("boredom",ms.boredom);if(w!=sizeof(float))return fail("boredom",w,sizeof(float));
    w=p.putFloat("valence",ms.valence);if(w!=sizeof(float))return fail("valence",w,sizeof(float));
    w=p.putFloat("social",ms.socialNeed);if(w!=sizeof(float))return fail("social",w,sizeof(float));
    w=p.putFloat("trust",ms.trust);if(w!=sizeof(float))return fail("trust",w,sizeof(float));
    w=p.putULong("interact",ms.interactions);if(w!=sizeof(uint32_t))return fail("interact",w,sizeof(uint32_t));
    w=p.putULong("convos",ms.conversations);if(w!=sizeof(uint32_t))return fail("convos",w,sizeof(uint32_t));
    w=p.putBytes("rel",&rs,sizeof(rs));if(w!=sizeof(rs))return fail("rel",w,sizeof(rs));
    w=p.putBytes("ltsocial",&ls,sizeof(ls));if(w!=sizeof(ls))return fail("ltsocial",w,sizeof(ls));
    w=p.putBytes("evo",&es,sizeof(es));if(w!=sizeof(es))return fail("evo",w,sizeof(es));
    p.end();return true;
  }
  bool clear(){Preferences p;if(!p.begin("robobrain",false))return false;bool ok=p.clear();p.end();return ok;}
};
