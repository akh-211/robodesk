# Phone navigation, notifications and calm autonomous character

Selected defaults: Google Maps Android turn/distance/ETA on the existing OLED;
Android bridge and iPhone ANCS notifications; active but calm character.
HawkFi is explicitly deferred and has no implementation in this workstream.

## Software implementation

- [x] NAV-01: transient NAV1 packet, strict field/bounds validation, one latest instruction.
- [x] NAV-02: encrypted characteristic 0004, trusted phone checks, separate navigation state.
- [x] NAV-03: C3/S3 capability negotiation and bounded RPC forwarding; old peers do not receive navigation.
- [x] NAV-04: Google Maps English/Indonesian notification parser; unknown direction/distance remain unknown.
- [x] NAV-05: OLED arrow/distance/road/ETA; old-data indicator at 30 seconds, expiry at 120 seconds, arrival at 10 seconds.
- [x] PHONE-01: keyed notification replacement/removal; six RAM items, five-minute lifetime and allowlist filtering.
- [x] PHONE-02: Android serialized BLE operations, reconnect, scan/operation timeout, current permission/status UI.
- [x] PHONE-03: iPhone ANCS GATT client over an encrypted bonded connection; resolved identity enrollment, service discovery/change, attribute stream reassembly.
- [x] PHONE-04: transient OLED/dashboard notifications; optional per-app Gemini readout with tools/transcripts/memory blocked, no microphone capture during readout.
- [x] CHAR-01: retain seven existing activities and add pixel_doodle, watch_room, touch_play, rhythm_improv, focus_company, calm_breathing.
- [x] CHAR-02: activity expression variants, history/preferences, adjustable intensity/cooldown, rhythm variation and touch-game invitation/feedback.
- [x] CHAR-03: pause/resume activities for phone overlays; quiet hours, privacy and existing safety gates; shared 30-minute proactive greeting cooldown.
- [x] UI-01: phone source, navigation toggle, readout allowlist, intensity, live navigation/ANCS status and thirteen activity choices.
- [x] COMPAT-01: keep legacy notification characteristics; additive optional behavior fields; read original settings-backup prefix.

## Software qualification

- [x] Host protocol tests: invalid/oversize packets, stale/wrapped time, keyed updates/removal, fragmented/mismatched ANCS responses.
- [x] Existing host regression suites and dual-board migration/security contracts.
- [x] Final S3/C3 firmware builds and OTA size margins (ESP32 Arduino 3.3.11, pinned LivingEyes).
- [x] Android Maps parser scenarios and Android debug APK build (JDK 17, Gradle 8.11.1; version 1.1.0).
- [x] Independent review and resolution of material findings: persist peer removal before unpairing; retain Android removal packets during disconnect. Six-slot capacity confirmed by regression test.

### Verification evidence (2026-10-05)

- `tests/run_host_tests.ps1`: passed, including full queue capacity, notification deduplication lifetime, navigation unknown-distance/expiry, activity 13 and optional schema-1 settings compatibility. Log: `.verification/phone-character-host.log`.
- `tools/test_android_parser.ps1`: parser scenarios passed; final executable rerun log: `.verification/phone-character-parser.log`.
- Android `assembleDebug --offline --no-daemon`: passed. Compatibility branches for older Android GATT APIs emit deprecation warnings. Log: `.verification/phone-character-android.log`.
- `tools/build_dual_board.ps1 -Board both`: passed. C3 image 1,863,760 bytes / 1,966,080-byte slot, 102,320-byte margin; S3 image 2,078,256 bytes / 3,145,728-byte slot, 1,067,472-byte margin. Both exceed the build gate's 64 KiB minimum margin. Logs: `.verification/dual-board/{gateway,robot}/compile.log`.
- Static globals: C3 68,688 bytes; S3 118,912 bytes. These are compiler figures, not runtime free heap measurements. Concurrent BLE/Wi-Fi/ANCS/audio heap qualification remains unchecked below.
- Installable development APK: `.verification/phone-character/RoboDeskPhoneBridge-1.1.0-debug.apk` (852,049 bytes). No production release, robot flash or physical-phone validation is implied by these results.

## Device qualification — execute after all software work

No robot/phone pair is available for this stage; unchecked boxes are not claimed as passed.

- [ ] Flash the final S3/C3 pair over USB and verify UART negotiation, dashboard and status JSON.
- [ ] Android permission denial, enrollment/approval, unauthorized peer rejection and reconnect.
- [ ] Real Google Maps in Indonesian and English: left/right/straight/U-turn/roundabout, rerouting, arrival, unsupported format and GPS loss.
- [ ] Verify OLED arrows/text and return to clean character frames; unknown data must not show a guessed turn/distance.
- [ ] Stop Maps, disconnect HP/UART, reboot either board: stale at 30 seconds and expiry at 120 seconds.
- [ ] iPhone: select ANCS source, pair/authorize notifications, approve resolved identity, reconnect after address rotation, notification add/update/remove and permission revocation.
- [ ] Confirm iPhone navigation remains unavailable and Android forwarding cannot bypass selected iPhone mode.
- [ ] Verify app allowlists, queue overflow/TTL/removal, quiet hours and readout opt-in; no notification content in local transcripts/NVS/LittleFS.
- [ ] Start/pause/resume/cancel every activity; exercise touch invitation then tap/hold/tap; check preferences, no-repeat and overlay arbitration.
- [ ] Run BLE/Wi-Fi/dashboard/Gemini/audio together for two hours. Record internal free heap, minimum and largest block; target C3 free >=48 KiB and largest block >=24 KiB with no reset/brownout.
- [ ] Exercise OTA while phone traffic is active and confirm paired update/rollback/reconnect.

## Interfaces and limitations

Android service UUID remains `93de0001-2c7d-4a52-9f1c-6f4b4f424c45`.
0002 accepts the original four LF-delimited fields; 0003 returns the allowlist.
0004 accepts navigation, 0005 returns `1\nandroid|iphone\n0|1` phone/navigation configuration,
and 0006 accepts `key\nappId\nappLabel\ntitle\nbody` or `REMOVE\nappId\nkey`.
All characteristics require an encrypted trusted peer. Configuration/source changes take effect after the existing settings reboot.

NAV1 has six LF-delimited fields: version, state, turn, distance in meters (or `?`),
road (48 UTF-8 bytes), ETA (24 bytes). State is active/rerouting/arrived/ended;
turn is left/right/straight/uturn/slight_left/slight_right/roundabout/unknown.
The packet is capped at 180 bytes. The C3 queries `/_phone/capabilities` before
using `/_phone/navigation`; notification updates/removal use `/_phone/push`.

Maps notifications are an application-dependent text source, not a supported
Google routing API. Changes in Maps formatting require device verification and
may yield unknown instructions. iPhone ANCS firmware is implemented but not
hardware-qualified. ANCS supplies notifications, not a general Maps route feed.
Readout requires a ready Gemini session and sends the allowed snippet to Google;
it never wakes the microphone and the session is discarded after readout.
