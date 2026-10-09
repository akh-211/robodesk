#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace PhoneBridgeAuthRecord {
constexpr size_t RecordSize=53;
constexpr size_t KeyOffset=21;
constexpr size_t KeySize=32;
using ReadRecord=bool(*)(void*,uint8_t*,size_t,size_t*);
using WriteVerified=bool(*)(void*,const uint8_t*,size_t);
using RemoveVerified=bool(*)(void*);

struct Store {
  void* context=nullptr;
  ReadRecord read=nullptr;
  WriteVerified writeVerified=nullptr;
  RemoveVerified removeVerified=nullptr;
};

// Clear only the credential bits while preserving a valid stable robot ID.
// Memory changes only after storage confirms the write or removal.
inline bool clearCredential(const Store&store,uint8_t memory[RecordSize],bool&recordPresent){
  if(!store.read||!store.writeVerified||!store.removeVerified||!memory)return false;
  uint8_t current[RecordSize]{},next[RecordSize]{};size_t size=0;
  if(!store.read(store.context,current,sizeof(current),&size))return false;
  if(size==0){memset(memory,0,RecordSize);recordPresent=false;memset(current,0,sizeof(current));return true;}
  const bool valid=size==RecordSize&&!memcmp(current,"RDB2",4)&&(current[4]&~3u)==0&&(!(current[4]&2u)||(current[4]&1u));
  if(!valid){const bool removed=store.removeVerified(store.context);if(removed){memset(memory,0,RecordSize);recordPresent=false;}memset(current,0,sizeof(current));return removed;}
  memcpy(next,current,RecordSize);next[4]=uint8_t(next[4]&~3u);memset(next+KeyOffset,0,KeySize);
  const bool unchanged=!memcmp(next,current,RecordSize);
  const bool written=unchanged||store.writeVerified(store.context,next,RecordSize);
  if(written){memcpy(memory,next,RecordSize);recordPresent=true;}
  memset(current,0,sizeof(current));memset(next,0,sizeof(next));return written;
}
} // namespace PhoneBridgeAuthRecord
