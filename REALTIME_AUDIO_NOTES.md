# RoboDesk v0.15.1 — Realtime Audio Notes

This build changes only the direct voice transport/capture/playback architecture.
The deterministic LivingEyes Character core remains the authority for realtime
behavior and safety; Gemini remains the cognition/language layer.

## Why this build exists

Physical logs from the previous direct builds showed two independent problems:

- microphone turns could remain active for many seconds and lose capture cadence
  because I2S reads shared the Arduino loop with TLS, JSON, sensors and rendering;
- speaker service gaps reached roughly 118 ms and application-level starvation
  appeared, even though the decoded Gemini PCM itself was valid.

The new design makes audio capture and playback dedicated realtime work, while
keeping all queues fixed-capacity and bounded.

## Physical test

Use normal speaking volume. Say the same Indonesian phrase three times, for example:

`Halo RoboDesk, siapa nama kamu?`

Then inspect `INPUT`, `VAD`, `AUDIO_FLOW`, `SPEECH_PRIMED`, and `STAT`. The most
important new metrics are `micDrop`, `micGapMaxUs`, `starve`, and `spkGapMaxUs`.
