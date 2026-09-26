param(
  [Parameter(Mandatory = $true)][string]$BackupDir,
  [string]$Port = "COM9"
)
$ErrorActionPreference = "Stop"
$esptool = Get-ChildItem (Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\tools\esptool_py") -Recurse -Filter esptool.exe | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$boot = Join-Path $BackupDir "bootloader-before.bin"
$otadata = Join-Path $BackupDir "otadata-before.bin"
$table = Join-Path $BackupDir "partition-table-before.bin"
foreach ($file in @($esptool, $boot, $otadata, $table)) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required recovery file is missing: $file" }
}
if ((Get-Item -LiteralPath $boot).Length -ne 0x8000 -or (Get-Item -LiteralPath $otadata).Length -ne 0x2000 -or (Get-Item -LiteralPath $table).Length -ne 0x1000) {
  throw "Recovery backup sizes are invalid; no flash writes were made."
}
Write-Host "This restores the original bootloader and partition table. The legacy application at 0x10000 remains untouched."
& $esptool --chip esp32s3 --port $Port --baud 115200 write-flash --flash-size keep --flash-mode keep --flash-freq keep 0x0 $boot
if ($LASTEXITCODE -ne 0) { throw "Bootloader restore failed. Keep power connected and rerun recovery." }
& $esptool --chip esp32s3 --port $Port --baud 115200 write-flash --flash-size keep --flash-mode keep --flash-freq keep 0xe000 $otadata
if ($LASTEXITCODE -ne 0) { throw "OTA metadata restore failed. Keep power connected and rerun recovery." }
& $esptool --chip esp32s3 --port $Port --baud 115200 write-flash --flash-size keep --flash-mode keep --flash-freq keep 0x8000 $table
if ($LASTEXITCODE -ne 0) { throw "Partition-table restore failed. Keep power connected and rerun recovery." }
Write-Host "Original bootloader, OTA metadata, and partition table restored. NVS and legacy app contents were not overwritten."
