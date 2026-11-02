# wallpaper-motor-re

Reconstrucción **local y sin Steam** del motor de DNXWallpaper v3
(`distribution/DNXWallpaper.Motor.exe`, PE64 5.11 MB, MSVC 14.51), mediante
ingeniería inversa con **REA** + **Rizin 0.9.1**. Evidencia en `evidence/`.

## Arquitectura reconstruida

```
wWinMain (main.cpp): flags → WallpaperManager::Init → loop de mensajes
  ├── EnsureSingleInstance (mutex dnxWallpaperV3GlobalMutex)
  ├── EnumerateMonitors
  ├── AttachWallpaperParent → WorkerW tras iconos (DNXWallpaperV3Parent)
  ├── CreateWallpaperWindow (DNXWallpaperV3Wallpaper)
  ├── D3D11Renderer::Init (D3D11CreateDevice) + LoadPlaylistShaders
  ├── LoadScene: Scene (scene.json) | Video (Media Foundation)
  └── AudioProcessor (WASAPI loopback + FFT)
IPC: pipes \\.\pipe\WPXCMD_<id> (InterProcessMessageHandler)
```

Clases originales identificadas (símbolos decorados MSVC):
`WallpaperManager::Init`, `Main::LoadScene`,
`SceneWallpaper::LoadSceneAndProperties`,
`VideoWallpaper::StartVideoWithNewPlayer`,
`MaterialSystem::ReloadDirtyMaterials`, `DataCache`,
`Texture::ReadTextureData`, `DesktopMediaExtensions::QueueAsyncTextureLoad`,
`InterProcessMessageHandler::PeekNextRecvMessageCommand`,
`PropertySystem::*`, `AudioEventHandler/AudioProcessor (WASAPI)`.

## Recursos externos (res/, nada embebido)

| Recurso | Descripción |
|---|---|
| `res/manifest.json` | Hashes SHA256 de la distribución (tabla embebida en el original) |
| `res/shaders/dx11playlist*.hlsl` | Shaders de playlist (compilados en runtime) |
| `res/config/default.json` | Config por defecto (clearcolor, vsync, audio) |
| `res/scenes/sample_scene.json` | Escena de ejemplo (formato documentado) |

## Ventanas visibles (sin CEF)

- **Asistente de bienvenida** (`src/wizard.*`): réplica funcional del
  original (cabecera naranja, idioma, Adelante → Todo listo + "Buscar
  fondos ahora"). Solo la primera vez; estado en
  `res/config/welcome.json`.
- **Bandeja del sistema** (`src/tray.*`, icono externo `res/app.ico`):
  doble clic = elegir fondo; menú = Elegir fondo / Abrir carpeta /
  Configuración / Salir.
- **Log**: `res/dnxwallpaper.log` con las etapas del arranque.

## Estado actual (2026-10-26) — funcionando

- **Fondo animado detrás de los iconos**: ventana `WS_CHILD` del WorkerW de
  fondo (hermano posterior de `SHELLDLL_DefView` dentro de ProgMan) +
  **DirectComposition** (`CreateSwapChainForComposition` +
  `IDCompositionTarget/Visual`). Un swapchain HWND clásico no compone.
  Ver `evidence/desktop-composition-findings.md`.
- **Vídeo**: Media Foundation con
  `MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING` (RGB32), hilo lector,
  bucle, textura dinámica D3D11 y quad fullscreen con `cover`.
- **Imágenes**: WIC (PNG/JPG/BMP/GIF) como fondo.
- **Selección**: `res/current.json` + evento `dnxWallpaperV3Reload`;
  se restaura al arrancar.
- **Galería**: inventario de `res/projects` + carpetas de
  `res/config/video_sources.json` con miniaturas reales.
- **Ventanas**: wizard de bienvenida, icono de bandeja y log
  `res/dnxwallpaper.log`.
- Requisito: ejecutar **sin elevar** (UIPI bloquea ventanas hijas de
  Explorer si el proceso es de integridad alta).

## Eliminado (Steam)

`-op steamscreenshot`, lectura de `Software\Valve\Steam\Users`,
`WriteSteamMiniDump`, `-steamlanguage` (ahora alias local de `-language`).

## Compilar (MSVC v143)

```
cl ... (ver wallpaper.vcxproj) d3d11.lib d3dcompiler.lib dwmapi.lib
   mfreadwrite.lib mfplat.lib shlwapi.lib advapi32.lib user32.lib kernel32.lib
```

Uso: `DNXWallpaper.Motor.exe -file escena.json [-monitor N] [-language es] ...`
Uso responsable: solo con software que poseas o estés autorizado a examinar.
