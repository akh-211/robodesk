param(
  [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')][string]$Repository,
  [Parameter(Mandatory = $true)][ValidateRange(1, 2147483647)][int]$Version,
  [string]$ArduinoCli = (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe')
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$signer = Join-Path $root 'tools\ota_signing.py'
$exampleSecrets = Join-Path $root 'secrets.example.h'
foreach ($file in @($ArduinoCli, $signer, $exampleSecrets)) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required release input is missing: $file" }
}
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

& $ArduinoCli compile --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=default' --build-path $build --build-property upload.maximum_size=3145728 $stage
if ($LASTEXITCODE -ne 0) { throw "Secret-free firmware build failed (exit $LASTEXITCODE). Staging tree: $stage" }
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
