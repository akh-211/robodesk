# RoboDesk Android App: security and feature roadmap

**Status: proposal for user review. No APK, Android source, or firmware changes are part of this document.**

Related UI concept: [offline 22-screen mockup](android-mockup/index.html), with a [12-screen visual overview](android-mockup/overview.png). The mockup uses synthetic data and does not connect to Android or a robot.

## Goal

Make RoboDesk Bridge a clear, privacy-first Android companion app for one trusted robot. The app should make it obvious what phone data is shared, which robot is trusted, and whether each action reached the robot. Existing firmware-side allowlists and safety gates remain authoritative.

This roadmap is for review. Implementation should start only after the user approves the scope and priority order.

## Baseline already present in the project

The repository's current implementation notes and Android source describe these capabilities as implemented. That does not replace physical-device validation:

- Robot selection and authenticated enrollment, mutual session authentication, replay/age checks, BLE framing, ACK handling, and robot forget/revoke.
- Keystore-protected pairing credentials and backup exclusions for trust data.
- Per-app notification modes, conservative defaults, lock-screen downgrade, text redaction, local queue controls, and notification/navigation pause behavior.
- Google Maps notification-derived directions and a bounded set of authenticated companion controls.
- A functional five-destination native UI: Home, Apps, Navigation, Robot, and More.

The clearest product gap is that the native UI remains much smaller than the proposed 22-screen mockup. Physical behavior and the release signing path are also not confirmed. Treat the security protocol and privacy state machine as **implemented but awaiting device/release validation**, not as greenfield features.

## Current product boundary

- Android can forward selected notifications through Android's notification access service, after the user enables that system permission and selects apps/content modes.
- Google Maps guidance is derived from notifications currently visible to Android; this is not a general-purpose Google Maps route API or background location tracker.
- Android companion controls may send only the bounded, authenticated commands already supported by the robot protocol.
- The app does not need an account, analytics, advertising, or an app server for its phone-to-robot bridge.
- iPhone notification forwarding is a separate firmware/Apple ANCS path. A generic Android-style iOS app cannot read arbitrary third-party notifications; do not present the Android APK as an iPhone bridge.
- The mockup is a proposed information architecture. Native screens and Android system dialogs still need implementation and device validation.

## Threats the design must cover

| Threat | Desired protection |
|---|---|
| Nearby unauthorized device or impersonated robot | Explicit robot selection, authenticated pairing, identity confirmation, one active trusted robot, clear forget/revoke flow |
| Replayed, altered, or cross-session BLE commands | Versioned authenticated messages, fresh session material, replay counters, bounded age, reject-on-error behavior |
| Notification disclosure from lock screen, removed app, or changed privacy mode | Conservative defaults, lock-screen masking, immediate queue purge, robot-side app allowlist, purge ACK before forwarding resumes |
| Credential leakage through backup, logs, crash reports, or app storage | Android Keystore-backed secrets, backup exclusions for credentials, no secrets in logs/clipboard/export, safe revocation |
| Misleading connection or command status | Show paired, authenticated, ready, queued, delivered, rejected, and unknown outcomes as separate states |
| Battery drain or resource exhaustion from reconnects and queue growth | Bounded queues, TTL, exponential backoff, throttled diagnostics, lifecycle-aware BLE work |
| Malicious or malformed BLE input | Strict lengths/schema/version checks, bounded fragment assembly, parser tests, no dynamic unbounded allocation |

## Proposed implementation stages

### Stage 0 — baseline and decisions

1. Record the exact Android package/version, supported Android range, firmware protocol versions, permissions, signing state, and current app screens.
2. Turn the mockup into a screen-to-capability map: each control must map to an existing API/protocol operation or be marked “future”.
3. Confirm product decisions: one trusted robot at a time; selected apps start off; notification content is hidden while the phone is locked; local-only bridge remains the default; Maps forwarding starts off.
4. Write a concise data inventory: notification fields, robot identity, BLE metadata, credentials, diagnostics, retention, and where each value lives.

**Exit criteria:** no UI claim implies an unimplemented robot or phone capability; data flows and permission purposes are documented.

### Stage 1 — security verification and remaining hardening (P0)

1. Audit the existing pairing and authenticated-session path against the threats in this plan; fix only concrete gaps found in source review or tests.
2. Add or retain regression coverage for wrong robot, wrong code, replay, expired challenge, disconnect during enrollment, lost bond, malformed/oversized packets, protocol downgrade, and unsupported firmware.
3. Confirm every transition to lock, pause, forget, permission revoke, and app-mode downgrade invalidates queued content and transient session state safely.
4. Verify Android and firmware expose distinct discovery, bond, robot approval, mutual-authentication, and ready states. No UI may call a peer “trusted” merely because GATT connected.
5. Review protocol version compatibility and explicit recovery when the phone APK and C3 firmware differ.

**Acceptance checks:** software regressions prove fail-closed behavior and accurate UI states. Physical pairing/OLED and Android-version checks remain scheduled in the final device gate.

### Stage 2 — privacy verification and remaining gaps (P0)

1. Verify the existing **Off-by-default** app modes, lock-screen downgrade, redaction, allowlist intersection, queue expiry, and pause/revoke/forget purge behavior.
2. Specifically test stale robot cache entries that Android no longer knows about, app-mode downgrade while offline, purge ACK loss, process death during purge, and reconnect. Remain fail-closed until removal is confirmed.
3. Audit whether notification previews, exceptions, debug logs, crash reports, clipboard, Android backup/device transfer, and diagnostics can expose content or credentials.
4. Confirm the manifest requests no location, contacts, SMS, or call-log access for the current Maps-notification workflow. Keep Maps forwarding opt-in.
5. Review cloud/AI integration boundaries and ensure no app permission or UI implies that notification text is processed locally if a separate robot/dashboard cloud path receives it.
6. Keep any newly proposed notification history off by default; do not add retention until a use case and deletion policy are approved.

**Acceptance checks:** privacy regression tests and source inspection pass; final device tests prove no disallowed old content reappears after lock, offline changes, app removal, reconnect, or power loss.

### Stage 3 — native UI, onboarding, and accessibility (P1)

1. Review and then implement the 22-screen mockup as native Android screens, preserving the five main destinations: Home, Apps, Navigation, Robot, and More.
2. Use Android system permission and Bluetooth dialogs for consent. Explain a permission immediately before requesting it; provide a path to system settings after denial.
3. Show separate connection stages and honest action outcomes, including offline, authentication failure, pending purge, queue full, command rejected, and delivery unknown.
4. Add loading, empty, stale, and error states to every data-driven screen. Never fill a failed request with plausible-looking live data.
5. Add translated string resources, TalkBack labels, scalable text, contrast checks, large touch targets, keyboard/switch access, dark/light theme, and reduced-motion support.
6. Keep dangerous actions (forget, revoke, clear) explicit and reversible where possible; state which robot/data will be affected.

**Acceptance checks:** accessibility review with TalkBack and large fonts; UI tests for permission denial, back navigation, offline transitions, and all empty/error states; no network/Bluetooth action from the design-only prototype.

### Stage 4 — BLE reliability, lifecycle, and battery (P1)

1. Review existing queue bounds, TTL, deduplication, ACKs, retries, and reconnect policy for edge cases; add fixes only for measured gaps.
2. Do not retry non-idempotent companion commands automatically after an unknown outcome; show the user that delivery is unknown.
3. Measure whether reconnect scans or status polling drain battery, then tune capped backoff and jitter from evidence.
4. Confirm Android background execution behavior across supported API levels; use only the minimum service mode and persistent notification needed for user-visible ongoing sharing.
5. Validate process restart recovery without silently re-authorizing a robot or restoring sensitive content.
6. Review copied/shared diagnostics for redaction of secrets, message contents, and stable identifiers.

**Acceptance checks:** long idle, robot unavailable, rapid disconnect/reconnect, queue saturation, process kill, Bluetooth toggle, and phone reboot measurements; compare battery and memory against a documented baseline.

### Stage 5 — proposed companion additions (P2)

1. Keep robot character intensity, activity selection, pause/resume/cancel, and quiet-hours controls limited to the typed command allowlist.
2. Show the robot's authoritative acceptance/rejection reason and current state; the phone must not imply an activity started before ACK.
3. Keep navigation and notification forwarding independent so users can allow one without the other.
4. Consider a quick pause tile/widget only after the core privacy state machine is reliable; make it pause-only unless the user explicitly resumes in the app.
5. Consider local notification summaries/history only with a user-selected short retention period and an explicit clear control. Default to no history.
6. Do not add remote microphone activation, arbitrary shell/RPC, Wi-Fi credential changes, firmware flashing, factory reset, or free-form AI instructions through the phone bridge.

**Acceptance checks:** every command is authenticated, typed, age-limited, firmware-allowlisted, safety-gated, and represented accurately in the UI; no command can bypass robot privacy or emergency stop behavior.

### Stage 6 — release hardening and verification (P0 release gate)

1. Produce a non-debuggable release build with a separately managed signing key; document certificate fingerprint and artifact checksum without storing the key in the repository.
2. Decide migration from debug package/signature before publishing; explain whether users must reinstall and re-pair.
3. Review exported components, intent filters, permissions, backup/data extraction rules, cleartext/network policy, dependency versions, and min/target SDK behavior.
4. Run source/static checks, protocol tests, Android unit/UI tests, lint, release build verification, and a secrets scan.
5. Have an independent security review inspect pairing downgrade, credential lifecycle, notification purge, BLE parser bounds, logs, and release configuration.
6. Only after software gates pass, run the physical Android/robot matrix: enrollment, permissions, locked screen, privacy downgrade, offline revoke, reconnect, command ACKs, long idle, battery, and firmware compatibility.
7. Publish only when the app signing key and release artifacts have been reviewed and the device checks have a recorded result.

**Acceptance checks:** reproducible release checklist, reviewed artifact fingerprint/checksum, no critical security findings, complete device matrix or clearly documented supported-device limit.

## Suggested priority and review points

| Priority | Scope | Review decision |
|---|---|---|
| P0 | Pairing/session security, privacy defaults and purge, credential/backup policy, release signing | Approve before any public APK release |
| P1 | Native mockup implementation, accessibility, truthful states, BLE lifecycle and battery | Approve after P0 scope; deliver in small reviewable increments |
| P2 | Companion refinements and optional convenience features | Approve only after safe command and privacy behavior is validated |

Before implementation, review these choices:

1. Keep all app sharing off by default, including Maps? **Recommended: yes.**
2. Keep notification history disabled, with only short-lived delivery diagnostics? **Recommended: yes.**
3. Is the 22-screen mockup's five-tab navigation and feature grouping acceptable as the native-app direction?
4. Should the first release target Android only, with iOS/ANCS described as separate hardware integration? **Recommended: yes.**

## Explicitly out of scope for this review

- Editing Android Kotlin, Gradle configuration, manifests, firmware, or protocol code.
- Building/signing/publishing an APK, pairing a phone, or changing a robot.
- Enabling analytics, accounts, remote/cloud storage, notification history, continuous location, remote microphone access, or unrestricted robot commands.
