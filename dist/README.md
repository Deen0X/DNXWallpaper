# Despliegue — cómo ejecutar

**Doble clic en `dist\DNXWallpaper.exe`.** Arranca: motor (fondo animado),
UI (galería y plugins) e icono de bandeja.

## Estructura

```
dist/
  DNXWallpaper.exe            ← punto de entrada
  DNXWallpaper.Motor.exe         ← motor: D3D11 + DirectComposition detrás de iconos
  DNXWallpaper.UI.exe         ← host CEF de la UI
  libcef.dll, chrome_elf.dll, *.pak, *.bin, *.dat ...  (runtime CEF 120)
  cef/                    ← resources.pak, chrome_*.pak, locales/
  ui/dist/                ← UI servida vía http://wpx.internal/
  locales/                ← traducciones (core_*.json, ui_*.json)
  res/
    config/video_sources.json  ← CARPETAS de vídeos que aparecen en la galería
    cache/thumbs/              ← miniaturas generadas automáticamente
    projects/                  ← proyectos locales (vídeo)
    shaders/, scenes/, hashes.json, manifest.json
```

## Cómo añadir tus propios vídeos

Edita `res\config\video_sources.json` y añade rutas absolutas:

```json
{ "folders": ["D:\\Users\\DNX\\Videos\\Wallpaper", "E:\\MisFondos"] }
```

Al arrancar se escanean, se generan miniaturas reales en
`res\cache\thumbs` y aparecen en la galería (pestaña **Instalados** y
**Descubrir**). También puedes importar un archivo suelto con
**Add wallpaper / Open Wallpaper** (diálogo nativo).

## Uso

- **Instalados**: proyectos locales + todos los vídeos de tus carpetas.
- **Descubrir**: sin servicios externos, muestra el mismo catálogo local (equivalente
  offline de la pestaña original).
- Clic en un wallpaper → preview (los vídeos se reproducen en el panel).
- **Aceptar** aplica el fondo: se anima detrás de los iconos, los iconos
  siguen clicables.
- La selección se guarda en `res\current.json` y se restaura al arrancar.
- **catálogo de plugins**: no disponible (requiere servicios externos; eliminado de este build).

## Detener

Icono de bandeja → Salir, o `taskkill /F /IM DNXWallpaper.Motor.exe`
(el launcher termina solo).

## Registro

`res\dnxdnxwallpaper.log` contiene las etapas (attach, D3D11, DirectComposition,
frames de vídeo, miniaturas). Útil para diagnosticar.

## Icono de la bandeja (menú contextual)

En el área de notificaciones (Windows 11 puede ocultarlo en el `^`):

| Acción | Efecto |
|---|---|
| Clic izquierdo (1) / doble clic | Abre la UI |
| Clic derecho | Menú contextual (abajo) |

Menú:
- **Abrir DNXWallpaper v3** → abre la UI.
- **Cambiar fondo** → abre la UI en la galería.
- **Abrir carpeta de fondos** → `res\projects`.
- **Configuración** → wizard de idioma/estado.
- **Salir** → cierra el motor (el launcher termina solo).

## Miniaturas y preview animadas

Los vídeos generan dos miniaturas en `res\cache\thumbs`:
- `<nombre>.png` (estática, respaldo),
- `<nombre>.webp` (**animada**, se usa como `preview`/`previewsmall`).

La galería y el panel de detalles muestran el WebP animado, igual que el
original. Se generan con `ffmpeg` si está disponible (`C:\Windows\ffmpeg.exe`
o en PATH); si no, se usa la estática.

## Iconos de las aplicaciones (editables)

- Fuentes editables: `assets/icons/launcher.png`, `motor.png`, `ui.png`.
- Generar `.ico` tras editar: `python tools\make-icons.py`.
- Cada EXE los lleva embebidos mediante `res\*.rc` (compilado con `rc.exe`).
- El icono de bandeja usa `res\app.ico` (editable/regenerable igual).

## Contenido online ("Descubrir")

El original usa catálogo de plugins. Sin servicios externos, "Descubrir" muestra tu catálogo
local. Para usar webs externas, ver `docs\providers.md` (análisis de
viabilidad: no hay APIs públicas estables; se recomienda descarga manual a
una carpeta indexada o un feed JSON propio).

## Playlist automática con transición

`res\config\playlist.json`:

```json
{
  "intervalSeconds": 45,      // tiempo por wallpaper
  "transitionSeconds": 2,     // crossfade entre wallpapers
  "items": [
    "D:\\Users\\DNX\\Videos\\Wallpaper\\01.mp4",
    "D:\\Users\\DNX\\Videos\\Wallpaper\\02.mp4"
  ]
}
```

El motor cambia de wallpaper cada `intervalSeconds` con un fundido
(crossfade) usando `res\shaders\dx11playlisttransition.hlsl`. Vuelve a
leer el archivo al arrancar.

## Ajustes persistentes

La pestaña *Settings* de la UI guarda en `res\config\settings.json`.
Además:
- **Inicio automático**: activa/desactiva el valor `DNXWallpaperV3`
  en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
- **Restablecer**: elimina `settings.json`.
- Portapapeles (copiar/pegar JSON de presets) funciona vía host nativo.

## Pestaña "Plugins"

La antigua pestaña *catálogo de plugins* ahora se llama **Plugins** y muestra el
catálogo de los plugins instalados en `dist\plugins\` (p. ej. `lively`
y `mock`). Los ítems se piden al host cada 2,5 s; si acabas de añadir un
plugin, espera unos segundos y abre la pestaña.

Los plugins sin bloque `config` en su manifiesto **no necesitan
configuración** (es el caso de `lively` y `mock`).

## Optimizaciones de recursos (equivalente al original)

El original expone estas claves (evidencia REA en el binario):
`playbackfocus`, `playbackmaximized`, `playbackfullscreen`,
`playbackonbattery`, `playbacksleep`, `playbackaudio`, `playbackmode`,
`quality`, `fps`, `extremesleephack`.

Implementado en `res\config\optimizations.json`:

```json
{
  "pauseWhenHidden": true,      // wallpaper oculto/no visible
  "pauseOnFullscreen": true,    // juego/ventana a pantalla completa delante
  "pauseOnMaximized": false,    // (pendiente)
  "pauseOnBattery": false,      // pausar con batería
  "pauseOnSessionLock": true,   // sesión bloqueada / suspensión
  "fps": 30                     // límite de refresco
}
```

Al pausar: no se renderiza (0 GPU), se detiene la decodificación de vídeo y
el temporizador baja a 1 s. Se registra en `res\dnxdnxwallpaper.log` (`opt: paused`
/ `opt: resumed`).

Pendiente: `pauseOnMaximized`, `playbackaudio` (pausar si otra app reproduce
audio), escalado de calidad y HDR.
