# Evidencia clave — render del fondo detrás de iconos (descubrimiento)

Fecha: 2026-10-26. Fuente: binario original en ejecución + REA/Rizin.

## Estructura real del escritorio (verificada en vivo)

```
Progman  (explorer.exe)                       style=0x96000000 ex=0x00200080
├── SHELLDLL_DefView                          ex=0x00080000 (LAYERED)
│   └── SysListView32  (iconos)
└── WorkerW                                   ex=0x080000A0 (LAYERED|NOACTIVATE|TOOLWINDOW|TRANSPARENT)
    └── WPEDesktopDX11Window  (!!)  ← ventana del WE original
        style = 0x50000000  = WS_CHILD | WS_VISIBLE
        ex    = 0x08280000  = WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED
        rect  = 1920x1080
```

Observado con `wallpaper64.exe` original aplicando un wallpaper (el usuario
seleccionó "Fantastic Car" y el fondo se animó detrás de los iconos).

## Conclusiones técnicas (las que costaron la sesión)

1. El ancla correcta es el **WorkerW hijo de ProgMan**, hermano *posterior*
   a `SHELLDLL_DefView`:
   `FindWindowExW(ProgMan, DefView, L"WorkerW", nullptr)`.
   La variante "WorkerW top-level" no existe en esta build de Windows 11.
2. La ventana debe ser **WS_CHILD de ese WorkerW** con EXSTYLE
   `NOACTIVATE|NOREDIRECTIONBITMAP|LAYERED` (0x08280000).
3. **Un swapchain DXGI clásico (CreateSwapChainForHwnd) NO compone** en el
   fondo del escritorio: hay que usar:
   - `IDXGIFactory2::CreateSwapChainForComposition` (FLIP_SEQUENTIAL),
   - `DCompositionCreateDevice` + `CreateTargetForHwnd(workerChild)` +
     `CreateVisual` + `SetContent(swapchain)` + `Commit()`.
4. `WS_EX_NOREDIRECTIONBITMAP` + DirectComposition es lo que permite pintar
   sin superficie GDI y sin tapar los iconos ni bloquear el ratón.
5. Creación de la ventana: si `CreateWindowExW` con LAYERED falla (algunos
   builds), reintentar sin LAYERED; el contenido va por DirectComposition.
6. **UIPI**: si el proceso corre elevado (integridad alta) y Explorer no,
   `SetParent`/ventanas hijas hacia Explorer fallan. El launcher debe
   ejecutarse **sin elevar** (doble clic normal).

## Flujo del motor (implementado)

```
WallpaperManager::Init
  ├─ EnsureSingleInstance (mutex propio del build local)
  ├─ EnumerateMonitors
  ├─ AttachWallpaperParent → WS_CHILD del WorkerW de fondo
  ├─ D3D11 device + swapchain de composición + DirectComposition
  ├─ LoadPlaylistShaders (res/shaders/*.hlsl)
  └─ ApplySelection (res/current.json) → vídeo/imagen
RenderFrame (timer 33 ms): frames MF → textura dinámica → quad fullscreen
ShowWallpaperParent: HWND_BOTTOM + SW_SHOWNOACTIVATE
```

## Vídeo

`VideoWallpaper`: IMFSourceReader con
`MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING` (necesario para que el
decoder H.264 entregue RGB32; sin eso: 0xC00D36B4) + conversión BGRA→RGBA
+ bucle con rebobinado. AVI "DIB" casero NO es soportado por MF
(0xC00D36C4): usar MP4/H.264 reales.

## Galería

- Inventario: `res/projects/*` + carpetas de `res/config/video_sources.json`.
- Miniaturas: `IShellItemImageFactory` (COM en STA) + WIC → PNG.
  `SHCreateItemFromParsingName` exige rutas sin barras duplicadas (el parser
  JSON debe des-escapar `\\`).
- La UI aplica con `browseWallpaperObject.acceptAndClose`; la selección se
  persiste en `res/current.json` y se señaliza con el evento
  `cxWallpaperEngineReload`.
- Pestaña "Descubrir": sin Steam se inyecta el catálogo local
  (`updateWallpapers`).
