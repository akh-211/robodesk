#pragma once
#include <Arduino.h>
#include <stdarg.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
class RoboLogger {
  struct Line {char text[1024];};
  QueueHandle_t queue_=nullptr;
  std::atomic<uint32_t> dropped_{0};
  static void run(void* arg){auto*self=static_cast<RoboLogger*>(arg);Line line;for(;;){if(xQueueReceive(self->queue_,&line,portMAX_DELAY)!=pdTRUE)continue;size_t n=strlen(line.text),offset=0;uint32_t start=millis();while(offset<n&&uint32_t(millis()-start)<20u){int available=Serial.availableForWrite();if(available>0){size_t k=n-offset;if(k>size_t(available))k=size_t(available);offset+=Serial.write(reinterpret_cast<const uint8_t*>(line.text)+offset,k);}else vTaskDelay(1);}if(offset<n)++self->dropped_;}}
 public:
  void begin(){queue_=xQueueCreate(12,sizeof(Line));if(!queue_)return;if(xTaskCreatePinnedToCore(run,"rdLog",4096,this,1,nullptr,0)!=pdPASS){vQueueDelete(queue_);queue_=nullptr;}}
  uint32_t dropped()const{return dropped_.load();}
  void printf(const char*fmt,...){if(!queue_){++dropped_;return;}Line line;va_list ap;va_start(ap,fmt);vsnprintf(line.text,sizeof(line.text),fmt,ap);va_end(ap);if(xQueueSend(queue_,&line,0)!=pdTRUE)++dropped_;}
  void println(const char*s){printf("%s\n",s?s:"");}
};
inline RoboLogger RoboLog;
#else
#define RoboLog Serial
#endif
