# RoboDesk phone bridge

## Android

`android/` contains the companion app source. Select RoboDesk in the app using Android's system companion-device chooser, grant **Nearby devices** on Android 12+, and enable **Notification access** only if you want notifications forwarded. On Android 8-11 the system chooser may require Location Services to be on, but the app does not request GPS/Location permission. Keep the robot's BLE pairing window open from its dashboard, enter the six digit code shown on the OLED in Android's system pairing dialog, then approve the candidate in the dashboard. The app reconnects only to the selected device address.

Use **Choose app privacy** to select `Off`, `App name only`, `Title only`, or `Title + redacted snippet` for each app. The local choice can narrow, but cannot expand, the robot dashboard allowlist. New apps default to `Off`; while the phone is locked, notification content is reduced to app name only. Pattern redaction is not guaranteed to recognize every secret.

The bridge uses a per-robot credential provisioned after dashboard approval, a mutual HMAC session handshake, direction-bound authenticated envelopes, monotonic replay counters, expiry checks and authenticated robot acknowledgements. Android stores the credential with Android Keystore encryption and removes it on Forget. Legacy notification writes are intentionally ignored by the firmware; both phone and robot must support the authenticated v2 characteristics. These controls compile and pass host/fake-GATT checks, but the flow has not yet passed physical phone/robot security testing or independent review.

## Google Maps Android navigation

Enable **Forward Google Maps navigation** in the app and navigation in the robot dashboard. The bridge reads the existing Google Maps ongoing notification and sends only the current instruction: arrow, distance, short road text and ETA if available. English/Indonesian patterns are supported. Maps notification formatting can change; unknown directions and missing distances remain unknown instead of being guessed. The OLED marks updates stale after 30 seconds without a phone update and ends them at 120 seconds. While connected, the app refreshes a still-active Maps instruction every 15 seconds.

The phone bridge serializes GATT reads/writes, coalesces notification updates by ID and reconnects after a disconnect. The native UI has Home, Apps, Navigation, Robot and More sections, including a global pause control. Pausing clears the local queue and asks a connected robot to clear transient notification/navigation content. Permission and connection states appear in the app. Phone battery restrictions may suspend notification access and must be included in device testing. The debug APK uses `com.robodesk.phonebridge.debug`. The release variant can be signed only when the release keystore is provided through `ROBODESK_ANDROID_KEYSTORE`, `ROBODESK_ANDROID_STORE_PASSWORD`, `ROBODESK_ANDROID_KEY_ALIAS`, and `ROBODESK_ANDROID_KEY_PASSWORD`; never commit those values or the keystore.

## iPhone

Select **iPhone ANCS** in the robot dashboard, save/reboot, and open **Forget current phone and start pairing**. Pair RoboDesk from iPhone Bluetooth settings, authorize system-notification sharing if offered, then approve its bonded identity in the authenticated robot dashboard. Add permitted bundle identifiers (for example `com.apple.MobileSMS`) to the allowlist. Switching phone source requires re-pairing; only one phone is trusted.

The C3 now implements the ANCS GATT client on its existing encrypted bonded BLE connection, with asynchronous service/characteristic/CCCD discovery, Service Changed handling, fragmented attribute parsing and notification add/update/remove. It requests title/body only after the app identifier passes the allowlist. This software path has not been validated with a physical iPhone/C3 pair. iPhone Maps navigation is unavailable; ANCS notification access is not a general route feed.

## Display and optional readout

Navigation takes the OLED ahead of ordinary phone notifications and idle activities. Existing microphone privacy/status alerts remain visible. Notifications and their dashboard history are kept only in RAM, with six entries and five-minute expiry; forwarded entries retain their original age. Content is not written to NVS/LittleFS.

The dashboard's optional voice allowlist is empty by default. Opting in authorizes sending that app's snippet to Gemini when its voice session is already ready; quiet hours, DND, microphone privacy and busy audio suppress it. The readout path blocks tool calls and microphone transmission, excludes text from local transcripts/memory, and discards the AI session after completion. It never opens a microphone wake window.

## BLE wire format

Service `93de0001-2c7d-4a52-9f1c-6f4b4f424c45`. The phone reads the allowlist from `...0003` and configuration from `...0005`. Authenticated v2 uses `...0007` for robot identity/capability, `...0008` for fragmented handshake control, `...0009` for authenticated update envelopes, and `...000A` for fragmented authenticated acknowledgements. The v2 handshake is bound to the robot ID and both nonces. Envelope contents are HMAC protected, direction-bound, session-bound, counter checked and age limited. Notification updates/removals, navigation, global pause-clear and remote revoke use typed envelope kinds; raw legacy writes to `...0002` and `...0006` are not dispatched. The robot checks the notification allowlist again and does not persist notification content.


`...0004` remains the legacy navigation characteristic and `...0006` the legacy notification write characteristic; Android no longer uses these for content transmission and the firmware intentionally ignores old payload writes. `...0003` and `...0005` remain read-only allowlist/configuration. See [the task/qualification checklist](../docs/companion/PHONE_CHARACTER_TASKS.md) for packet limits and final device testing.

Build the APK with JDK 17 and Gradle 8.11.1 (`assembleDebug`). The artifact is `android/app/build/outputs/apk/debug/app-debug.apk`; it is a development APK, not a published release. Run pure Maps parser checks with `tools/test_android_parser.ps1` from the project root.
