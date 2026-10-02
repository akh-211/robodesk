#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <livingeyes/SemanticMemory.h>
#include <livingeyes/CharacterMind.h>
#include <livingeyes/RelationshipMemory.h>
#include <livingeyes/LongTermSocialMemory.h>
#include <livingeyes/CharacterEvolution.h>
#include "SnapshotStore.h"
#include "RoboAllocation.h"

class BrainMemoryStore {
  struct Payload {
    companion::State companionState;
    livingeyes::CharacterMindState mind;
    livingeyes::RelationshipPersistentState relationships;
    livingeyes::LongTermSocialPersistentState social;
    livingeyes::CharacterEvolutionPersistentState evolution;
  };
  SnapshotStore snapshots_;
  Payload*payload_=nullptr;
 public:
  bool begin(){if(!payload_)payload_=roboAllocate<Payload>();return payload_&&snapshots_.begin();}
  bool available()const{return payload_&&snapshots_.available();}
  uint32_t generation()const{return snapshots_.generation();}
  bool legacyAllowed()const{return snapshots_.legacyAllowed();}
  bool load(companion::State& state,livingeyes::CharacterMind& mind,livingeyes::RelationshipMemory& relationships,livingeyes::LongTermSocialMemory& social,livingeyes::CharacterEvolution& evolution,uint32_t now){
    if(!payload_||!snapshots_.load(payload_,sizeof(*payload_))||!payload_->companionState.valid())return false;
    if(!relationships.restorePersistentState(payload_->relationships,now)||!social.restorePersistentState(payload_->social)||!evolution.restorePersistentState(payload_->evolution,social.totals()))return false;
    state=payload_->companionState;
    // Monotonic deadlines cannot survive a cold boot.
    for(auto&r:state.reminders)if(r.schedule==companion::Schedule::Timer)r=companion::Reminder();
    mind.restore(payload_->mind,now);return true;
  }
  bool save(const companion::State& state,const livingeyes::CharacterMind& mind,const livingeyes::RelationshipMemory& relationships,const livingeyes::LongTermSocialMemory& social,const livingeyes::CharacterEvolution& evolution,uint32_t now,bool privacy=false){
    if(!payload_)return false;
    payload_->companionState=state;payload_->mind=mind.state();payload_->relationships=relationships.persistentState(now);payload_->social=social.persistentState();payload_->evolution=evolution.persistentState();
    return state.valid()&&(privacy?snapshots_.scrubRecovery(payload_,sizeof(*payload_)):snapshots_.save(payload_,sizeof(*payload_)));
  }
  bool loadLegacy(livingeyes::SemanticMemory& mem,
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

};
