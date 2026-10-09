param(
  [string]$LibraryRoot = (Join-Path $env:USERPROFILE 'OneDrive/Documents/Arduino/libraries'),
  [string]$LivingEyesRoot,
  [switch]$AllowUnpinnedLivingEyes,
  [string]$VerificationRoot = '.verification'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($LivingEyesRoot)) {
  $LivingEyesRoot = if (![string]::IsNullOrWhiteSpace($env:ROBODESK_LIVINGEYES_ROOT)) {
    $env:ROBODESK_LIVINGEYES_ROOT
  } else {
    Join-Path $env:USERPROFILE 'OneDrive/Documents/LivingEyes-merge/LivingEyes'
  }
}
if ($AllowUnpinnedLivingEyes -and !$PSBoundParameters.ContainsKey('LivingEyesRoot')) {
  throw '-AllowUnpinnedLivingEyes requires an explicit -LivingEyesRoot; this bypass is for host migration tests only.'
}
Push-Location $root
try {
  if (!(Test-Path -LiteralPath $LivingEyesRoot -PathType Container)) { throw "LivingEyes checkout not found: $LivingEyesRoot" }
  $eyesRoot = (Resolve-Path -LiteralPath $LivingEyesRoot).Path
  $eyes = Join-Path $eyesRoot 'src'
  $json = Join-Path $LibraryRoot 'ArduinoJson/src'
  if (!(Test-Path -LiteralPath (Join-Path $eyes 'Engine.cpp') -PathType Leaf) -or
      !(Test-Path -LiteralPath (Join-Path $json 'ArduinoJson.h') -PathType Leaf)) {
    throw 'LivingEyes must contain src/Engine.cpp; set -LibraryRoot for the ArduinoJson installation.'
  }
  $fingerprint = Join-Path $root 'tools/livingeyes_fingerprint.py'
  $pin = Join-Path $root 'tools/livingeyes-pin.json'
  $fingerprintArgs = @($fingerprint, $eyesRoot)
  if (!$AllowUnpinnedLivingEyes) { $fingerprintArgs += @('--pin', $pin) }
  & python @fingerprintArgs
  if ($LASTEXITCODE -ne 0) { throw "LivingEyes fingerprint verification failed: $eyesRoot" }
  if ($AllowUnpinnedLivingEyes) { Write-Warning 'LivingEyes pin bypassed explicitly for host migration tests; releases always require the pin.' }
  Write-Host "Host tests using LivingEyes: $eyesRoot"

  $verificationPath = $VerificationRoot
  if (![System.IO.Path]::IsPathRooted($verificationPath)) { $verificationPath = Join-Path $root $verificationPath }
  New-Item -ItemType Directory -Force -Path $verificationPath | Out-Null
  $verification = (Resolve-Path -LiteralPath $verificationPath).Path
  $includes = @('-I','tests/stubs','-I','.','-isystem',$eyes,'-isystem',$json)
  $dashboardSource = Get-Content -LiteralPath (Join-Path $root 'SettingsDashboard.h') -Raw
  $firmwareSource = Get-Content -LiteralPath (Join-Path $root 'RoboDeskSonicCharacter.ino') -Raw
  $brainSource = Get-Content -LiteralPath (Join-Path $root 'RoboBrain.h') -Raw
  if (!$dashboardSource.Contains('const activity=d.activity||{};') -or
      !$dashboardSource.Contains("activity.lastOutcome==='interrupted'") -or
      !$dashboardSource.Contains('Number(activity.lastOutcomeAgeMs)<60000') -or
      !$dashboardSource.Contains("data-expression='curious'") -or
      !$dashboardSource.Contains("document.querySelectorAll('[data-expression]')") -or
      !$dashboardSource.Contains("id='faceMood'") -or
      !$firmwareSource.Contains('\"activity\":{') -or
      !$firmwareSource.Contains('if(!strcmp(action,"game_accept"))') -or
      !$firmwareSource.Contains('touchStable||!touchGame.start(now,touchGameEligible(now))') -or
      !$firmwareSource.Contains('touchGame.press(uint32_t(now-headTouchStartedAt)') -or
      !$firmwareSource.Contains('touchGameEligible(now,touchGame.active())') -or
      !$firmwareSource.Contains('companionScheduler.update(now,schedulerContext,recentIds,recentCount)') -or
      !$firmwareSource.Contains('life.startMacroSession(') -or
      !$firmwareSource.Contains('if(!strcmp(action,"activity_start"))') -or
      !$firmwareSource.Contains('if(!strcmp(action,"activity_pause"))') -or
      !$firmwareSource.Contains('presenceRitual.update(pirRaw,now,true)') -or
      !$firmwareSource.Contains('runtimeSettings.proactiveVoice&&runtimeSettings.proactiveVisual') -or
      !$firmwareSource.Contains('presenceRitual.acknowledge(now)') -or
      !$firmwareSource.Contains('schedulerContext.preferenceBias[id]=int8_t(preferenceLearner.scheduleProbability') -or
      $firmwareSource.Contains('life.setSchedule(') -or
      !$firmwareSource.Contains('int32_t(sonicConversationQuietUntil-now)<=0') -or
      !$firmwareSource.Contains('cancelRhythmCues()') -or
      !$firmwareSource.Contains('dashboardClearPreferenceState') -or
      !$dashboardSource.Contains('factoryReset_(factoryResetContext_)') -or
      !$firmwareSource.Contains('activityTransitionReasonName(companionActivity.lastTransition())') -or
      !$firmwareSource.Contains('companionActivity.outcomeNewest(i,item)') -or
      !$firmwareSource.Contains('activity_history_clear') -or
      !$firmwareSource.Contains('if(!strcmp(action,"learning_enable"))') -or
      !$firmwareSource.Contains('companionActivityName(companionActivity.activity())') -or
      !$dashboardSource.Contains("id='learningSummary'") -or
      !$dashboardSource.Contains("id='activityHistory'") -or
      !$dashboardSource.Contains("id='activityChoice'") -or
      !$dashboardSource.Contains("action('activity_start'") -or
      !$dashboardSource.Contains("action('activity_favorite')") -or
      !$firmwareSource.Contains('\"lastOutcome\":\"%s\"')) {
    throw 'Dashboard activity renderer and /api/status activity payload are out of sync.'
  }
  if (!$brainSource.Contains('cause==livingeyes::MindCause::Sulking&&mind_.moodOverlayActive(now)') -or
      $brainSource.Contains('cause==livingeyes::MindCause::Sulking&&mind_.sulking(now)')) {
    throw 'Sulking expression must use LivingEyes mood overlay lifetime.'
  }
  Write-Host 'PASS: dashboard activity status contract'
  if (!$dashboardSource.Contains("id='resourceHealth'") -or
      !$dashboardSource.Contains("id='healthInternalMinimum'") -or
      !$dashboardSource.Contains("window.roboRenderMemoryHealth") -or
      !$dashboardSource.Contains('m.internalMinimumFree') -or
      !$dashboardSource.Contains('r.speakerRingPsram') -or
      !$dashboardSource.Contains('c.audioUnderruns') -or
      !$firmwareSource.Contains('speakerDrops')) {
    throw 'Dashboard memory/audio health renderer and /api/status telemetry are out of sync.'
  }
  Write-Host 'PASS: dashboard memory/audio health contract'
  $setupAt = $firmwareSource.IndexOf('void setup(){')
  $otaSelfTestAt = $firmwareSource.IndexOf('beginOtaBootSelfTest()', $setupAt)
  $audioAllocationAt = $firmwareSource.IndexOf('speakerRing.begin()', $setupAt)
  $audioFailureAt = $firmwareSource.IndexOf('fatalStartup("AUDIO_BUFFER")', $setupAt)
  $micTaskStartAt = $firmwareSource.IndexOf('xTaskCreatePinnedToCore(micCaptureTaskMain', $setupAt)
  $otaWindowResetAt = $firmwareSource.IndexOf('resetOtaBootSelfTestWindow()', $setupAt)
  if ($setupAt -lt 0 -or $otaSelfTestAt -lt 0 -or $audioAllocationAt -lt 0 -or
      $audioFailureAt -lt 0 -or $micTaskStartAt -lt 0 -or $otaWindowResetAt -lt 0 -or
      $otaSelfTestAt -gt $audioAllocationAt -or $audioAllocationAt -gt $audioFailureAt -or
      $micTaskStartAt -gt $otaWindowResetAt) {
    throw 'OTA rollback self-test must be initialized before audio buffer allocation can fail.'
  }
  Write-Host 'PASS: OTA rollback is armed before audio allocation and readiness timing starts after task startup'
  & python (Join-Path $root 'tests/test_dual_board_contract.py')
  if ($LASTEXITCODE -ne 0) { throw 'Dual-board migration/reset ordering contract failed.' }
  Write-Host 'PASS: dual-board migration window, factory erasure ordering and gateway rollback service contract'
  & python (Join-Path $root 'tests/test_ble_profile.py')
  if ($LASTEXITCODE -ne 0) { throw 'BLE profile and diagnostic release safety checks failed.' }
  Write-Host 'PASS: BLE profile, diagnostic output separation and release rejection checks'
  # Existing upstream renderer warnings are outside this change; compile it
  # separately, while all project tests retain -Wall -Wextra -Werror.
  $objects = @()
  foreach ($source in @('Engine','Render','Presets')) {
    $object = Join-Path $verification "LivingEyes$source.o"
    & g++ -std=c++17 -w @includes -c (Join-Path $eyes "$source.cpp") -o $object
    if ($LASTEXITCODE -ne 0) { throw "LivingEyes $source failed to compile" }
    $objects += $object
  }
  foreach ($name in @('phone_bridge_test','phone_bridge_protocol_test','phone_ancs_event_support_test','phone_bridge_auth_record_test','phone_companion_command_test','robo_link_protocol_test','robo_tunnel_protocol_test','robo_behavior_wire_test','gemini_offline_test','mic_frame_test','wifi_fallback_test','offline_voice_commands_test','wake_word_command_mode_test','companion_core_test','companion_activity_test','companion_scheduler_test','life_macro_session_test','character_mind_test','presence_ritual_test','preference_learner_test','touch_game_test','snapshot_test','gemini_companion_parser_test','brain_companion_test','companion_settings_test','dashboard_security_test','dashboard_json_writer_test','ota_redirect_policy_test','ota_workflow_policy_test','tls_memory_test','companion_interaction_test')) {
    $compilerArgs = @('-std=c++17','-Wall','-Wextra','-Werror') + $includes + @("tests/$name.cpp")
    if ($name -eq 'wake_word_command_mode_test') { $compilerArgs += @('-I','tests/offline_voice_stubs') }
    if ($name -eq 'tls_memory_test') { $compilerArgs += @('-I','tests/tls_memory_stubs') }
    if ($name -in @('phone_bridge_protocol_test','phone_ancs_event_support_test')) { $compilerArgs += @('-pthread') }
    if ($name -eq 'brain_companion_test') { $compilerArgs += $objects }
    $exe = Join-Path $verification "$name.exe"
    & g++ @compilerArgs -o $exe
    if ($LASTEXITCODE -ne 0) { throw "$name failed to compile" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
  }
  foreach($chip in @('ESP32C3','ESP32S3')) {
    $exe=Join-Path $verification "robo_link_queue_$chip.exe"
    $queueArgs=@('-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',"-DCONFIG_IDF_TARGET_${chip}=1",'-DROBODESK_DUAL_BOARD=1','-I','tests/robo_link_queue_stubs')+$includes+@('tests/robo_link_queue_test.cpp','-o',$exe)
    & g++ @queueArgs
    if($LASTEXITCODE -ne 0){throw "robo_link_queue_test failed to compile for $chip"}
    & $exe
    if($LASTEXITCODE -ne 0){throw "robo_link_queue_test failed for $chip"}
  }
  foreach($chip in @('ESP32C3','ESP32S3')) {
    $runtimeExe=Join-Path $verification "robo_dual_runtime_$chip.exe"
    $runtimeArgs=@('-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',"-DCONFIG_IDF_TARGET_${chip}=1",'-DROBODESK_DUAL_BOARD=1','-I','tests/robo_dual_runtime_stubs','-I','tests/robo_link_queue_stubs')+$includes+@('tests/robo_dual_runtime_test.cpp','-o',$runtimeExe)
    & g++ @runtimeArgs
    if($LASTEXITCODE -ne 0){throw "robo_dual_runtime_test failed to compile for $chip"}
    & $runtimeExe
    if($LASTEXITCODE -ne 0){throw "robo_dual_runtime_test failed for $chip"}
  }
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools/test_android_privacy.ps1')
  if ($LASTEXITCODE -ne 0) { throw 'Android local security tests failed.' }
} finally { Pop-Location }
