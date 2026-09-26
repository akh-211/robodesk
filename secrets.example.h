#pragma once

// Optional compile-time defaults. The preferred path is the on-device dashboard:
// - first boot without valid Wi-Fi opens RoboDesk-Setup-XXXX
// - connect to it and browse http://192.168.4.1/
// - after Wi-Fi is configured, use http://robodesk.local/
// Stored dashboard values live in NVS and override these defaults.
#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASSWORD "CHANGE_ME"
#define GEMINI_API_KEY "CHANGE_ME"

#define GEMINI_MODEL "gemini-3.8-live"
#define GEMINI_VOICE "Iapetus"

// Dashboard HTTP Basic Auth: username = admin. Change before deployment.
#define DASHBOARD_ADMIN_PIN "robodesk"

// Password for the fallback setup AP. Must be at least 8 chars for WPA2.
#define SETUP_AP_PASSWORD "robodesk123"

// 0 = verify Google with bundled GTS Root R1 certificate.
// 1 = insecure TLS for diagnostics only.
#define GEMINI_TLS_INSECURE 0
