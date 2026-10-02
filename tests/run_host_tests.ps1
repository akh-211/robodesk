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
  # Existing upstream renderer warnings are outside this change; compile it
  # separately, while all project tests retain -Wall -Wextra -Werror.
  $objects = @()
  foreach ($source in @('Engine','Render','Presets')) {
    $object = Join-Path $verification "LivingEyes$source.o"
    & g++ -std=c++17 -w @includes -c (Join-Path $eyes "$source.cpp") -o $object
    if ($LASTEXITCODE -ne 0) { throw "LivingEyes $source failed to compile" }
    $objects += $object
  }
  foreach ($name in @('gemini_offline_test','mic_frame_test','wifi_fallback_test','offline_voice_commands_test','wake_word_command_mode_test','companion_core_test','snapshot_test','gemini_companion_parser_test','brain_companion_test','companion_settings_test','dashboard_security_test','ota_redirect_policy_test','ota_workflow_policy_test','tls_memory_test','companion_interaction_test')) {
    $compilerArgs = @('-std=c++17','-Wall','-Wextra','-Werror') + $includes + @("tests/$name.cpp")
    if ($name -eq 'wake_word_command_mode_test') { $compilerArgs += @('-I','tests/offline_voice_stubs') }
    if ($name -eq 'tls_memory_test') { $compilerArgs += @('-I','tests/tls_memory_stubs') }
    if ($name -eq 'brain_companion_test') { $compilerArgs += $objects }
    $exe = Join-Path $verification "$name.exe"
    & g++ @compilerArgs -o $exe
    if ($LASTEXITCODE -ne 0) { throw "$name failed to compile" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
  }
} finally { Pop-Location }
