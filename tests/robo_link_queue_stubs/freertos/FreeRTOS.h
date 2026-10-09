#pragma once
#include <cstdint>
using TaskHandle_t=void*;
constexpr int pdPASS=1;
constexpr uint32_t portMAX_DELAY=0xffffffffu;
#define pdMS_TO_TICKS(n) (n)
