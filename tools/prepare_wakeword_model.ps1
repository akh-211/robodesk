param(
  [string]$ArduinoData = (Join-Path $env:LOCALAPPDATA 'Arduino15'),
  [string]$CoreVersion = '3.3.11',
  [string]$OutputDirectory = (Join-Path $PSScriptRoot '../.verification/wakeword')
)
$ErrorActionPreference = 'Stop'
$source = Join-Path $ArduinoData "packages/esp32/tools/esp32s3-libs/$CoreVersion/esp_sr/srmodels.bin"
$csv = Join-Path $PSScriptRoot '../partitions/robodesk_ota_16mb.csv'
if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Installed Espressif model bundle not found: $source" }
$layout = Get-Content -LiteralPath $csv -Raw
if ($layout -notmatch '(?m)^model,data,spiffs,0xa00000,0x600000\s*$') { throw 'Model offset/size differs from the installed layout; refuse to prepare flashing instructions.' }
$bytes = [IO.File]::ReadAllBytes($source)
if ($bytes.Length -gt 0x600000 -or $bytes.Length -lt 32) { throw 'Model bundle does not fit the reserved partition.' }
$text = [Text.Encoding]::ASCII.GetString($bytes)
if ($text -notmatch 'wn9_hiesp' -or $text -notmatch 'mn7_en') { throw 'The bundle must contain Hi ESP WakeNet and the English MultiNet dependency required by this core.' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output = Join-Path $OutputDirectory 'srmodels.bin'
Copy-Item -LiteralPath $source -Destination $output
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
[ordered]@{ core=$CoreVersion; phrase='Hi ESP'; offset='0xa00000'; partitionSize=0x600000; bytes=$bytes.Length; sha256=$hash } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'model-manifest.json') -Encoding UTF8
Write-Output "Prepared $output ($($bytes.Length) bytes, SHA256 $hash). No device was modified."
