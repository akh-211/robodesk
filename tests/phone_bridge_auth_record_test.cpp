#include "PhoneBridgeAuthRecord.h"
#include <cassert>
#include <cstring>
#include <iostream>

struct FakeStore {
  uint8_t bytes[PhoneBridgeAuthRecord::RecordSize]{};
  size_t size=0;
  bool readFails=false,writeFails=false,removeFails=false,writeVerifyFails=false;
};
bool readRecord(void*ctx,uint8_t*out,size_t cap,size_t*size){auto&s=*static_cast<FakeStore*>(ctx);if(s.readFails)return false;*size=s.size;if(!s.size)return true;if(s.size>cap)return false;memcpy(out,s.bytes,s.size);return true;}
bool writeRecord(void*ctx,const uint8_t*data,size_t size){auto&s=*static_cast<FakeStore*>(ctx);if(s.writeFails)return false;memcpy(s.bytes,data,size);s.size=size;return !s.writeVerifyFails;}
bool removeRecord(void*ctx){auto&s=*static_cast<FakeStore*>(ctx);if(s.removeFails)return false;memset(s.bytes,0,sizeof(s.bytes));s.size=0;return true;}
void validRecord(FakeStore&s){s.size=sizeof(s.bytes);memcpy(s.bytes,"RDB2",4);s.bytes[4]=1;for(size_t i=5;i<s.size;++i)s.bytes[i]=uint8_t(i);}

int main(){
  using namespace PhoneBridgeAuthRecord;
  Store callbacks;
  FakeStore store;callbacks={&store,readRecord,writeRecord,removeRecord};
  uint8_t memory[RecordSize]{};validRecord(store);memcpy(memory,store.bytes,sizeof(memory));
  bool present=false;assert(clearCredential(callbacks,memory,present));
  assert(present&&store.bytes[4]==0&&memory[4]==0);
  assert(!memcmp(memory+5,store.bytes+5,16));
  for(size_t i=KeyOffset;i<RecordSize;++i)assert(memory[i]==0&&store.bytes[i]==0);

  validRecord(store);memcpy(memory,store.bytes,sizeof(memory));uint8_t before[RecordSize];memcpy(before,memory,sizeof(before));store.writeFails=true;
  assert(!clearCredential(callbacks,memory,present));
  assert(!memcmp(memory,before,sizeof(memory))&&store.bytes[4]==1);

  store.writeFails=false;store.writeVerifyFails=true;
  assert(!clearCredential(callbacks,memory,present));
  assert(!memcmp(memory,before,sizeof(memory)));

  store.writeVerifyFails=false;store.bytes[0]='X';store.size=RecordSize;store.removeFails=true;
  assert(!clearCredential(callbacks,memory,present));
  store.removeFails=false;assert(clearCredential(callbacks,memory,present));
  assert(!present&&store.size==0);

  memset(memory,0xA5,sizeof(memory));assert(clearCredential(callbacks,memory,present));
  assert(!present);for(uint8_t byte:memory)assert(byte==0);
  std::cout<<"PASS: credential record erasure verifies persistence and fails closed on store errors\n";
}
