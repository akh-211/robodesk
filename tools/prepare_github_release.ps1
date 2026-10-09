param(
  [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')][string]$Repository,
  [Parameter(Mandatory = $true)][ValidateRange(1, 2147483647)][int]$Version,
  [switch]$EnableC3Peripherals,
  [switch]$DualBoard,
  [string]$ArduinoCli = (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
  [string]$LivingEyesRoot
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
$eyesRoot = (Resolve-Path -LiteralPath $LivingEyesRoot -ErrorAction Stop).Path
$libraryParent = Split-Path -Parent $eyesRoot
$fingerprint = Join-Path $root 'tools\livingeyes_fingerprint.py'
$pin = Join-Path $root 'tools\livingeyes-pin.json'
$signer = Join-Path $root 'tools\ota_signing.py'
$exampleSecrets = Join-Path $root 'secrets.example.h'
foreach ($file in @($ArduinoCli, $fingerprint, $pin, $signer, $exampleSecrets)) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required release input is missing: $file" }
}
& python $fingerprint $eyesRoot --pin $pin
if ($LASTEXITCODE -ne 0) { throw "LivingEyes checkout does not match the reviewed pin: $eyesRoot. No release was built or signed." }
Write-Host "Release bundle uses pinned LivingEyes: $eyesRoot"

# One GitHub tag is used by both board profiles' /latest OTA manifest URLs.
# Keep the old S3 state and the C3 state in sync, and honor whichever is newer.
$statePaths = @(
  (Join-Path $root '.ota-secrets\last-github-release-version.txt'),
  (Join-Path $root '.ota-secrets\last-github-release-version-esp32c3.txt')
)
$lastVersion = 2
foreach ($statePath in $statePaths) {
  if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    $storedVersion = [int](Get-Content -LiteralPath $statePath -Raw).Trim()
    if ($storedVersion -gt $lastVersion) { $lastVersion = $storedVersion }
  }
}
if ($Version -le $lastVersion) { throw "Release version must exceed the last prepared bundle version ($lastVersion)." }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$workRoot = Join-Path $env:TEMP "RoboDeskSonicCharacter-public-release-v$Version-$stamp"
$assets = Join-Path $workRoot 'assets'
New-Item -ItemType Directory -Force -Path $assets | Out-Null
$boards = @(
  [pscustomobject]@{ Id='esp32s3'; Target='ESP32-S3'; Partition='partitions\robodesk_ota_16mb.csv'; SlotSize=0x300000; Fqbn='esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom'; Asset='RoboDeskSonicCharacter.ino.bin'; Manifest='manifest.txt' },
  [pscustomobject]@{ Id='esp32c3'; Target='ESP32-C3'; Partition='partitions\robodesk_ota_4mb_c3.csv'; SlotSize=0x1E0000; Fqbn='esp32:esp32:esp32c3:FlashSize=4M,PartitionScheme=custom'; Asset='RoboDeskSonicCharacter-esp32c3.ino.bin'; Manifest='manifest-esp32c3.txt' }
)
$dualNimbleLibrary = $null

if($DualBoard){
  if($EnableC3Peripherals){throw '-DualBoard cannot be combined with -EnableC3Peripherals.'}
  $dualNimbleLibrary = & (Join-Path $root 'tools\prepare_nimble.ps1') -Destination (Join-Path $root '.verification\release-dependencies')
  $boards += @(
    [pscustomobject]@{Id='esp32s3-robot';Target='ESP32-S3/robot-link-v1';Partition='partitions\robodesk_ota_16mb.csv';SlotSize=0x300000;Fqbn='esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom';Asset='RoboDesk-s3-robot.bin';Manifest='manifest-s3-robot.txt'},
    [pscustomobject]@{Id='esp32c3-gateway';Target='ESP32-C3/gateway-link-v1';Partition='partitions\robodesk_ota_4mb_c3.csv';SlotSize=0x1E0000;Fqbn='esp32:esp32:esp32c3:FlashSize=4M,PartitionScheme=custom';Asset='RoboDesk-c3-gateway.bin';Manifest='manifest-c3-gateway.txt'}
  )
}
foreach ($board in $boards) {
  $partitionCsv = Join-Path $root $board.Partition
  if (-not (Test-Path -LiteralPath $partitionCsv -PathType Leaf)) { throw "Partition table missing for $($board.Id): $partitionCsv" }
  $stage = Join-Path $workRoot "$($board.Id)\RoboDeskSonicCharacter"
  $build = Join-Path $workRoot "$($board.Id)\build"
  New-Item -ItemType Directory -Force -Path $stage, $build | Out-Null
  & robocopy $root $stage /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD .git .ota-secrets .verification firmware-releases .codex .agents __pycache__ tests /XF secrets.h FIRMWARE_UPDATE_PLAN.md HOTFIX*.txt *.bin *.elf *.map *.sig *.pyc
  if ($LASTEXITCODE -ge 8) { throw "Could not stage the public $($board.Id) source tree (robocopy exit $LASTEXITCODE)." }
  Copy-Item -LiteralPath $exampleSecrets -Destination (Join-Path $stage 'secrets.h')
  Copy-Item -LiteralPath $partitionCsv -Destination (Join-Path $stage 'partitions.csv')

  $compileLog = Join-Path (Split-Path -Parent $build) 'arduino-cli-verbose.log'
  $sdkLibraries=Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\hardware\esp32\3.3.11\libraries'
  $compileArgs = @('compile','--verbose','--library',(Join-Path $sdkLibraries 'Network'),'--library',(Join-Path $sdkLibraries 'WebServer'),'--libraries',$libraryParent,'--fqbn',$board.Fqbn,'--build-path',$build,'--build-property',"upload.maximum_size=$($board.SlotSize)")
  $extraFlags=@()
  if ($board.Id -eq 'esp32c3' -and $EnableC3Peripherals) {
    $extraFlags += @('-DROBODESK_C3_HARDWARE_READY=1','-DROBODESK_C3_PINMAP_CONFIRMED=1')
  }
  if($board.Id -in 'esp32s3-robot','esp32c3-gateway'){
    $extraFlags += @('-DROBODESK_DUAL_BOARD=1','-DROBODESK_PHONE_BLE_ROBOT=0')
    if($board.Id -eq 'esp32c3-gateway'){
      $extraFlags += '-DROBODESK_NIMBLE_EXTERNAL=1'
      $compileArgs += @('--library',$dualNimbleLibrary)
    }
  }
  if($extraFlags.Count -gt 0){$compileArgs += @('--build-property',('compiler.cpp.extra_flags='+($extraFlags -join ' ')))}
  $compileArgs += $stage
  $previousErrorAction = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    & $ArduinoCli @compileArgs 2>&1 | Tee-Object -FilePath $compileLog
    $compileExitCode = $LASTEXITCODE
  } finally { $ErrorActionPreference = $previousErrorAction }
  if ($compileExitCode -ne 0) { throw "$($board.Id) firmware build failed (exit $compileExitCode). Log: $compileLog" }

  $buildOptions=Get-Content -LiteralPath (Join-Path $build 'build.options.json') -Raw
  if($buildOptions.Contains('ROBODESK_BLE_DIAGNOSTIC')){throw "$($board.Id) includes the BLE diagnostic profile; refusing to sign."}
  if($board.Id -in 'esp32s3-robot','esp32c3-gateway'){
    if(!$buildOptions.Contains('-DROBODESK_DUAL_BOARD=1') -or !$buildOptions.Contains('-DROBODESK_PHONE_BLE_ROBOT=0')){throw "$($board.Id) was not compiled as the C3-owned paired profile; refusing to sign."}
  }
  if($board.Id -eq 'esp32c3-gateway'){
    $linkMap=Get-Content -LiteralPath (Join-Path $build 'RoboDeskSonicCharacter.ino.map') -Raw
    if(!$buildOptions.Contains('-DROBODESK_NIMBLE_EXTERNAL=1') -or !$linkMap.Contains('NimBLE-Arduino-2.5.1') -or !$linkMap.Contains('ble_hs.c.o')){throw 'C3 release did not link the pinned external NimBLE host; refusing to sign.'}
    if($linkMap -match '(?:[\\/]|\()BLEDevice\.cpp\.o(?=\)|\s|$)' -or $linkMap -match 'libbt\.a\(ble_hs\.c'){throw 'C3 release linked an unexpected or duplicate BLE host; refusing to sign.'}
  }

  $libraryMatches = [regex]::Matches((Get-Content -LiteralPath $compileLog -Raw), '(?im)Using library LivingEyes.*?in folder:\s*([^\r\n]+)')
  if ($libraryMatches.Count -eq 0) { throw "Arduino CLI did not report which LivingEyes library it selected; refusing to sign $($board.Id). Log: $compileLog" }
  foreach ($match in $libraryMatches) {
    $reportedPath = $match.Groups[1].Value.Trim()
    if (!(Test-Path -LiteralPath $reportedPath -PathType Container)) { throw "Arduino CLI reported a missing LivingEyes path: $reportedPath" }
    $selectedPath = (Resolve-Path -LiteralPath $reportedPath).Path
    if (![string]::Equals($selectedPath, $eyesRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
      throw "Arduino CLI selected LivingEyes at $selectedPath instead of $eyesRoot; refusing to sign $($board.Id). Log: $compileLog"
    }
  }
  & python $fingerprint $eyesRoot --pin $pin
  if ($LASTEXITCODE -ne 0) { throw "LivingEyes changed during the $($board.Id) compilation; refusing to sign. Log: $compileLog" }

  $compiledPartitions = Get-Content -LiteralPath (Join-Path $build 'partitions.csv') -Raw
  if ($board.Id -like 'esp32c3*') {
    $partitionValid = $compiledPartitions -match '(?m)^app0,app,ota_0,0x10000,0x1E0000' -and $compiledPartitions -match '(?m)^app1,app,ota_1,0x1F0000,0x1E0000'
  } else {
    $partitionValid = $compiledPartitions -match '(?m)^app0,app,ota_0,0x400000,0x300000' -and $compiledPartitions -match '(?m)^app1,app,ota_1,0x700000,0x300000'
  }
  if (-not $partitionValid) { throw "Build did not use the expected two-slot partition table for $($board.Id); refusing to sign." }

  $compiledImage = Join-Path $build 'RoboDeskSonicCharacter.ino.bin'
  $image = Join-Path $assets $board.Asset
  Copy-Item -LiteralPath $compiledImage -Destination $image
  $imageBytes = (Get-Item -LiteralPath $image).Length
  $margin = [int64]$board.SlotSize - [int64]$imageBytes
  if ($margin -lt 65536) { throw "$($board.Id) image leaves only $margin bytes in its OTA slot; at least 65536 bytes are required." }
  $imageSig = "$image.sig"
  & python $signer sign $image --version $Version --board $board.Target
  if ($LASTEXITCODE -ne 0) { throw "$($board.Id) firmware signing failed; bundle manifests were not generated." }
  & python $signer verify $image $imageSig --board $board.Target
  if ($LASTEXITCODE -ne 0) { throw "$($board.Id) firmware signature verification failed; bundle manifests were not generated." }

  $digest = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
  $signature = (Get-Content -LiteralPath $imageSig -Raw).Trim()
  $manifest = @(
    'format=robodesk-ota-v1'
    "board=$($board.Id)"
    "version=$Version"
    "size=$imageBytes"
    "sha256=$digest"
    "image=$($board.Asset)"
    "signature=$signature"
    "url=https://github.com/$Repository/releases/download/v${Version}/$($board.Asset)"
  ) -join "`n"
  Set-Content -LiteralPath (Join-Path $assets $board.Manifest) -Value $manifest -Encoding ascii
  Write-Host "$($board.Id): $imageBytes bytes; OTA slot margin $margin bytes; signature verified."
}

foreach ($statePath in $statePaths) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $statePath) | Out-Null
  Set-Content -LiteralPath $statePath -Value $Version -NoNewline
}
Write-Host "Signed S3+C3 release bundle prepared: $assets"
Write-Host "Upload all bundle files to the same GitHub Release tag v${Version}:"
Get-ChildItem -LiteralPath $assets -File | Sort-Object Name | ForEach-Object { Write-Host "  $($_.FullName)" }
Write-Host 'The release builds used secrets.example.h; local secrets.h was not included.'
