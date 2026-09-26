#pragma once

// Wake-word support is intentionally prepared but disabled in the default build.
// Enabling this requires an ESP-SR model partition (Arduino partition esp_sr_16)
// and working PSRAM. Keep this at 0 until those prerequisites are qualified.
#ifndef ROBODESK_WAKEWORD_ENGINE_ENABLE
#define ROBODESK_WAKEWORD_ENGINE_ENABLE 0
#endif

// This label documents the wake model expected in the ESP-SR model partition.
// It is not a runtime-renamable phrase. Custom phrases require a custom WakeNet model.
#ifndef ROBODESK_WAKEWORD_LABEL
#define ROBODESK_WAKEWORD_LABEL "Hi ESP"
#endif

#ifndef ROBODESK_WAKEWORD_DEFAULT_FOLLOWUP_MS
#define ROBODESK_WAKEWORD_DEFAULT_FOLLOWUP_MS 15000u
#endif
