#pragma once

// WakeNet uses the existing model partition; loading is checked before ESP-SR starts.
// Never substitute continuous cloud capture when a model is absent.
#ifndef ROBODESK_WAKEWORD_ENGINE_ENABLE
#define ROBODESK_WAKEWORD_ENGINE_ENABLE 1
#endif

// This label documents the wake model expected in the ESP-SR model partition.
// It is not a runtime-renamable phrase. Custom phrases require a custom WakeNet model.
#ifndef ROBODESK_WAKEWORD_LABEL
#define ROBODESK_WAKEWORD_LABEL "Hi ESP"
#endif

#ifndef ROBODESK_WAKEWORD_DEFAULT_FOLLOWUP_MS
#define ROBODESK_WAKEWORD_DEFAULT_FOLLOWUP_MS 15000u
#endif
