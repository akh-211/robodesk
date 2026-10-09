param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dest=[IO.Path]::GetFullPath($Destination)
if(!$dest.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'NimBLE staging must stay inside this workspace.'}
$pin=Get-Content (Join-Path $PSScriptRoot 'nimble-pin.json') -Raw | ConvertFrom-Json
$cache=Join-Path $root '.verification\dependencies'
New-Item -ItemType Directory -Force -Path $cache,$dest | Out-Null
$archive=Join-Path $cache "NimBLE-Arduino-$($pin.version).zip"
if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $pin.url -OutFile $archive}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $pin.sha256){throw 'NimBLE archive fingerprint mismatch.'}
$library=& python (Join-Path $PSScriptRoot 'stage_nimble.py') $dest
if($LASTEXITCODE -ne 0){throw 'NimBLE source staging failed.'}
Write-Output $library
