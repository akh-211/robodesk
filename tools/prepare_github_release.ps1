param(
  [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')][string]$Repository,
  [Parameter(Mandatory = $true)][ValidateRange(1, 2147483647)][int]$Version,
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
if (!(Test-Path -LiteralPath $LivingEyesRoot -PathType Container)) { throw "LivingEyes checkout not found: $LivingEyesRoot" }
$eyesRoot = (Resolve-Path -LiteralPath $LivingEyesRoot).Path
$libraryParent = Split-Path -Parent $eyesRoot
$fingerprint = Join-Path $root 'tools\livingeyes_fingerprint.py'
$pin = Join-Path $root 'tools\livingeyes-pin.json'
$signer = Join-Path $root 'tools\ota_signing.py'
$exampleSecrets = Join-Path $root 'secrets.example.h'
$partitionCsv = Join-Path $root 'partitions\robodesk_ota_16mb.csv'
foreach ($file in @($ArduinoCli, $fingerprint, $pin, $signer, $exampleSecrets, $partitionCsv)) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required release input is missing: $file" }
}
& python $fingerprint $eyesRoot --pin $pin
if ($LASTEXITCODE -ne 0) { throw "LivingEyes checkout does not match the reviewed pin: $eyesRoot. No release was built or signed." }
Write-Host "Release build using pinned LivingEyes: $eyesRoot"
$secretState = Join-Path $root '.ota-secrets\last-github-release-version.txt'
$lastVersion = 2
if (Test-Path -LiteralPath $secretState -PathType Leaf) {
  $lastVersion = [int](Get-Content -LiteralPath $secretState -Raw).Trim()
}
if ($Version -le $lastVersion) { throw "Release version must exceed the last signed version ($lastVersion)." }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stageRoot = Join-Path $env:TEMP ("RoboDeskSonicCharacter-public-release-$stamp")
$stage = Join-Path $stageRoot 'RoboDeskSonicCharacter'
$build = Join-Path $env:TEMP ("RoboDeskSonicCharacter-public-build-$stamp")
New-Item -ItemType Directory -Force -Path $stage, $build | Out-Null
& robocopy $root $stage /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD .git .ota-secrets .verification firmware-releases .codex .agents __pycache__ /XF secrets.h FIRMWARE_UPDATE_PLAN.md HOTFIX*.txt *.bin *.elf *.map *.sig *.pyc
if ($LASTEXITCODE -ge 8) { throw "Could not stage the public source tree (robocopy exit $LASTEXITCODE)." }
Copy-Item -LiteralPath $exampleSecrets -Destination (Join-Path $stage 'secrets.h')
Copy-Item -LiteralPath $partitionCsv -Destination (Join-Path $stage 'partitions.csv')

$compileLog = Join-Path $build 'arduino-cli-verbose.log'
# Windows PowerShell 5 treats redirected native stderr as a terminating error
# under Stop; retain the process exit code and check it explicitly instead.
$previousErrorAction = $ErrorActionPreference
try {
  $ErrorActionPreference = 'Continue'
  & $ArduinoCli compile --verbose --libraries $libraryParent --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom' --build-path $build --build-property upload.maximum_size=3145728 $stage 2>&1 | Tee-Object -FilePath $compileLog
  $compileExitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = $previousErrorAction }
if ($compileExitCode -ne 0) { throw "Secret-free firmware build failed (exit $compileExitCode). Staging tree: $stage; log: $compileLog" }
$libraryMatches = [regex]::Matches((Get-Content -LiteralPath $compileLog -Raw), '(?im)Using library LivingEyes.*?in folder:\s*([^\r\n]+)')
if ($libraryMatches.Count -eq 0) { throw "Arduino CLI did not report which LivingEyes library it selected; refusing to sign. Log: $compileLog" }
foreach ($match in $libraryMatches) {
  $reportedPath = $match.Groups[1].Value.Trim()
  if (!(Test-Path -LiteralPath $reportedPath -PathType Container)) { throw "Arduino CLI reported a missing LivingEyes path: $reportedPath" }
  $selectedPath = (Resolve-Path -LiteralPath $reportedPath).Path
  if (![string]::Equals($selectedPath, $eyesRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Arduino CLI selected LivingEyes at $selectedPath instead of $eyesRoot; refusing to sign. Log: $compileLog"
  }
}
Write-Host "Arduino CLI LivingEyes resolver verified: $eyesRoot"
& python $fingerprint $eyesRoot --pin $pin
if ($LASTEXITCODE -ne 0) { throw "LivingEyes changed during compilation; refusing to sign. Log: $compileLog" }
$compiledPartitions = Get-Content -LiteralPath (Join-Path $build 'partitions.csv') -Raw
if ($compiledPartitions -notmatch '(?m)^app0,app,ota_0,0x400000,0x300000' -or $compiledPartitions -notmatch '(?m)^app1,app,ota_1,0x700000,0x300000') {
  throw 'Build did not use the RoboDesk two-slot 3 MiB OTA partition table; refusing to sign this image.'
}
$image = Join-Path $build 'RoboDeskSonicCharacter.ino.bin'
$imageSig = "$image.sig"
if ((Get-Item -LiteralPath $image).Length -gt 0x300000) { throw 'Firmware image exceeds one 3 MiB OTA slot.' }
& python $signer sign $image --version $Version
if ($LASTEXITCODE -ne 0) { throw 'Firmware signing failed; no release manifest was generated.' }
& python $signer verify $image $imageSig
if ($LASTEXITCODE -ne 0) { throw 'Firmware signature verification failed; no release manifest was generated.' }

$digest = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
$asset = 'RoboDeskSonicCharacter.ino.bin'
$signature = (Get-Content -LiteralPath $imageSig -Raw).Trim()
$manifest = @(
  'format=robodesk-ota-v1'
  'board=esp32s3'
  "version=$Version"
  "size=$((Get-Item -LiteralPath $image).Length)"
  "sha256=$digest"
  "image=$asset"
  "signature=$signature"
  "url=https://github.com/$Repository/releases/download/v${Version}/$asset"
) -join "`n"
$manifestPath = Join-Path $build 'manifest.txt'
Set-Content -LiteralPath $manifestPath -Value $manifest -Encoding ascii
Set-Content -LiteralPath (Join-Path $root '.ota-secrets\last-github-release-version.txt') -Value $Version -NoNewline
Write-Host "Secret-free signed release prepared: $build"
Write-Host "Upload these three files to GitHub Release tag v${Version}:"
Write-Host "  $image"
Write-Host "  $imageSig"
Write-Host "  $manifestPath"
Write-Host 'The signed image is available in the output directory; credentials from local secrets.h were not copied into the build.'
