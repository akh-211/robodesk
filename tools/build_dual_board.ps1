param(
  [ValidateSet('both','gateway','robot','standalone-c3','standalone-s3')][string]$Board='both',
  [string]$ArduinoCli='C:\Program Files\Arduino CLI\arduino-cli.exe',
  [string]$LivingEyesRoot=(Join-Path $env:USERPROFILE 'OneDrive\Documents\LivingEyes-merge\LivingEyes'),
  [string]$OutputRoot='.verification\dual-board',
  [switch]$BleDiagnostic,
  [ValidateSet('baseline','msys1-6')][string]$BleProfile='baseline'
)
$ErrorActionPreference='Stop'
if($BleDiagnostic -and $Board -notin @('gateway','both')){throw 'BLE diagnostic builds require the dual-board C3 gateway or both-board build.'}
if($BleProfile -ne 'baseline' -and !$BleDiagnostic){throw 'Non-baseline BLE profiles are diagnostic-only; pass -BleDiagnostic.'}
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(![IO.Path]::IsPathRooted($OutputRoot)){$OutputRoot=Join-Path $root $OutputRoot}
$output=[IO.Path]::GetFullPath($OutputRoot)
if(!$output.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Build output must stay inside this workspace.'}
& python (Join-Path $root 'tools\livingeyes_fingerprint.py') $LivingEyesRoot --pin (Join-Path $root 'tools\livingeyes-pin.json')
if($LASTEXITCODE -ne 0){throw 'LivingEyes pin mismatch.'}
$profiles=@(
  @{Id='gateway';Chip='esp32c3';Partition='robodesk_ota_4mb_c3.csv';Slot=0x1E0000;Options='FlashSize=4M,PartitionScheme=custom';Dual=1},
  @{Id='robot';Chip='esp32s3';Partition='robodesk_ota_16mb.csv';Slot=0x300000;Options='FlashSize=16M,PSRAM=opi,PartitionScheme=custom';Dual=1},
  @{Id='standalone-c3';Chip='esp32c3';Partition='robodesk_ota_4mb_c3.csv';Slot=0x1E0000;Options='FlashSize=4M,PartitionScheme=custom';Dual=0},
  @{Id='standalone-s3';Chip='esp32s3';Partition='robodesk_ota_16mb.csv';Slot=0x300000;Options='FlashSize=16M,PSRAM=opi,PartitionScheme=custom';Dual=0}
)
foreach($profile in $profiles){
  if($Board -eq 'both' -and !$profile.Dual){continue}
  if($Board -ne 'both' -and $Board -ne $profile.Id){continue}
  $artifactId=if($BleDiagnostic -and $profile.Id -eq 'gateway' -and $BleProfile -ne 'baseline'){"gateway-diagnostic-$BleProfile"}elseif($BleDiagnostic -and $profile.Id -eq 'gateway'){'gateway-diagnostic'}else{$profile.Id}
  $stage=Join-Path $output "$artifactId\RoboDeskSonicCharacter"
  $build=Join-Path $output "$artifactId\build"
  New-Item -ItemType Directory -Force -Path $stage,$build | Out-Null
  Get-ChildItem -LiteralPath $root -File | Where-Object {$_.Extension -in '.h','.ino' -and $_.Name -ne 'secrets.h'} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $stage -Force}
  Copy-Item -LiteralPath (Join-Path $root 'secrets.example.h') -Destination (Join-Path $stage 'secrets.h') -Force
  Copy-Item -LiteralPath (Join-Path $root "partitions\$($profile.Partition)") -Destination (Join-Path $stage 'partitions.csv') -Force
  $log=Join-Path (Split-Path -Parent $build) 'compile.log'
  $sdkLibraries=Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\hardware\esp32\3.3.11\libraries'
  $flags="-DROBODESK_DUAL_BOARD=$($profile.Dual)"
  $ownsBle=$profile.Dual -and $profile.Id -eq 'gateway'
  if($profile.Dual){$flags+=' -DROBODESK_PHONE_BLE_ROBOT=0'}
  $bleLibrary=@()
  if($ownsBle){
    $nimble=& (Join-Path $PSScriptRoot 'prepare_nimble.ps1') -Destination (Join-Path $output 'dependencies')
    $bleLibrary=@('--library',$nimble)
    $flags+=' -DROBODESK_NIMBLE_EXTERNAL=1'
    if($BleDiagnostic){
      $flags+=' -DROBODESK_BLE_DIAGNOSTIC=1'
      if($BleProfile -eq 'msys1-6'){$flags+=' -DROBODESK_NIMBLE_PROFILE_MSYS1_6=1'}
    }
  }
  $args=@('compile','--library',(Join-Path $sdkLibraries 'Network'),'--library',(Join-Path $sdkLibraries 'WebServer'))+$bleLibrary+@('--libraries',(Split-Path -Parent $LivingEyesRoot),'--fqbn',"esp32:esp32:$($profile.Chip):$($profile.Options)",'--build-path',$build,'--build-property',"upload.maximum_size=$($profile.Slot)",'--build-property',"compiler.cpp.extra_flags=$flags",$stage)
  $saved=$ErrorActionPreference
  try{$ErrorActionPreference='Continue';& $ArduinoCli @args *> $log;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$saved}
  if($code -ne 0){Get-Content $log -Tail 55;throw "$($profile.Id) build failed ($code)."}
  if($ownsBle){
    $options=Get-Content (Join-Path $build 'build.options.json') -Raw
    $map=Get-Content (Join-Path $build 'RoboDeskSonicCharacter.ino.map') -Raw
    if(!$options.Contains('-DROBODESK_NIMBLE_EXTERNAL=1') -or !$map.Contains('NimBLE-Arduino-2.5.1') -or !$map.Contains('ble_hs.c.o')){throw 'BLE owner did not link the pinned source NimBLE host.'}
    if($map -match '(?:[\\/]|\()BLEDevice\.cpp\.o(?=\)|\s|$)'){throw 'BLE owner selected the core BLE wrapper.'}
    if($map -match 'libbt\.a\(ble_hs\.c'){throw 'BLE owner linked SDK and external NimBLE hosts together.'}
  }
  $image=Get-Item (Join-Path $build 'RoboDeskSonicCharacter.ino.bin')
  if($image.Length -gt $profile.Slot-65536){throw "$($profile.Id) leaves less than 64 KiB OTA slot margin."}
  Write-Host "${artifactId} (BLE profile: $BleProfile): $($image.Length) bytes; slot margin $($profile.Slot-$image.Length) bytes."
  Get-Content $log -Tail 5
}
