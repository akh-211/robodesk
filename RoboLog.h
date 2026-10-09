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
  TaskHandle_t task_=nullptr;
  std::atomic<uint32_t> dropped_{0},lineTruncations_{0},maxLineBytes_{0},maxQueueDepth_{0};
  std::atomic<uint32_t> queueDepth_{0};
  static void run(void* arg){auto*self=static_cast<RoboLogger*>(arg);Line line;for(;;){if(xQueueReceive(self->queue_,&line,portMAX_DELAY)!=pdTRUE)continue;self->queueDepth_.fetch_sub(1,std::memory_order_relaxed);size_t n=strlen(line.text),offset=0;uint32_t start=millis();while(offset<n&&uint32_t(millis()-start)<20u){int available=Serial.availableForWrite();if(available>0){size_t k=n-offset;if(k>size_t(available))k=size_t(available);offset+=Serial.write(reinterpret_cast<const uint8_t*>(line.text)+offset,k);}else vTaskDelay(1);}if(offset<n)++self->dropped_;}}
  static void recordMax(std::atomic<uint32_t>& target,uint32_t value){uint32_t old=target.load(std::memory_order_relaxed);while(value>old&&!target.compare_exchange_weak(old,value,std::memory_order_relaxed)){} }
 public:
  void begin(){if(queue_)return;queue_=xQueueCreate(12,sizeof(Line));if(!queue_)return;if(xTaskCreatePinnedToCore(run,"rdLog",4096,this,1,&task_,0)!=pdPASS){vQueueDelete(queue_);queue_=nullptr;task_=nullptr;}}
  uint32_t dropped()const{return dropped_.load();}
  uint32_t lineTruncations()const{return lineTruncations_.load();}
  uint32_t maxLineBytes()const{return maxLineBytes_.load();}
  uint32_t maxQueueDepth()const{return maxQueueDepth_.load();}
  uint32_t stackHighWaterBytes()const{return task_?uint32_t(uxTaskGetStackHighWaterMark(task_)):0u;}
  void printf(const char*fmt,...){if(!queue_){++dropped_;return;}Line line;va_list ap;va_start(ap,fmt);int required=vsnprintf(line.text,sizeof(line.text),fmt,ap);va_end(ap);if(required<0){++dropped_;return;}recordMax(maxLineBytes_,uint32_t(required));if(size_t(required)>=sizeof(line.text))++lineTruncations_;uint32_t depth=queueDepth_.fetch_add(1,std::memory_order_relaxed)+1u;if(depth>12u)depth=12u;recordMax(maxQueueDepth_,depth);if(xQueueSend(queue_,&line,0)!=pdTRUE){queueDepth_.fetch_sub(1,std::memory_order_relaxed);++dropped_;}}
  void println(const char*s){printf("%s\n",s?s:"");}
};
inline RoboLogger RoboLog;
#else
#define RoboLog Serial
#endif
