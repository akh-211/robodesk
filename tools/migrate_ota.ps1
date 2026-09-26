param(
  [string]$Port = "COM9",
  [string]$BuildDir = (Join-Path $PSScriptRoot "..\firmware-releases\migration-v2"),
  [switch]$ConfirmMigration
)
$ErrorActionPreference = "Stop"
$esptool = Get-ChildItem (Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\tools\esptool_py") -Recurse -Filter esptool.exe | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$parttool = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\hardware\esp32\3.3.11\tools\gen_esp32part.py"
$partcsv = Join-Path $PSScriptRoot "..\partitions\robodesk_ota_16mb.csv"
$partbin = Join-Path $BuildDir "robodesk_ota_16mb.bin"
$backupDir = Join-Path $env:TEMP ("RoboDeskSonicCharacter-pre-ota-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
$app = Join-Path $BuildDir "RoboDeskSonicCharacter.ino.bin"
$boot = Join-Path $BuildDir "RoboDeskSonicCharacter.ino.bootloader.bin"
$releaseVersionFile = Join-Path $BuildDir "release-version.txt"
$keyHeader = Join-Path $PSScriptRoot "..\FirmwareOtaKey.h"
foreach ($file in @($esptool, $parttool, $partcsv, $app, $boot)) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required migration file is missing: $file" }
}
if (Test-Path -LiteralPath $releaseVersionFile -PathType Leaf) {
  $releaseVersion = [int](Get-Content -LiteralPath $releaseVersionFile -Raw).Trim()
  $headerText = Get-Content -LiteralPath $keyHeader -Raw
  if ($headerText -notmatch "#define\s+ROBODESK_OTA_INITIAL_VERSION\s+$releaseVersion`UL\b") {
    throw "Fresh-NVS baseline in FirmwareOtaKey.h must match migration release version $releaseVersion. No flash writes were made."
  }
}
if ((Get-Item -LiteralPath $app).Length -gt 0x300000) { throw "Application exceeds one 3 MiB OTA slot." }
python $parttool --flash-size 16MB $partcsv $partbin
if ($LASTEXITCODE -ne 0) { throw "Partition table validation failed." }
Write-Host "Target: $Port; flash writes are bootloader @0x0, app slots @0x400000/@0x700000, partition table @0x8000 (last)."
Write-Host "Image size: $((Get-Item -LiteralPath $app).Length) bytes; each slot: 3145728 bytes."
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
& $esptool --chip esp32s3 --port $Port --baud 115200 read-flash 0x0 0x8000 (Join-Path $backupDir "bootloader-before.bin")
if ($LASTEXITCODE -ne 0) { throw "Could not back up the existing bootloader; no flash writes were made." }
& $esptool --chip esp32s3 --port $Port --baud 115200 read-flash 0x8000 0x1000 (Join-Path $backupDir "partition-table-before.bin")
if ($LASTEXITCODE -ne 0) { throw "Could not back up the existing partition table; no flash writes were made." }
& $esptool --chip esp32s3 --port $Port --baud 115200 read-flash 0x9000 0x5000 (Join-Path $backupDir "nvs-before.bin")
if ($LASTEXITCODE -ne 0) { throw "Could not back up NVS; no flash writes were made." }
& $esptool --chip esp32s3 --port $Port --baud 115200 read-flash 0xe000 0x2000 (Join-Path $backupDir "otadata-before.bin")
if ($LASTEXITCODE -ne 0) { throw "Could not back up OTA metadata; no flash writes were made." }
Write-Host "Bootloader, partition table, NVS, and OTA metadata backups: $backupDir"
Write-Host "Keep robot power stable. Do not unplug USB or power during migration."
if (-not $ConfirmMigration) { throw "Pass -ConfirmMigration only after reviewing the target port and layout. No flash writes were made." }
& $esptool --chip esp32s3 --port $Port --baud 115200 write-flash --flash-size keep --flash-mode keep --flash-freq keep 0x0 $boot 0x400000 $app 0x700000 $app
if ($LASTEXITCODE -ne 0) { throw "Bootloader/application flash failed. Keep power connected and run tools/restore_ota_partition.ps1 with backup dir to recover." }
& $esptool --chip esp32s3 --port $Port --baud 115200 write-flash --flash-size keep --flash-mode keep --flash-freq keep 0x8000 $partbin
if ($LASTEXITCODE -ne 0) { throw "Partition-table activation failed. Keep power connected and run tools/restore_ota_partition.ps1 with backup dir to recover." }
Write-Host "Migration flash completed. Initial USB-installed image is not pending OTA, so verify serial boot and dashboard manually. Later Wi-Fi updates run the local rollback self-test."
