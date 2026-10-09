param(
  [Parameter(Mandatory)][string]$Image,
  [Parameter(Mandatory)][int]$Version,
  [Parameter(Mandatory)][string]$Host_,
  [Parameter(Mandatory)][string]$Pin
)
# Menandatangani .bin S3 lalu mengunggahnya ke dashboard C3 (/ota/s3); C3 meneruskan lewat UART, S3 yang verifikasi.
$ErrorActionPreference = 'Stop'
python "$PSScriptRoot/ota_signing.py" sign $Image --version $Version --board ESP32-S3/robot-link-v1
$sig = (Get-Content "$Image.sig" -Raw).Trim().Split(':')[1]
$sha = (Get-FileHash $Image -Algorithm SHA256).Hash.ToLower()
$size = (Get-Item $Image).Length
curl.exe --fail-with-body -u "admin:$Pin" -F "firmware=@$Image" "http://$Host_/ota/s3?version=$Version&size=$size&sha256=$sha&sig=$sig"
