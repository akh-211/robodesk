#pragma once
#include "RoboBuildRole.h"

// Compile-time hardware and OTA profile for each supported RoboDesk board.
// The C3 pin assignment is the selected ESP32-C3 SuperMini wiring map.
// Peripheral initialization remains disabled until the selected wiring is installed.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define ROBODESK_BOARD_ID "esp32c3"
#define ROBODESK_BOARD_NAME "ESP32-C3"
#define ROBODESK_OTA_SIGNING_TARGET "ESP32-C3"
#define ROBODESK_APP_PARTITION_SIZE 0x1E0000u
#define ROBODESK_MIC_I2S_PORT I2S_NUM_0
#define ROBODESK_SPEAKER_I2S_PORT I2S_NUM_0
#define ROBODESK_AUDIO_TASK_CORE 0
#define ROBODESK_GPIO_MAX 21

#ifndef ROBODESK_C3_HARDWARE_READY
#define ROBODESK_C3_HARDWARE_READY 0
#endif

#ifndef ROBODESK_C3_PINMAP_CONFIRMED
#define ROBODESK_C3_PINMAP_CONFIRMED 1
#endif
#if ROBODESK_C3_HARDWARE_READY && !ROBODESK_C3_PINMAP_CONFIRMED
#error "C3 peripherals cannot be enabled before the SuperMini pin map is confirmed"
#endif

// Selected SuperMini map. Hardware remains disabled until the wiring is installed.
#define ROBODESK_PIN_SDA 0
#define ROBODESK_PIN_SCL 1
#define ROBODESK_PIN_TOUCH_HEAD 20
#define ROBODESK_PIN_TOUCH_SIDE 21
#define ROBODESK_PIN_PIR 3
#define ROBODESK_PIN_MIC_WS 4
#define ROBODESK_PIN_MIC_BCLK 5
#define ROBODESK_PIN_MIC_DATA 6
#define ROBODESK_PIN_SPK_DIN 10
#define ROBODESK_PIN_SPK_BCLK 2
#define ROBODESK_PIN_SPK_LRC 7
#define ROBODESK_PIN_BATTERY -1

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define ROBODESK_BOARD_ID "esp32s3"
#define ROBODESK_BOARD_NAME "ESP32-S3"
#define ROBODESK_OTA_SIGNING_TARGET "ESP32-S3"
#define ROBODESK_APP_PARTITION_SIZE 0x300000u
#define ROBODESK_MIC_I2S_PORT I2S_NUM_0
#define ROBODESK_SPEAKER_I2S_PORT I2S_NUM_1
#define ROBODESK_AUDIO_TASK_CORE 1
#define ROBODESK_GPIO_MAX 48
#define ROBODESK_C3_HARDWARE_READY 1
#define ROBODESK_C3_PINMAP_CONFIRMED 1
#define ROBODESK_PIN_SDA 8
#define ROBODESK_PIN_SCL 9
#define ROBODESK_PIN_TOUCH_HEAD 7
#ifndef ROBODESK_PIN_TOUCH_SIDE
#define ROBODESK_PIN_TOUCH_SIDE 16
#endif
#define ROBODESK_PIN_PIR 15
#define ROBODESK_PIN_MIC_WS 4
#define ROBODESK_PIN_MIC_BCLK 5
#define ROBODESK_PIN_MIC_DATA 6
#define ROBODESK_PIN_SPK_DIN 11
#define ROBODESK_PIN_SPK_BCLK 12
#define ROBODESK_PIN_SPK_LRC 13
#define ROBODESK_PIN_BATTERY 10
#else
#error "RoboDesk supports only ESP32-S3 and ESP32-C3 targets"
#endif

#if ROBODESK_DUAL_GATEWAY
#undef ROBODESK_BOARD_ID
#undef ROBODESK_OTA_SIGNING_TARGET
#define ROBODESK_BOARD_ID "esp32c3-gateway"
#define ROBODESK_OTA_SIGNING_TARGET "ESP32-C3/gateway-link-v1"
#define ROBODESK_OTA_MANIFEST "manifest-c3-gateway.txt"
#define ROBODESK_OTA_ASSET "RoboDesk-c3-gateway.bin"
#elif ROBODESK_DUAL_ROBOT
#undef ROBODESK_BOARD_ID
#undef ROBODESK_OTA_SIGNING_TARGET
#define ROBODESK_BOARD_ID "esp32s3-robot"
#define ROBODESK_OTA_SIGNING_TARGET "ESP32-S3/robot-link-v1"
#define ROBODESK_OTA_MANIFEST "manifest-s3-robot.txt"
#define ROBODESK_OTA_ASSET "RoboDesk-s3-robot.bin"
#endif
