#pragma once
#include <new>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif
template<class T> T* roboAllocate(){
#if defined(ARDUINO_ARCH_ESP32)
  void*memory=heap_caps_malloc(sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!memory)memory=heap_caps_malloc(sizeof(T),MALLOC_CAP_8BIT);
  return memory?new(memory)T():nullptr;
#else
  return new(std::nothrow)T();
#endif
}
