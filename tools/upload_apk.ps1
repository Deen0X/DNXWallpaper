param(
    [string]$Apk = "",
    [string]$Caption = "",
    [ValidateSet('low', 'normal', 'high')][string]$Lane = 'low',
    [switch]$Front,
    [string]$Chat = "",
    [int]$Topic = -1
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$envFile = Join-Path $root '.tcloud.env'
if (-not (Test-Path $envFile)) {
    throw "Falta $envFile (credenciales TCloud)."
}

# Cargar el entorno sin imprimir valores.
Get-Content -Path $envFile -Encoding UTF8 | ForEach-Object {
    $line = $_.Trim()
    if ($line -eq '' -or $line.StartsWith('#')) { return }
    $i = $line.IndexOf('=')
    if ($i -lt 1) { return }
    $k = $line.Substring(0, $i).Trim()
    $v = $line.Substring($i + 1).Trim().Trim('"').Trim("'")
    [Environment]::SetEnvironmentVariable($k, $v, 'Process')
}

if (-not $Apk) {
    $Apk = Join-Path $root 'appcompanion\DnxCompanion\app\build\outputs\apk\debug\app-debug.apk'
}
if (-not (Test-Path $Apk)) {
    throw "No existe el APK: $Apk"
}
$Apk = (Resolve-Path $Apk).Path

if (-not $Caption) {
    $Caption = "DNXWallpaper v3 companion - debug - " + (Get-Date -Format 'yyyy-MM-dd HH:mm')
}
if ($Caption.Length -gt 900) { $Caption = $Caption.Substring(0, 900) }
$hash = (Get-FileHash -Algorithm SHA256 -Path $Apk).Hash.Substring(0, 12).ToLower()
$Caption = $Caption + " - sha256:" + $hash

$pyArgs = @($Apk, '--caption', $Caption, '--lane', $Lane)
if ($Front) { $pyArgs += '--front' }
if ($Chat) { $pyArgs += @('--chat', $Chat) }
if ($Topic -ge 0) { $pyArgs += @('--topic', [string]$Topic) }

& python (Join-Path $PSScriptRoot 'tcloud_upload.py') @pyArgs
exit $LASTEXITCODE
