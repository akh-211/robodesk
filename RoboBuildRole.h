#pragma once

// C3 owns Wi-Fi and phone BLE; S3 keeps audio, sensors, and character runtime.
#ifndef ROBODESK_DUAL_BOARD
#define ROBODESK_DUAL_BOARD 0
#endif
#ifndef ROBODESK_PHONE_BLE_ROBOT
#define ROBODESK_PHONE_BLE_ROBOT 0
#endif
#if ROBODESK_DUAL_BOARD && defined(CONFIG_IDF_TARGET_ESP32C3)
#define ROBODESK_DUAL_GATEWAY 1
#define ROBODESK_DUAL_ROBOT 0
#define ROBODESK_LINK_TX 3
#define ROBODESK_LINK_RX 6
#elif ROBODESK_DUAL_BOARD && defined(CONFIG_IDF_TARGET_ESP32S3)
#define ROBODESK_DUAL_GATEWAY 0
#define ROBODESK_DUAL_ROBOT 1
#define ROBODESK_LINK_TX 17
#define ROBODESK_LINK_RX 18
#define ROBODESK_PIN_IR_RX 1
#define ROBODESK_PIN_IR_TX 2
#else
#define ROBODESK_DUAL_GATEWAY 0
#define ROBODESK_DUAL_ROBOT 0
#endif
// 1 removes the C3 web dashboard (WebServer, setup AP, integrations page, dashboard OTA routes).
// BLE, Wi-Fi STA, GitHub OTA, the TLS tunnel and the UART link are unchanged. Gateway-only; default 0.
#ifndef ROBODESK_NO_DASHBOARD
#define ROBODESK_NO_DASHBOARD 0
#endif
#if ROBODESK_NO_DASHBOARD && !ROBODESK_DUAL_GATEWAY
#error "ROBODESK_NO_DASHBOARD is supported only on the dual-board C3 gateway"
#endif

