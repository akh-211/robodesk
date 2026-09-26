#pragma once
#include "FreeRTOS.h"
inline int xTaskCreatePinnedToCore(void (*fn)(void*),const char*,uint32_t,void* arg,int,void*,int){
  if(!mockTaskOK)return 0;
  mockTask=fn;mockTaskArg=arg;return pdPASS;
}
inline void vTaskDelete(void*){}
