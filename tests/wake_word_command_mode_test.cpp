#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_MODEL_IN_FLASH 1
#include "WakeWordController.h"
#include <cassert>
#include <iostream>

int main(){
  I2SClass mic;
  WakeWordController controller;
  const sr_cmd_t commands[]={{4,"Check status"},{7,"Pause initiative"}};
  assert(controller.armCommands(mic,commands,2));
  assert(controller.commanding()&&controller.state()==WakeWordController::Commanding);
  assert(ESP_SR.lastMode==SR_MODE_COMMAND&&ESP_SR.commandCount==2);
  assert(mic.lastRate==16000&&mic.lastTransform==I2S_RX_TRANSFORM_32_TO_16);
  ESP_SR.emit(SR_EVENT_COMMAND,7,0);
  assert(ESP_SR.lastMode==SR_MODE_COMMAND&&ESP_SR.setModeCalls==1);
  int commandId=-1;
  assert(controller.consumeCommand(commandId)&&commandId==7);
  assert(!controller.consumeCommand(commandId));
  assert(controller.commandDetections()==1);
  ESP_SR.emit(SR_EVENT_TIMEOUT);
  assert(ESP_SR.setModeCalls==2&&controller.commanding());
  assert(controller.arm(mic));
  assert(!controller.commanding()&&controller.running());
  assert(ESP_SR.endCalls==1&&ESP_SR.lastMode==SR_MODE_WAKEWORD&&ESP_SR.commandCount==0);
  assert(controller.disarmToManual(mic));
  assert(controller.state()==WakeWordController::Dormant&&mic.lastTransform==I2S_RX_TRANSFORM_NONE);
  ESP_SR.beginOK=false;
  assert(!controller.armCommands(mic,commands,2));
  assert(controller.state()==WakeWordController::Fault&&controller.beginFailures()==1);
  assert(mic.lastTransform==I2S_RX_TRANSFORM_NONE);
  std::cout<<"PASS: command mode ownership, command/timeout rearm, wake return and start-failure recovery\n";
}
