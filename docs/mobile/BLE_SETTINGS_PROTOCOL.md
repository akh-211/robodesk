# BLE Settings Protocol v1

Contract between the C3 gateway firmware and the Android app for reading and
writing robot settings, secrets, status and device commands over BLE. This is
the source of truth for the Android implementation; the firmware codec is
`PhoneSettingsProtocol.h`, the shared vectors are
`tests/fixtures/ble_settings_v1.txt`.

Status: firmware source and host tests only. Not yet verified on a device or
with the Android app. The web dashboard is unchanged and still uses the admin PIN.

## 1. Transport (unchanged, reused)

- Owner: C3 gateway (`ROBODESK_PHONE_BLE_ROBOT=0`). In the S3-owner build the
  kinds below are answered `Unsupported` (12).
- GATT service `93de0001-2c7d-4a52-9f1c-6f4b4f424c45`.
  - Requests: write to envelope characteristic `93de0009-…`.
  - Responses: notifications on ack characteristic `93de000a-…` (same channel
    as the existing `0x7f` ack and `0x7e` companion result).
- Each GATT value uses the existing 8-byte fragment header (version 1,
  flags bit0 first / bit1 last, message id u16, total length u16, offset u16;
  all little endian). Max reassembled message 600 bytes, fragment timeout 5 s.
- Each message is one authenticated v2 envelope:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | ASCII `RDB2` |
| 4 | 1 | version = 2 |
| 5 | 1 | direction: 1 phone→robot, 2 robot→phone |
| 6 | 1 | kind |
| 7 | 1 | reserved = 0 |
| 8 | 4 | session id (u32 LE) |
| 12 | 4 | counter (u32 LE, strictly increasing per direction) |
| 16 | 4 | ageMs (u32 LE) — **must be 0** for every kind in this document |
| 20 | 2 | payload length (u16 LE, ≤ 500) |
| 22 | n | payload |
| 22+n | 32 | HMAC-SHA256(sessionKey, `"RoboDesk-Bridge-Envelope-v2\0"` ‖ bytes 0..22+n) |

All integers in this document are little endian. Strings are raw bytes
(UTF-8 for SSID/password), length-prefixed, never NUL-terminated.

## 2. Authentication rules

1. Every kind in this document requires the existing bonded + HMAC session:
   LE Secure Connections bond with the approved peer (trusted connection),
   completed v2 mutual-proof handshake (control characteristic), and an
   envelope that verifies under that session's key, session id and replay
   counter. Android platform only (`phonePlatform == 0`).
2. Anything else is **dropped silently**: no response can be authenticated
   without a session. The app treats "no response" as a timeout.
3. No admin PIN is sent over BLE. The bonded session replaces it for BLE;
   the dashboard keeps using the PIN.
4. A replayed counter (≤ last accepted) gets the generic `0x7f` ack with
   status 1 / reason 2, never a settings response.
5. `SecretSet` values are write-only. No kind ever returns a Wi-Fi password,
   SSID or Gemini key; secret responses have no body.
6. A response is bound to the session that sent the request. If the session
   resets (disconnect, revoke, new handshake) before the response is sent,
   the response is discarded and the exchange buffer is wiped.

## 3. Kinds

| Kind | Dir | Name | Request payload |
|---|---|---|---|
| 8 (0x08) | phone→robot | SettingsGet | `page u8` |
| 9 (0x09) | phone→robot | SettingsSet | page header + page data (§5) |
| 10 (0x0a) | phone→robot | SecretSet | §6 |
| 11 (0x0b) | phone→robot | StatusSnapshot | `page u8` (must be 0) |
| 12 (0x0c) | phone→robot | Command | §8 |
| 13 (0x0d) | phone→robot | MemoryList | §14.2 |
| 14 (0x0e) | phone→robot | MemoryAdd | §14.3 |
| 15 (0x0f) | phone→robot | MemoryDelete | §14.4 |
| 16 (0x10) | phone→robot | MemoryClear | §14.5 |
| 17 (0x11) | phone→robot | ReminderList | §14.6 |
| 18 (0x12) | phone→robot | ReminderAdd | §14.7 |
| 19 (0x13) | phone→robot | ReminderDelete | §14.8 |
| 20 (0x14) | phone→robot | IrProfileList | §16 |
| 21 (0x15) | phone→robot | IntegrationsGet | §17.1 |
| 22 (0x16) | phone→robot | IntegrationsSet | §17.2 |
| 23 (0x17) | phone→robot | IntegrationsAction | §17.3 |
| 24 (0x18) | phone→robot | IntegrationsStatus | §17.4 |
| 125 (0x7d) | robot→phone | SettingsResponse | §4 |

Kinds 1–7, `0x7e` and `0x7f` keep their existing meaning. Android receivers
must register max age 0 for kind `0x7d`. Kinds 8–19 all use the same
authentication (§2), response (§4), status codes (§4.1) and paging (§5). Kinds 20..24 do too (the authorization gate `admitted` is checked for every request kind 8..24 and live op 4..13 alike).

## 4. Response (kind 0x7d)

Every request that passes authentication gets exactly one response (unless
the session ends first or the single outbound slot is busy; see §10).

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | request id = counter of the request envelope (u32) |
| 4 | 1 | request kind (8..24) |
| 5 | 1 | status (§4.1) |
| 6 | n | body (kind-specific, may be empty) |

Header + body ≤ 500 bytes.

### 4.1 Status codes

| Code | Name | Meaning |
|---|---|---|
| 0 | Ok | Success (see per-kind body) |
| 1 | Busy | Another settings request is in flight, or a queue is full. Retry later. |
| 2 | BadRequest | Payload shape/length/bounds invalid |
| 3 | NotAuthorized | Session ended while the request was being processed (normally never delivered) |
| 4 | RobotUnavailable | S3 link down or no reply within the timeout |
| 5 | SequenceError | SettingsSet page out of order, header changed, wrong session, or stage expired; restart at page 0 |
| 6 | CrcMismatch | All pages received but length/CRC32 did not match; restart at page 0 |
| 7 | InvalidSettings | Document is not valid JSON or fails `roboBehaviorImport` (schema/bounds) |
| 8 | StorageFailed | NVS save failed |
| 9 | NotImplemented | Recognised but not implemented (ota-start on firmware before StatusSnapshot schema 2) |
| 10 | ConfirmRequired | Factory reset confirm token missing or wrong |
| 11 | UpdateBusy | A firmware update is running |
| 12 | Unsupported | Unknown kind/op, or settings handler not present (S3-owner build) |
| 13 | PageOutOfRange | Requested page ≥ page count (Get page index must be < 9) |
| 14 | Forbidden | Document changes `phonePlatform` (dashboard-only: it revokes phone trust) |
| 15 | TooLarge | Exported document exceeds 4096 bytes |
| 16 | NoMemory | Robot could not allocate its document buffer |
| 17 | ContentRejected | Text refused by the robot's local privacy filter (credentials, health, long digit runs, instruction-like phrases, …). The filter is conservative, not a guarantee. |
| 18 | Full | Memory store has 96 facts and the key is new, or all 8 reminder slots are used. Nothing is evicted. |
| 19 | NotFound | No fact / reminder with that id |
| 20 | Disabled | AI memory is switched off in the robot settings |
| 21 | TimeNotSet | Robot clock is not synchronised; `once`/`daily` reminders need wall-clock time; ota-start needs it for TLS |
| 22 | NetworkUnavailable | ota-start: the C3 station is not connected to Wi-Fi |
| 23 | Rejected | Live command (§8.2) understood but refused in the current robot state (busy, cooling down, nothing to act on, wake/IR/audio not ready). Not a protocol error: do not retry blindly; read StatusSnapshot. |

## 5. Paged documents (SettingsGet / SettingsSet / StatusSnapshot)

Page header (8 bytes), followed by page data:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | page index (0-based) |
| 1 | 1 | page count = ceil(totalLength / 480) |
| 2 | 2 | totalLength (u16), 1..4096 |
| 4 | 4 | CRC-32 of the whole document (IEEE 802.3 / zlib / `java.util.zip.CRC32`: reflected poly 0xEDB88320, init 0xFFFFFFFF, final xor 0xFFFFFFFF) |
| 8 | n | data: bytes `[index*480, min(totalLength, (index+1)*480))` |

Limits: page data 480 bytes; document ≤ 4096 bytes, so page count ≤ 9. Data
length must be exactly the size implied by index and totalLength (only the
last page is shorter). Page 0 always reports the full header.

### 5.1 SettingsGet (kind 8)

Request: `page u8` (0..8). Response body: page header + data of page `page`
of the behavior JSON produced by `roboBehaviorExport` on the S3 (same
document as `/_settings/get`; `schema: 1`; never contains Wi-Fi, Gemini key,
admin PIN or phone peer). Typical size 1.3–2 KB (3–5 pages).

Read loop: request page 0, then 1..count-1. If any page reports a different
totalLength or CRC than page 0, the settings changed mid-read: restart at
page 0. After the last page, verify CRC-32 over the concatenation.
Errors: `PageOutOfRange`, `RobotUnavailable`, `UpdateBusy`, `TooLarge`, `NoMemory`, `Busy`.

### 5.2 SettingsSet (kind 9)

Request: page header + data. Send pages 0..count-1 strictly in order, each
after the previous response. The S3 stages pages in RAM; page 0 always
starts a new stage (discarding any previous one). Every page must repeat the
same totalLength, page count and CRC. A stage is bound to the BLE session id
and expires 30 s after the last accepted page.

Only when the last page arrives and length + CRC verify, the S3 parses the
whole document and applies it atomically with `roboBehaviorImport` semantics
(all fields validated first, nothing changes on failure), preserving
pomodoro runtime flags and the phone peer, then saves to NVS. The document
must be a complete schema-1 export (optional fields may be omitted and keep
their current value, exactly as `roboBehaviorImport`). Changing
`phonePlatform` is rejected with `Forbidden`.

Response body (present whenever the S3 answered, i.e. not for C3-side
`Busy`/`BadRequest`/`RobotUnavailable`/`UpdateBusy`): `page u8` (echo),
`applied u8` (0 = staged, more pages expected or error; 1 = all pages verified
and settings saved). Any non-Ok status discards the stage.

After `applied = 1` the S3 restarts about 1 s later (same as the dashboard
apply), so the link drops briefly and the C3 resyncs its mirror. If the final
response is lost, re-read with SettingsGet and compare the CRC before
re-sending.

## 6. SecretSet (kind 10)

Write-only, handled on the C3, saved to C3 NVS. Response: status only, empty
body. Never logged; request buffers are wiped after use.

Type 1 — Wi-Fi slot:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | type = 1 |
| 1 | 1 | slot 0..2 (0 = primary, 1, 2 = fallbacks) |
| 2 | 1 | flags: bit0 = keep existing password (other bits 0) |
| 3 | 1 | ssidLength 0..32 |
| 4 | s | ssid |
| 4+s | 1 | passwordLength 0..64 |
| 5+s | p | password |

Rules: total length must be exactly `5 + s + p`; bytes < 0x20 and 0x7f are
rejected; `ssidLength = 0` clears the slot (password length and flags must be
0); keep-password requires `ssidLength > 0` and `passwordLength = 0`;
`passwordLength = 0` without keep sets an open network. New credentials are
used on the next Wi-Fi (re)connect; send Command reboot gateway to apply now.

Type 2 — Gemini API key:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | type = 2 |
| 1 | 1 | keyLength 0..96 (0 clears the key) |
| 2 | k | key, alphabet `A-Z a-z 0-9 - _ .` |

Total length must be exactly `2 + k`. The C3 forwards the key to the S3 over
UART RAM-only as before. Admin PIN is not settable over BLE.

Errors: `BadRequest`, `StorageFailed`.

## 7. StatusSnapshot (kind 11)

Request: `page u8` = 0. Response body: page header (page 0 of 1) + 76-byte
snapshot, schema 3 (schema 1 was the first 58 bytes, schema 2 the first 65).
Fields that need the S3 are 0 when `linkFlags.bit1` is 0.
No secrets, no transcripts.

Version fields (GAP-H): each firmware-version u32 is the signed release number (monotonic integer, e.g. `6`), NOT semver. Little-endian; `0` = unknown. UI shows `v<N>` (`formatFirmwareVersion`); never parse as major.minor.patch. Update check compares numerically with the GitHub manifest `version`.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | schema = 3 |
| 1 | 4 | C3 firmware version (signed image version) |
| 5 | 4 | S3 firmware version |
| 9 | 4 | C3 uptime ms |
| 13 | 4 | S3 uptime ms |
| 17 | 4 | C3 heap free |
| 21 | 4 | C3 heap minimum free |
| 25 | 4 | C3 largest internal free block |
| 29 | 4 | S3 internal heap free |
| 33 | 4 | S3 internal heap minimum free |
| 37 | 1 | linkFlags: bit0 UART link connected, bit1 S3 fields valid |
| 38 | 4 | link age ms (since last S3 frame) |
| 42 | 4 | link retries |
| 46 | 4 | link CRC errors |
| 50 | 4 | link disconnects |
| 54 | 1 | wifiState: bit0 station connected, bit1 setup AP active |
| 55 | 1 | Wi-Fi RSSI (int8, 0 when not connected) |
| 56 | 1 | bleFlags: bit0 ready, bit1 connected, bit2 pairing window open, bit3 phone paired |
| 57 | 1 | configFlags: bit0 Gemini key configured, bit1/2/3 Wi-Fi slot 0/1/2 configured |
| 58 | 1 | otaState (§7.1) — schema 2 |
| 59 | 1 | otaError (§7.1), 0 unless otaState = 7 |
| 60 | 1 | otaPercent 0..100 of the image being downloaded (states 2, 3, 5) |
| 61 | 4 | otaTargetVersion: signed release version of the pair being installed / last result, 0 if unknown |
| 65 | 1 | wifiActiveSlot — schema 3: 0 none/unknown, 1..3 = the C3 station is connected through Wi-Fi slot 1..3 (C3) |
| 66 | 1 | pomodoroState: 0 idle, 1 focus, 2 break, 3 long break, 4 paused (S3 `RobotFeatures::Phase`) |
| 67 | 2 | pomodoroRemainingSec (u16, 0 when idle; clamped to 65535) |
| 69 | 1 | activityId: 0 none, else catalog id 1..13 |
| 70 | 1 | activityState: 0 idle, 1 running, 2 paused (also reported while the owner paused a running activity), 3 completed, 4 interrupted, 5 cancelled |
| 71 | 1 | gameState (touch game): 0 not offered, 1 offered (eligible, can be accepted), 2 active |
| 72 | 1 | learningEnabled: 0/1 (preference learning) |
| 73 | 1 | irProfileCount: stored IR profiles 0..4 |
| 74 | 1 | irLearnState: 0 idle, 1 listening (10 s window), 2 saved, 3 failed/timed out. Reset to 0 when a new learn is accepted. Asynchronous result of op 12 learn. |
| 75 | 1 | integrationBusy: 0/1, a C3 external-integration request is in progress (`roboexternal::busy()`, same flag that makes IntegrationsSet/clear answer `Busy`) |

Future schemas may append fields or pages; readers must accept a longer
document and ignore unknown trailing bytes when schema > 1. A schema-1
document is exactly 58 bytes and has no OTA fields (read them as 0); a
schema-2 document is at least 65 bytes and has no schema-3 fields (read them
as 0); a schema-3 document is at least 76 bytes. Schema 3 only appends, so a
schema-2 reader that ignores trailing bytes still reads bytes 0..64 correctly.
The S3 reports bytes 66..74 to the C3 in the tail of the LinkInfo reply (§11).

### 7.1 OTA state

| otaState | Name | Meaning |
|---|---|---|
| 0 | Idle | No update running and no stored result |
| 1 | Checking | ota-start accepted / fetching the signed S3 and C3 manifests |
| 2 | DownloadingS3 | S3 install started, preparing (quiesce, manifest) |
| 3 | FlashingS3 | S3 image streaming GitHub → C3 → UART → S3 inactive slot |
| 4 | WaitingS3Boot | S3 verified and rebooting; C3 waits up to 60 s for S3 to report the pair version |
| 5 | DownloadingC3 | C3 image downloading into the C3 inactive slot (C3 restarts at the end) |
| 6 | Done | Pair installed (S3 version confirmed; C3 running the target version after restart) |
| 7 | Failed | See otaError; the running firmware of the failing board is unchanged |
| 8 | UpToDate | Both signed manifests are not newer than the installed versions |

otaError: 0 none, 1 wifi, 2 clock, 3 robot busy / prepare timeout, 4 network
(DNS/TCP/TLS/HTTP/redirect), 5 manifest format, 6 signature, 7 image size/hash,
8 S3 rejected the image (begin/write/commit), 9 S3/C3 release version mismatch,
10 S3 boot not confirmed, 11 C3 install failed, 12 C3 did not boot the target
version, 13 S3 unavailable, 255 other. States 6/7/8 started over BLE are
stored in C3 NVS (`robodesk_pair/ota_ble`) and reported after the C3 restart
until the next ota-start. A factory reset clears them.

## 8. Command (kind 12)

| op | Name | Payload | Behavior |
|---|---|---|---|
| 1 | reboot | `01 target` — target bitmask: bit0 gateway C3, bit1 robot S3 (1..3) | Ok = scheduled: S3 restarts ~1 s later, C3 ~1.5 s later. `RobotUnavailable` if S3 bit set and link down. |
| 2 | factory-reset | `02` + 8-byte confirm token = ASCII `WIPE` ‖ session id (u32 LE) | Wrong token (or other session id) → `ConfirmRequired`; payload not exactly 9 bytes → `BadRequest`. Ok = reset request persisted (same path as dashboard `/factory`): about 5 s later the C3 erases S3 then C3 settings, phone credentials and bonds, then reboots; resumes after power loss. The BLE session ends. |
| 3 | ota-start | `03` | Ok = scheduled; see §8.1. `UpdateBusy` if an update/factory reset/dashboard S3 upload runs or an ota-start is already pending; `RobotUnavailable` if the UART link is down; `NetworkUnavailable` without Wi-Fi; `TimeNotSet` before NTP. |

Ops 4..13 are the live controls of §8.2. Any other op → `Unsupported`; wrong
length/target/argument → `BadRequest`; firmware update running or ota-start
pending → `UpdateBusy`. Response body is empty.

### 8.2 Live controls (ops 4..13)

Authorization is the same as every kind (§2): no bonded, MAC-verified,
Android session = no response. The C3 checks the shape (`BadRequest` for any
value outside the table), then forwards `session u32 ‖ payload` to the S3
(UART op `LinkCommand`, §11), which calls the same function as the dashboard:
`dashboardCompanionAction` (the companion action in the last column),
`dashboardSoundTest` (`/sound/test`) or `brain.clearExperiences`
(`/memory/experiences/clear`). **Ok = the S3 accepted the action**; it never
waits for a result. Status: `RobotUnavailable` (UART link down or no reply in 3 s),
`UpdateBusy` (firmware update), `Rejected` (S3 refused in its current state),
`BadRequest`, `Busy` (another settings request in flight).

| op | Name | Payload | Companion action |
|---|---|---|---|
| 4 | expression | `04 id` — id 0..21: happy, curious, shy, love, sad, surprised, thinking, agree, disagree, wink, laugh, sleepy, annoyed, dizzy, excited, calm, bored, proud, anxious, relieved, mischievous, lonely (id 12..21 added by PD-5.2; append-only) | `expression` (cooldown 0.7 s → `Rejected`) |
| 5 | talk | `05` | `talk` (opens the conversation window; busy/cooldown 1.5 s → `Rejected`) |
| 6 | pause/resume initiative | `06 mode` — 0 resume, 1 pause until resumed, 2 pause 15 min, 3 pause 1 h | `resume` / `pause` value 0, 900, 3600 |
| 7 | pomodoro | `07 sub` — 1 start, 2 pause, 3 resume, 4 stop | `pomodoro_start/pause/resume/stop` |
| 8 | activity | `08 sub [id]` — 1 start (+`id` 1..13, exactly 3 bytes), 2 pause, 3 resume, 4 cancel, 5 skip, 6 favorite, 7 history-clear (2 bytes) | `activity_start/pause/resume/cancel/skip/favorite/history_clear` (start needs a fresh scheduler context; skip/favorite need learning on and a running activity → else `Rejected`) |
| 9 | game | `09 sub` — 1 accept, 2 cancel | `game_accept` / `game_cancel` |
| 10 | learning | `0a sub` — 0 disable, 1 enable, 2 reset learned data | `learning_enable` off/on / `learning_reset` |
| 11 | sound-test | `0b cue` — cue 0..11: wake, happy, curious, affection, thinking, surprise, sad, sleepy, error, success, pickup, shake | `/sound/test` (`Rejected` when sound is busy or disabled) |
| 12 | ir | `0c 01 code` NEC code u32 LE (6 bytes) \| `0c 02 len name` learn \| `0c 03 len name` replay; name 1..15 bytes of `A-Za-z0-9_- ` | `ir_send` / `ir_learn` / `ir_replay` |
| 13 | experiences-clear | `0d` | `/memory/experiences/clear` (clears recent experiences only, not facts) |

Asynchronous results: `ir learn` returns `Ok` as soon as the 10 s listening
window is open; poll StatusSnapshot `irLearnState` (1 → 2 or 3) and
`irProfileCount`. Pomodoro/activity/game/learning effects are visible in the
schema-3 fields of the next StatusSnapshot. Any other `sub`/length is
`BadRequest`; nothing is queued for it.

### 8.1 ota-start (update over BLE)

ota-start runs exactly the dashboard's GitHub pair flow (`/ota/github/check`
then `/ota/github/install`, `RoboC3Gateway.h` `gatewayServiceUpdate`), with
no new download or verification code:

1. The Ok response is sent first. The C3 main loop starts the flow only after
   the response left the exchange slot and at least 1.5 s passed (5 s max wait).
2. Check: signed S3 manifest (`manifest-s3-robot.txt`) then signed C3
   manifest; both must have the same version (else error 9). The version
   floor (`roboOtaVersionEligible`) is unchanged.
3. Install S3 first: the C3 streams the release asset over UART
   (`setRemoteImage` → `/_ota/begin` / write / `/_ota/finish`). The C3
   hashes the stream and verifies the manifest signature over the digest
   (`GitHubOtaUpdate::downloadImage`); the S3 independently verifies size,
   SHA-256 and the signature (`RoboSignedImage`) before switching slot.
4. C3 waits for S3 to boot and report the target version, then installs the
   signed C3 image (same verification) and restarts.
5. BLE is stopped by `gatewayPrepareOta` before the first TLS request (the
   check itself needs the BLE heap), so the phone is disconnected for the whole
   run. On Done/UpToDate/Failed the C3 restarts after 2 s to bring BLE back;
   the result is then readable through StatusSnapshot (§7.1).

No "check only" op: a check needs the same BLE teardown (TLS heap), so its
answer would only be readable after a C3 restart. Use the dashboard check, or
ota-start (which reports UpToDate when nothing is newer).

## 9. Limits and timing

| Item | Limit |
|---|---|
| Envelope payload | ≤ 500 B (largest request 488 B, largest response 494 B) |
| Page data / header | 480 B / 8 B |
| Document | ≤ 4096 B, ≤ 9 pages |
| In-flight settings requests | 1 (global); a second gets `Busy` |
| ageMs | 0 |
| S3 reply timeout (C3) | Get 3 s, Set 8 s, Status info 1.5 s, memory/reminder lists 3 s, memory/reminder add/delete/clear 8 s |
| SettingsSet stage expiry | 30 s after the last page |
| Live command (ops 4..13) S3 reply timeout (C3) | 3 s |
| IrProfileList S3 reply timeout (C3) | 3 s |
| Integrations (kinds 21..24) | answered by the C3 itself, no UART; one 500 B exchange buffer, no new allocation |
| Suggested app timeout | 5 s (10 s for the last SettingsSet page); then retry with a new counter |

## 10. Delivery notes

- The C3 has one outbound notification slot. An immediate error response
  (`Busy`/`BadRequest`/…) is dropped if that slot is busy; the app recovers by
  timeout. Send the next request only after the previous response.
- SettingsGet pages are exported fresh per request; use the CRC rule in §5.1.

## 11. Internal C3↔S3 UART frames (not visible to the phone)

robolink v1 frame types `Settings = 21` (C3→S3) and `SettingsReply = 22`
(S3→C3), reliable, payload ≤ 512 B. Request: `id u32, op u8, data`; reply:
`id u32, op u8, status u8, body`.

| op | Request data | Reply body |
|---|---|---|
| 1 GetPage | `page u8` | page header + data |
| 2 PutPage | `stream u32` (BLE session id) + page header + data | `page u8, applied u8` |
| 3 Info | — | `version u32, uptimeMs u32, internalFree u32, internalMinFree u32` (16 B) + live tail (9 B, schema 3): `pomodoroState u8, pomodoroRemainingSec u16, activityId u8, activityState u8, gameState u8, learningEnabled u8, irProfileCount u8, irLearnState u8`. The C3 accepts a 16 B reply from an older S3 and reads the tail only when present. |
| 4 Reboot | — (id 0, fire-and-forget) | status only |
| 5 MemoryList | `session u32` + BLE payload | page header + data |
| 6 MemoryAdd | `session u32` + BLE payload | `id u32, revision u32` |
| 7 MemoryDelete | `session u32` + BLE payload | `revision u32` |
| 8 MemoryClear | `session u32` + BLE payload | `revision u32` |
| 9 ReminderList | `session u32` + BLE payload | page header + data |
| 10 ReminderAdd | `session u32` + BLE payload | `id u32, revision u32` |
| 11 ReminderDelete | `session u32` + BLE payload | `revision u32` |
| 12 Command | `session u32` + BLE Command payload (op 4..13, §8.2) | status only (`Ok` = accepted, `Rejected`, `BadRequest`, `UpdateBusy`) |
| 13 IrProfileList | `page u8` (0), like GetPage (no session) | §16 body |

Ops 5–11 are `kind - 8`; op 13 (IrProfileList, kind 20) is the one exception to `kind - 8` because op 12 is Command; op 12 carries only the live commands (reboot, factory
reset and ota-start stay on the C3). The S3 re-decodes the payload with the same
`decodeCommand` before running it. The C3 does not interpret them beyond the shape check
of §14 (`validateRequest`): it prepends the 9-byte UART header in place and
returns the reply through the same 500 B exchange buffer, so no C3 buffer was
added. The S3 re-checks the clear token against the forwarded session id.

The C3 keeps a single 500 B exchange buffer: the BLE request is copied at
offset 12, the UART header is prepended in place, and the S3 reply is written
to offset 0, where its first 6 bytes are rewritten into the BLE response
header. The 12 KB string-RPC buffer and JSON documents are not used on the C3
for these kinds.

## 12. Test vectors

Key = bytes `00 01 … 1f`, session id = `0x78563412`, ageMs = 0. The complete
set (payload and full envelope hex for every case, including the two
SettingsSet pages of a 647-byte document and its 494-byte Get response) is in
`tests/fixtures/ble_settings_v1.txt` — INI-like: `[name]` then
`direction`, `kind`, `session` (hex), `counter`, `age`, `payload`, `envelope`.
It is checked by `tests/test_ble_settings_vectors.py` (independent Python
encoder + HMAC) and `tests/phone_settings_protocol_test.cpp` (firmware codec).
A Kotlin test should parse the same file.

Selected vectors (payload / envelope):

- SettingsGet page 0, counter 1
  `00` /
  `524442320201080012345678010000000000000001000012b1ff67c7c69ca7d83dc8b7a1767e14e9495a9aa0848fe184030855de34193c`
- SecretSet Wi-Fi slot 1 `HomeNet` / `correct horse`, counter 4
  `01010007486f6d654e65740d636f727265637420686f727365` /
  `5244423202010a00123456780400000000000000190001010007486f6d654e65740d636f727265637420686f7273653e18dd507fc65de4b8a90c9ee513205a87aedc842b71dac493b2b3ccc3ab86f4`
- SecretSet Wi-Fi slot 2 clear, counter 6: `0102000000`
- SecretSet Gemini key `AIzaSyTEST-key_123`, counter 7: `021241497a615379544553542d6b65795f313233`
- StatusSnapshot, counter 8: `00`
- Command reboot both, counter 9: `0103`
- Command factory reset, counter 10: `025749504512345678`
  (envelope `5244423202010c00123456780a0000000000000009000257495045123456789fb0765858651efd3740e4f2580608721bfe5df3876a06f8173cce034ee8a53d`)
- Command ota-start, counter 11: `03`
- SettingsSet page 0 header of the 647-byte document: `00 02 8702 2543019a` (len 0x0287, CRC 0x9a014325)
- Response (robot counter 3) SettingsSet last page applied: `0300000009000101` /
  `5244423202027d0012345678030000000000000008000300000009000101ba1f424bda45a81acb04a57d36c9ee7183616b5a6c476adb68d662017f3fb2e1`
- Response SecretSet ok (robot counter 5): `040000000a00`
- Response ota-start Ok (robot counter 8): `0b0000000c00`; UpdateBusy (counter 25): `0b0000000c0b`
- Response StatusSnapshot schema 3 (robot counter 6, 76 B snapshot): `response_status` in the fixture.
  Values: c3 v20261009, s3 v20261008, uptimes 123456/120000, C3 heap 48000/30000/24000, S3 200000/150000, link flags 3 age 12 retries 4 crc 1 disconnects 2, Wi-Fi connected RSSI −61, BLE flags 0x0b, config flags 0x03, OTA state 3 FlashingS3, error 0, 42 %, target 20261010; schema 3: wifiActiveSlot 2, pomodoro focus 1234 s, activity 5 running, game offered, learning on, 2 IR profiles, IR learn listening, integrations idle.
  The same values as schema 2 (65 B) are `response_status_schema2` (counter 27) and as schema 1 (58 B) `response_status_schema1` (counter 26).
- IR / integrations (counters 36..44, responses 31..36): `ir_profile_list` `00`, `integrations_get` `00`, `integrations_set_config` (`01 01` lat f32 -6.2, lon f32 106.8, `12` `https://ha.example`, `02`, `0b sensor.temp`, `0a light.lamp`), `integrations_set_token` (`02 11 TESTTOKEN-abc_123`), `integrations_set_calendar` (`03 20 https://cal.example/feed.ics?k=1`), `integrations_set_token_clear` `0200`, `integrations_refresh` `01`, `integrations_clear` `02`, `integrations_status` `00`; responses `response_ir_profile_list` (`TV power`, `Fan`), `response_ir_profile_list_robot_unavailable`, `response_integrations_get`, `response_integrations_set_ok`, `response_integrations_busy`, `response_integrations_status`. Requests carrying the token / calendar URL exist only as SET vectors; no response vector contains them.
- Live commands (counters 22..35): `command_expression_love` `0403`, `command_talk` `05`, `command_pause_15m` `0602`, `command_resume` `0600`, `command_pomodoro_start` `0701`, `command_activity_start_5` `080105`, `command_activity_skip` `0805`, `command_game_accept` `0901`, `command_learning_on` `0a01`, `command_sound_test_shake` `0b0b`, `command_ir_send` `0c01ef10df20`, `command_ir_learn_tv` / `command_ir_replay_tv` (`0c02` / `0c03` + `08` + `TV power`), `command_experiences_clear` `0d`. Responses: `response_command_rejected`, `response_command_robot_unavailable`, `response_command_bad_request` (request counter 27, kind 12, status 23 / 4 / 2).

Regenerate after a deliberate spec change: `python tests/test_ble_settings_vectors.py --write`.

## 13. Deferred

- Android client (separate task) — build from this document and the fixture.
- OTA progress while the update runs (BLE is down; only the result after the C3 restart is readable).
- Admin PIN over BLE (intentionally absent).
- Removing the web dashboard: compile-time flag only (section 15); not a release configuration.
- Android UI for memory/reminders (phase 2 is firmware and protocol only; section 14).
- Device verification: none yet. Do not tick any device checklist from this work.

## 14. Memory facts and reminders (kinds 13–19)

The robot is the master of its facts and reminders; the phone is a view and an
editor. The C3 only validates the request shape and forwards it over UART
(§11, ops 5–11). The S3 executes it through the same code paths as the
dashboard and the Gemini tools, so the privacy filter, the slot quotas,
key de-duplication and persistence are shared (`PhoneMemoryOps.h`):

| Kind | S3 path |
|---|---|
| MemoryAdd | `RoboBrain::manualRemember` (as the dashboard owner-name path) |
| MemoryDelete | `RoboBrain::forget` (as the `forget_memory` tool) |
| MemoryClear | `RoboBrain::clearMemory` (as the dashboard "Clear AI memory") |
| ReminderAdd / ReminderDelete | `executeTool("create_reminder")` / `executeTool("cancel_reminder")` |

Text from the phone is untrusted data. Besides the length bounds below it must
be strict UTF-8 (no overlong forms, surrogates or code points above U+10FFFF),
free of C0/C1 control characters, DEL, U+2028 and U+2029, and contain at least
one non-space byte; otherwise the request gets `BadRequest`. Stored text is
only ever data: it is never executed or interpreted as an instruction, and it
is serialised with a JSON encoder (quotes and backslashes are preserved
byte-for-byte). Raw transcripts are not part of this interface.

All IDs are `u32` and non-zero. Fact IDs are assigned by the robot (highest ID + 1)
and can be reused after the newest fact is deleted; reminder IDs increase
until the store is cleared. Always identify an item by the `id` from the latest list.

### 14.1 Revision and "pull if changed"

Every list document carries a `revision` (CRC-32 of the canonical concatenated
entries, independent of paging). It changes when any listed field changes and
does not change with the passage of time (a running timer's countdown is
excluded). An empty list has revision 0. Add/delete/clear responses return the
new revision, so the phone can update its cache without re-reading when the
revision it computes locally matches.

Sync loop: request page 0; compare `revision` (document offset 1) with the
cached one; if equal, stop (one round trip); otherwise read the remaining pages,
verify the document CRC-32 of the page header (§5), and replace the cache. If
the page CRC or length differs between pages the list changed mid-read:
restart at page 0.

### 14.2 MemoryList (kind 13)

Request: `afterId u32, page u8` (page 0..8, `afterId` 0 = from the start).
Response body: page header + data of the page of the document below (§5).

Document (≤ 4096 B), little endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | schema = 1 |
| 1 | 4 | revision (covers **all** facts, not only this window) |
| 5 | 1 | total number of facts on the robot (0..96) |
| 6 | 1 | number of entries in this window |
| 7 | 1 | more: 1 if facts with a higher id do not fit this window |
| 8 | … | entries in ascending id order |

Entry (14 + keyLength + valueLength bytes, ≤ 212):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | id |
| 4 | 4 | updated: UTC epoch seconds of the last write (0 if the robot clock was not set) |
| 8 | 2 | day: local day number of the last write (days since 1970-01-01, robot timezone). The store does not track creation separately, so created = updated. |
| 10 | 1 | category 0 profile, 1 preference, 2 event, 3 routine, 4 place, 5 note |
| 11 | 1 | flags: bit0 pinned, bits1..2 source (1 explicit/phone/dashboard, 2 learned from conversation, 3 legacy) |
| 12 | 1 | keyLength 1..39 |
| 13 | 1 | valueLength 1..159 |
| 14 | k | key (UTF-8) |
| 14+k | v | value (UTF-8) |

A window holds as many whole entries as fit in 4096 B (at least 19; typically
all 96). While `more = 1`, request again with `afterId` = the last entry's id
(the revision stays the same unless facts change; a changed revision means
restart at `afterId = 0`). Errors: `PageOutOfRange` (page ≥ page count),
`BadRequest`, `RobotUnavailable`, `UpdateBusy`, `NoMemory`, `Busy`.

### 14.3 MemoryAdd (kind 14)

Request: `category u8 (0..5), keyLength u8 (1..39), valueLength u8 (1..159), key, value`;
total length exactly `3 + keyLength + valueLength`. The fact is stored pinned
with source "explicit".

- A fact with the same key is replaced in place (no duplicate). Re-sending the
  same key/value is a no-op that returns `Ok`; changing the value of an existing
  key also discards the recent episode summaries and in-RAM transcripts (the
  robot's existing correction behaviour).
- New key while 96 facts exist: `Full`; nothing is evicted.
- Key or value rejected by the privacy filter: `ContentRejected`. Memory switched
  off in robot settings: `Disabled`. Flash write failed: `StorageFailed`, and the
  previous state of that fact is restored.

Response body (status `Ok`): `id u32, revision u32`.

### 14.4 MemoryDelete (kind 15)

Request: `id u32`. Unknown id: `NotFound`. Like the `forget_memory` tool it also
discards recent episode summaries and in-RAM transcripts. Failed persistence:
`StorageFailed` and the fact is restored. Response body (`Ok`): `revision u32`.

### 14.5 MemoryClear (kind 16)

Request: 8-byte confirm token = ASCII `MCLR` ‖ session id (u32 LE) — different from
the factory-reset token. Wrong or other-session token: `ConfirmRequired`; length ≠ 8:
`BadRequest`. Identical to "Clear AI memory" on the dashboard: all facts, episode
summaries, relationship memory and the character mind/evolution are reset;
reminders and settings are untouched. `StorageFailed` means RAM was cleared but
persisting failed (the robot retries its normal save). Response body (`Ok`):
`revision u32` (0, the empty list).

### 14.6 ReminderList (kind 17)

Request: `page u8`. Document (always ≤ 8 entries, one to three pages):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | schema = 1 |
| 1 | 4 | revision (CRC-32 of the entries with the timer countdown zeroed) |
| 5 | 1 | count (0..8) |
| 6 | … | entries in ascending id order |

Entry (12 + textLength bytes, ≤ 123):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | id |
| 4 | 1 | schedule: 1 once, 2 daily, 3 timer |
| 5 | 2 | minute of day 0..1439 (daily; 0 otherwise) |
| 7 | 4 | due: once = UTC epoch seconds; daily = 0; timer = seconds remaining now (0 when elapsed) |
| 11 | 1 | textLength 1..111 |
| 12 | t | text (UTF-8) |

### 14.7 ReminderAdd (kind 18)

Request: `schedule u8, minute u16, value u32, textLength u8 (1..111), text`; total
`8 + textLength`.

| schedule | minute | value |
|---|---|---|
| 1 once | 0 | UTC epoch seconds, must be in the future |
| 2 daily | 0..1439 (local minute of day) | 0 |
| 3 timer | 0 | seconds, 1..86400 |

Any other combination: `BadRequest` (a `once` time in the past is also `BadRequest`).
`once`/`daily` with an unsynchronised robot clock: `TimeNotSet`. All 8 slots used:
`Full`. Privacy filter: `ContentRejected`. Flash write failed: `StorageFailed` and
the reminder is not kept. Reminders are not de-duplicated by the robot: if a
response is lost, list before retrying. Response body (`Ok`): `id u32, revision u32`.

### 14.8 ReminderDelete (kind 19)

Request: `id u32`. Unknown id: `NotFound`. Response body (`Ok`): `revision u32`.

### 14.9 Vectors

All cases are in `tests/fixtures/ble_settings_v1.txt` (`memory_*`, `reminder_*`,
`response_memory_*`, `response_reminder_*`), with the state described in its
header: facts `drink=kopi` (category 1, id 1) and `nama=Éric` (category 0, id 2),
both updated at epoch 1800000000, day 20833; reminders timer 600 s (id 1),
daily 08:00 (id 2) and once 1800003600 (id 3). The C++ host tests replay the
add/list/delete/clear/reminder requests through the S3 handler and compare the
response bodies byte-for-byte. Selected (payload):

- MemoryList afterId 0 page 0, counter 12: `0000000000`
- MemoryAdd preference `drink`=`kopi`, counter 13: `0105046472696e6b6b6f7069`
- MemoryAdd profile `nama`=`Éric`, counter 14: `0004056e616d61c389726963`
- MemoryDelete id 2, counter 15: `02000000`
- MemoryClear (session 0x78563412), counter 16: `4d434c5212345678`
  (envelope `524442320201100012345678100000000000000008004d434c5212345678c638707f1bfe608a5ef5a90f00c17bbec1541729951e73b13d0b394ce44cb8d6`)
- ReminderAdd timer 600 s `Minum air`, counter 18: `03000058020000094d696e756d20616972`
- ReminderAdd daily 08:00 `Sarapan`, counter 19: `02e00100000000075361726170616e`
- ReminderAdd once 1800003600 `Rapat`, counter 20: `01000010e0496b055261706174`
- Response MemoryList page 0 (request counter 12): `0c0000000d00000136005f8ed51c0145f439770202000100000000d2496b6151010305046472696e6b6b6f70690200000000d2496b6151000304056e616d61c389726963`
  (document revision 0x7739f445, 2 facts, 1 page)
- Response MemoryAdd ok (request counter 13): `0d0000000e000100000044a06883` (id 1, revision 0x8368a044)
- Response MemoryClear ok (request counter 16): `10000000100000000000`
- Response MemoryClear without token (request counter 16): `10000000100a`

### 14.10 Not in this interface

Editing a fact in place (use MemoryAdd with the same key), reminder editing
(delete + add), episode summaries, relationship/mood data, raw transcripts and
the Gemini conversation history. No data is cached on the C3.

## 16. IR profile list (kind 20)

Names of the IR profiles stored on the S3 (the same store `ir_replay` uses; max 4), so the app can offer them for replay.
Request: `page u8` = 0 (anything else `PageOutOfRange`, other lengths `BadRequest`). The C3 forwards it over UART (§11, op 13) and
returns the S3 body unchanged. Body (not paged, at most 65 B): `count u8` (0..4), then per profile `len u8` (1..15) + name
(`A-Za-z0-9_- `). The S3 lists only profiles whose name passes the same validation as learn/replay, so the app can pass any
returned name to Command op 12 (replay) as is. Treat names as display text. Errors: `RobotUnavailable` (UART down or no reply in 3 s),
`UpdateBusy`, `Busy`. The count equals `irProfileCount` of the snapshot unless a profile was learned in between.

## 17. External integrations (kinds 21..24)

Same data as the dashboard `/integrations` page, owned by the C3 (`RoboExternalIntegrations.h`). The BLE handlers call the **same**
code as the dashboard routes: `roboexternal::httpsUrl` / `validEntity` (validation), `save()` (NVS `robodesk_ext`, key `config`: no
format change), `requestRefresh()` and `busy()`. They run on the C3 main loop, answer without UART, and add no allocation (documents
are built in the 500 B exchange buffer; no change to the BLE heap guards 96/80 KiB). Strings are raw printable ASCII `0x21..0x7e`,
never NUL-terminated.

**Secrets are write-only.** The Home Assistant token (as `SecretSet`) and the calendar feed URL (a private iCalendar URL is a secret,
exactly as on the dashboard, which also returns only "configured") are never returned, logged or echoed: Get/Status carry only flags,
and `complete()` wipes the request buffer. This differs from the early proposal, which listed `calendarUrl` in the Get document.

### 17.1 IntegrationsGet (kind 21)

Request `page u8` = 0. Response body: one page (§5, page 0 of 1) of this document (at most 283 B):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | schema = 1 |
| 1 | 1 | flags: bit0 weather enabled, bit1 HA token configured, bit2 calendar URL configured |
| 2 | 4 | latitude (IEEE-754 float32 LE) |
| 6 | 4 | longitude (float32 LE) |
| 10 | 1 | haBaseLength 0..127 |
| 11 | b | haBase (https URL, as stored) |
| 11+b | 1 | entity count 0..3 |
| … | … | per entity: `len u8` 1..47 + `domain.object` |

### 17.2 IntegrationsSet (kind 22)

Single message (not paged), three types; each is applied and saved on its own, the other fields keep their value:

| type | Payload | Notes |
|---|---|---|
| 1 config | `01 flags lat f32 lon f32 baseLen base entCount [len ent]*` | flags: bit0 weather enabled (other bits 0). lat -90..90, lon -180..180 (NaN refused). base empty or an `https://` URL (no query) <= 127 B. Entities 0..3, `domain.object` letters/digits/`_`, no duplicates; the list replaces the stored one. |
| 2 token | `02 len token` | len 0..191, 0 clears. Not changed by type 1. |
| 3 calendar | `03 len url` | len 0..255, 0 clears. Empty or an `https://` URL (query allowed). |

Shape violations (length, bytes outside `0x21..0x7e`, range, unknown type/flag) are `BadRequest` before anything runs; the C3 then applies
the dashboard validators (`httpsUrl`, `validEntity`, duplicates) and answers `BadRequest` on failure with nothing saved. `Busy` while a provider
request runs (the dashboard answers 409), `StorageFailed` if the NVS write fails. On success the cached readings are cleared and a refresh is queued
(as on the dashboard). Response body empty.

### 17.3 IntegrationsAction (kind 23)

Request `sub u8`: 1 refresh, 2 clear (anything else `BadRequest`).
- refresh: `Ok` = queued (the dashboard's 202; `requestRefresh()` is safe while a request runs). Results arrive asynchronously: poll IntegrationsStatus or the snapshot `integrationBusy`.
- clear: erases all integration settings and cached values (`save(Config{})`). `Busy` while a provider request runs, `StorageFailed` on NVS failure.

### 17.4 IntegrationsStatus (kind 24)

Request `page u8` = 0. Response: one page of this document (at most 423 B; response <= 437 B):

| Field | Size | Meaning |
|---|---|---|
| schema | 1 | 1 |
| flags | 1 | b0 busy, b1 weather configured, b2 weather valid, b3 HA configured, b4 calendar configured, b5 calendar valid |
| weatherErr, haErr, calErr | 3 | roboexternal codes: 0 ok, 1 client_init, 2 response_too_large, 3 tls_or_network, 4 authorization_rejected, 5 provider_rejected, 6 http_status, 7 invalid_response, 8 waiting_for_clock |
| weather | 4+1+n | age ms (0 = never), `len u8` <= 95, text (e.g. `21.5 C, clear`) |
| entities | 1 + … | count 0..3, each: `slot u8` (index in the Get list), `valid u8`, `age ms u32`, `len u8` <= 24, state text |
| calendar | 4+1+n | age ms, `len u8` <= 219, summary text |

Texts come from the providers: untrusted display text (clients replace control characters, as for memory facts). Only entities with a valid id are listed.

### 17.5 Errors and authorization

Without a bonded, MAC-verified Android session nothing is answered (§2). `UpdateBusy` is not used (these kinds only touch C3 NVS/RAM). The S3-owner build
answers `Unsupported`. The token is the only value of the whole family that can arrive while an integration request is running: the answer is `Busy`, never queued.

## 15. Dashboard-less gateway build (`ROBODESK_NO_DASHBOARD`)

Experimental, C3 gateway only (`#error` on any other target). Default is 0 and the default build is unchanged.
Build: `powershell -NoProfile -File tools/build_dual_board.ps1 -Board gateway -NoDashboard`
(artifact `gateway-nodashboard`, separate from `gateway`; not combinable with `-BleDiagnostic`; `tools/prepare_github_release.ps1`
never passes the flag). It drops the WebServer, `/integrations` page, setup AP and mDNS, the dashboard OTA routes and the
503 heap guards. BLE, Wi-Fi STA, GitHub OTA (ota-start), the TLS tunnel, ANCS and the factory-reset state machine are kept.
`RoboDualRuntime::MessageMax` is 8208 on this C3 only: the largest remaining S3 reply is the external-fetch result
(8192 + 3-byte header + `text/plain` = 8205 B); everything else is below 4096 B or travels in the <=512 B Settings frames.

Lost with the dashboard (no BLE equivalent): phonePlatform change (`Forbidden` over BLE, section 4.1); local image upload (C3 `/ota`, S3 `/ota/s3`)
and the standalone GitHub check; the setup AP for first Wi-Fi entry (use SecretSet, section 6); `/integrations` page (weather, Home
Assistant, calendar configuration; replaced by kinds 21..24, section 17); other `/companion/action`
buttons not covered by Command ops 4..13 (section 8.2) or the companion-command ops; admin-PIN change; `ble_pair` / `ble_approve`; local reboot/status page.
The S3 `pair_migrate` USB window is S3-side and unchanged, but the C3 migration it feeds runs without any dashboard UI.

Recovery without an AP: the gateway has no physical-button or serial factory-reset path (its firmware reads no serial input;
only the S3 USB console accepts `pair_migrate`, `audio_test`, `net_diag`). The only reset is Command factory-reset (section 8)
from an already paired phone. **Open gap:** the pairing window (`openPairingWindow`) is opened only by the dashboard
`ble_pair` / `ble_approve` actions, so in this build a gateway that is unpaired, revoked or factory-reset cannot be re-paired
over BLE; recovery is a USB re-flash and NVS erase of the C3 (no script for this exists in this repository). Do not ship this build
until a dashboard-less pairing trigger is designed and device-verified.

## 18. Chat with the robot (kinds 25, 26)

The phone sends one line of text; the S3 hands it to Gemini Live as a normal user turn and the answer comes back in two ways: as
voice and/or text on the OLED (settings `chatReplyMode`) and as text the phone reads by polling. The C3 only forwards (UART ops
`LinkChatSend = 14`, `LinkChatPoll = 15`, data = BLE session u32 + payload). Wi-Fi and the Gemini key must be active: while Gemini
is not connected the S3 answers `RobotUnavailable` and nothing is queued. Text and reply live in S3 RAM only and are wiped 5
minutes after the turn ends.

| Kind | Request payload | Reply body |
|---|---|---|
| 25 `ChatSend` | `textLength u16, text` (UTF-8, 1..280 bytes, no control characters) | `turn u32` (never 0) |
| 26 `ChatPoll` | `turn u32` (0 = latest), `offset u16` (0..640) | `turn u32, state u8, fail u8, total u16, offset u16, chunkLength u8, chunk` (chunk <= 180 bytes) |

`state`: 0 idle/unknown turn, 1 queued, 2 thinking, 3 replying, 4 done, 5 failed. `fail`: 0 none, 1 send, 2 timeout, 3 offline,
4 interrupted. The reply (at most 640 bytes) is read by polling with `offset` = bytes already received until `state` is 4 or 5
and `offset == total`; a chunk may split a UTF-8 character, so reassemble bytes before decoding. One turn at a time: a second
`ChatSend` while a turn runs answers `Busy`. `ChatSend` is never retried by the phone (a lost answer is "uncertain").

Reply settings (S3 NVS `rpvoice`, `rpchat`; both exported in the settings document as `voiceReplyMode` and `chatReplyMode`,
optional on import): `0` voice, `1` text on the OLED only (Gemini's audio is received and dropped), `2` both. `voiceReplyMode`
applies to spoken turns (default 0, unchanged behaviour), `chatReplyMode` to chat turns (default 1). The master sound / speech
switches still mute voice. OLED text is paged (6 lines of 21 characters, 4.5 s per page) and wiped when it expires.
