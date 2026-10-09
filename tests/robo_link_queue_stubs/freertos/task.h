#pragma once
#include "freertos/FreeRTOS.h"
inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t){return 0;}
inline void vTaskDelay(uint32_t){}
inline void vTaskDelete(void*){}
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,int,TaskHandle_t*handle,int){if(!mockTaskOK)return 0;*handle=reinterpret_cast<void*>(uintptr_t(1));return pdPASS;}
