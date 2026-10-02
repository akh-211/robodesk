#include "OfflineVoiceCommands.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main(){
  assert(offline_voice::kCommandCount==offline_voice::CommandCount);
  for(unsigned i=0;i<offline_voice::kCommandCount;++i){
    const auto& command=offline_voice::kCommands[i];
    assert(command.id==static_cast<int>(i));
    assert(command.phrase&&command.phrase[0]);
    assert(offline_voice::commandForId(command.id)==&command);
    for(unsigned j=i+1;j<offline_voice::kCommandCount;++j)
      assert(std::strcmp(command.phrase,offline_voice::kCommands[j].phrase)!=0);
  }
  for(int id=offline_voice::Happy;id<=offline_voice::Sleepy;++id)
    assert(offline_voice::commandForId(id)->expression);
  assert(!offline_voice::commandForId(offline_voice::Status)->expression);
  assert(!offline_voice::commandForId(offline_voice::PauseInitiative)->expression);
  assert(!offline_voice::commandForId(offline_voice::ResumeInitiative)->expression);
  assert(!offline_voice::commandForId(-1));
  assert(!offline_voice::commandForId(offline_voice::CommandCount));
  std::cout<<"PASS: 12 expression commands, 3 utility commands, unique English phrases and ID bounds\n";
}
