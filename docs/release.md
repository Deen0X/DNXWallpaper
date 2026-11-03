# Release de DNXWallpaper v3

El código vive en el repositorio; **lo que se ejecuta vive en Releases**.
GitHub no permite ficheros de más de 100 MB, y `dist\` (CEF + ffmpeg) pasa de
400 MB, así que se publica como **un zip por release** junto a la APK.

## Qué entra en el paquete

| Entra | Por qué |
|---|---|
| `DNXWallpaper.exe`, `DNXWallpaper.Motor.exe`, `DNXWallpaper.UI.exe` | El producto |
| `device-editor.exe`, `DNXWallpaper.scr` | Editor de dispositivos y salvapantallas |
| `libcef.dll`, `chrome_elf.dll`, `*.pak`, `locales\`, `icudtl.dat`, `*.bin`, `libEGL/libGLESv2`, `vk_swiftshader*`, `vulkan-1.dll`, `d3dcompiler_47.dll` | Runtime de CEF / gráficos |
| `res\dnxui\` | UI nueva (`/ui2/`) |
| `res\editor\` | Editor de dispositivos (web) |
| `res\shaders\`, `res\icons\`, `res\scenes\`, `res\app.ico`, `res\hashes.json`, `res\manifest.json` | Recursos del motor |
| `plugins\` | Plugins v2 (Python) sin su caché ni `config.json` |
| `tools\ffmpeg.exe`, `tools\ffprobe.exe` | Composición/optimización |
| `README.md` | Instrucciones |

## Qué NO entra

- `res\providers\`, `res\cache\`, `res\mobile\`, `res\uploads\`, `res\optimized\`,
  `res\projects\`: wallpapers descargados, miniaturas, exportaciones y composiciones.
- `res\config\` y `plugins\*\config.json`: configuración de usuario (puede llevar
  API keys y rutas locales).
- `dist\ui\`: UI Angular antigua (la v3 usa `res\dnxui`).
- `dist\docs\`, logs (`*.log`), `current.json`.
- Cachés de CEF (`cef-cache`, `GPUCache`, `DawnCache`, ...).
- `ffplay.exe`, `ffmpeg (2).exe`, `yt-dlp.exe`, `1.cmd`, `2.cmd` (no se usan).

## Generar el paquete

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_release.ps1
# -> D:\Temp\opencode\release\DNXWallpaper-v3-win64.zip
```

El script comprueba que están los ficheros imprescindibles y que **no** se ha
colado ninguna carpeta de datos. Sale con `sha256:<12>` para el changelog.

## Publicar

1. Compilar todo (`tools\build_*.cmd`) y la APK (`gradle :app:assembleDebug`).
2. Generar el zip con `tools\make_release.ps1`.
3. Crear la release en GitHub (tag `vX.Y.Z`) y adjuntar:
   - `DNXWallpaper-v3-win64.zip`
   - `DNXWallpaper-v3-companion-debug.apk` (copia de `app-debug.apk`)
4. Anotar en el cuerpo: cambios + `sha256:<12>` del zip y de la APK.

Última publicada: **v3.0.2**

- https://github.com/Deen0X/DNXWallpaper/releases/tag/v3.0.2
- `DNXWallpaper-v3-win64.zip` — 199,1 MB — `sha256:0d68344a1b8c`
- `DNXWallpaper-v3-companion-debug.apk` — 22,4 MB — `sha256:0020d0d63fbf`

Enlaces directos:

- Windows: https://github.com/Deen0X/DNXWallpaper/releases/download/v3.0.2/DNXWallpaper-v3-win64.zip
- Android: https://github.com/Deen0X/DNXWallpaper/releases/download/v3.0.2/DNXWallpaper-v3-companion-debug.apk

Anteriores: **v3.0.1** (`sha256:d026ef031257`) y **v3.0.0**
(`sha256:e6bc5cc79e83`).
