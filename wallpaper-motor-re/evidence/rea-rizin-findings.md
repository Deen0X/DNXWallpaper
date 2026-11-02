# Evidencia REA + Rizin — wallpaper64.exe (motor)

Binario: `distribution/wallpaper64.exe` (5.360.112 bytes).
Fecha: 2026-10-25.

## Identificación (Rizin `rabin2 -I` + REA `inspect-*`)

- PE32+ x64, MSVC Linker 14.51, lenguaje C, subsistema Windows GUI.
- Compilado: 29-jun-2026. Firmado. `stripped=false`, `havecode=true`.

## Imports (REA `inspect-pe-imports`, 13 DLLs)

KERNEL32 (180), USER32 (106), GDI32 (12), SHELL32 (5), ole32 (7),
ADVAPI32 (13), d3d11!D3D11CreateDevice (1), dwmapi (2), WINMM (2),
WTSAPI32 (2), SHLWAPI (1), MFReadWrite (2), DWrite (1).
Sin steam_api estático.

## Strings clasificados (Python, 21.393 ASCII + 890 UTF-16)

- **Flags**: -file -location -monitor -width -height -preset -playlist
  -profile -properties -safe -disablefx -disablegifs -borderless -activate
  -host -control -window -tex -nowallpapers -weuimain -updateuicmd -process
  -silent -buildv -skin -language -steamlanguage -monitordetection
  -monitormap -x64 -restartapplocal (+ ruido de idiomas: -navajo...).
- **Steam (a eliminar)**: ` -steamlanguage `, `-op steamscreenshot`,
  `Software\Valve\Steam\Users`, `WriteSteamMiniDump`, `[steamusers]`.
- **Ventanas/mutex**: `WallpaperEngineParent`, `WallpaperEngineWallpaper`,
  `cxWallpaperEngineGlobalMutex`, `eKristutta.WallpaperEngine`.
- **Registro**: `Software\WallpaperEngine\`, DWM, Personalize,
  VisualEffects, Wallpapers, `.../WallpaperEngineLockOverride.jpg`.
- **IPC**: `\\.\pipe\WPXCMD_`, ` -messagehandler `,
  `__WPXProviderAPIMediaControlPID/SSPID`.
- **Audio**: `AudioEventHandler/AudioProcessor (WASAPICallbackType)`,
  `ffts_static.c` (`D:\dev\we\windows\src\...` — path PDB),
  `audioprocessing*`, `audioinput*`, `MFCreateAudioRendererActivate`,
  "Your audio driver crashed ...".
- **Escena/render**: `SceneWallpaper`, `VideoWallpaper`,
  `MaterialSystem`, `DataCache`, `DesktopMediaExtensions`,
  `InterProcessMessageHandler`, `PropertySystem`, `VirtualDesktop`,
  `Main::LoadScene`, `Texture::ReadTextureData`, `ParticleVbo`,
  `ImageLayer`, `FADEEFFECT`, `SHADERVERSION`, `g_TextureN`,
  `assets/shaders/HLSL/dx11playlistgaussian|transition`,
  `assets/scenes/videoplayer/scene.json`, "Failed creating video player
  (%s): %x", `UpdateLayeredWindow`, `SetLayeredWindowAttributes`.

## Clases/métodos (símbolos `?Metodo@Clase@@`, 12 clases)

WallpaperManager::Init, Main::LoadScene,
SceneWallpaper::LoadSceneAndProperties,
VideoWallpaper::StartVideoWithNewPlayer,
MaterialSystem::ReloadDirtyMaterials,
DataCache::AddSharedRemoveListenerToLayer, Texture::ReadTextureData,
DesktopMediaExtensions::{QueueAsyncTextureLoad, StartMediaControl},
InterProcessMessageHandler::PeekNextRecvMessageCommand,
PropertySystem::{LoadMediaIntegrationOnDemand,
LoadShellThumbnailIntegrationOnDemand}, VirtualDesktop::AW4IntResult.

## Verificación del .exe reconstruido

Compilado con MSVC 14.51 (misma versión): `wallpaper64.exe` 300 KB,
PE64 GUI. Código validado por compilación; ejecución real requiere
entorno con D3D11 + monitores (fase 2: pruebas en VM con WorkerW).

## Limitaciones

- Sin Ghidra en Windows: reconstrucción a nivel de arquitectura +
  contratos, no traducción instrucción a instrucción.
- Stubs documentados: frame pumping de vídeo a D3D11, WASAPI/FFT
  completos, decodificación de texturas (FreeImage), web/application
  (procesos externos). Fase 2 con Ghidra o depuración dinámica.
