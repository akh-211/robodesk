#pragma once

// Baseline for a fresh NVS after the initial v2 USB migration; OTA releases stage their own version.
#define ROBODESK_OTA_INITIAL_VERSION 2UL

// Public verification key. The matching private key is DPAPI-protected locally.
static const char ROBODESK_OTA_PUBLIC_KEY_PEM[] = R"ROBODESKOTA(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEuy52GlvLz+zkg73gh158+YL8tLSv
FduX4lUgnW177FeujXzUhNfcNZusCP2IGp5Dp9ZPHeqcPXGQSTuY8Iq2Dw==
-----END PUBLIC KEY-----
)ROBODESKOTA";
