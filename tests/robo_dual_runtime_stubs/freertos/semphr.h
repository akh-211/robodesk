#pragma once
#include <new>
#include <cstdint>
#include "freertos/FreeRTOS.h"

struct MockSemaphore {};
using SemaphoreHandle_t = MockSemaphore*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new(std::nothrow) MockSemaphore; }
inline int xSemaphoreTake(SemaphoreHandle_t handle, uint32_t) { return handle ? 1 : 0; }
inline int xSemaphoreGive(SemaphoreHandle_t handle) { return handle ? 1 : 0; }
inline void vSemaphoreDelete(SemaphoreHandle_t handle) { delete handle; }
