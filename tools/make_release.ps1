# make_release.ps1 — empaqueta dist\ como release de Windows.
#
# Deja SOLO lo necesario para ejecutar: exes, runtime de CEF, UI nueva,
# plugins, shaders, editor de dispositivos y ffmpeg/ffprobe. Fuera:
#   - wallpapers descargados (res\providers), miniaturas y previews (res\cache)
#   - exportaciones (res\mobile), subidas (res\uploads), composiciones (res\optimized)
#   - proyectos, configuración de usuario (res\config), logs y current.json
#   - UI antigua Angular (ui\), documentación de dist, cachés de CEF
#   - ficheros que no se usan (ffplay, "ffmpeg (2).exe", yt-dlp, 1.cmd/2.cmd)
#
# Uso:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_release.ps1
#   ... -OutDir D:\Temp\opencode\release -Version v3.0.0
#
param(
  [string]$OutDir = "D:\Temp\opencode\release",
  [string]$Version = "v3.0.0",
  [string]$Dist = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($Dist)) { $Dist = Join-Path $root "dist" }
if (-not (Test-Path $Dist)) { throw "No existe $Dist" }

$name = "DNXWallpaper-v3"
$stage = Join-Path $OutDir $name
$zip = Join-Path $OutDir "$name-win64.zip"

Write-Host "Origen : $Dist"
Write-Host "Destino: $stage"

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

# Carpetas que nunca van en el paquete.
# "cache" se excluye por NOMBRE (cualquier nivel): cubre res\cache y la caché
# de cada plugin, también para plugins nuevos.
$xdNames = @("cache")
$xdPaths = @(
  "cef-cache", "DawnCache", "GPUCache", "ShaderCache", "GrShaderCache",
  "GraphiteDawnCache", "ui", "docs",
  "res\providers", "res\mobile", "res\uploads",
  "res\optimized", "res\projects", "res\config"
) | ForEach-Object { Join-Path $Dist $_ }

# Ficheros que no van: logs, estado de usuario, binarios no usados y
# config de plugins (puede llevar API keys).
$xf = @(
  "*.log", "current.json", "config.json",
  "ffplay.exe", "ffmpeg (2).exe", "yt-dlp.exe", "1.cmd", "2.cmd"
)

robocopy $Dist $stage /E /NFL /NDL /NJH /NJS /XD @xdNames @xdPaths /XF @xf | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy fallo ($LASTEXITCODE)" }

# Comprobaciones mínimas: lo imprescindible tiene que estar.
$need = @(
  "DNXWallpaper.exe", "DNXWallpaper.Motor.exe", "DNXWallpaper.UI.exe",
  "libcef.dll", "icudtl.dat", "cef\resources.pak",
  "res\dnxui\index.html", "res\shaders\dx11fullscreen.hlsl",
  "plugins\mylivewallpapers\main.py", "tools\ffmpeg.exe", "tools\ffprobe.exe"
)
$missing = $need | Where-Object { -not (Test-Path (Join-Path $stage $_)) }
if ($missing) { throw "Faltan en el paquete: $($missing -join ', ')" }

# Nada de medios ni configuracion de usuario por descuido.
foreach ($bad in @("res\providers", "res\cache", "res\mobile", "res\config",
                   "res\uploads", "ui", "res\current.json")) {
  if (Test-Path (Join-Path $stage $bad)) { throw "Se colo '$bad' en el paquete" }
}

if (Test-Path $zip) { Remove-Item $zip -Force }
Push-Location $OutDir
try {
  tar -a -c -f $zip $name
  if ($LASTEXITCODE -ne 0) { throw "tar fallo ($LASTEXITCODE)" }
} finally {
  Pop-Location
}

$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
$sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Write-Host ""
Write-Host "OK $zip ($mb MB)"
Write-Host "sha256:$($sha.Substring(0,12))"
Write-Host "Siguiente: subir el zip y la APK a la release $Version de GitHub."
