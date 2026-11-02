# DNXWallpaper v3

Fondo de escritorio animado para Windows (motor propio D3D11 + DirectComposition)
con catálogo de plugins, editor de escritorio por monitor y app compañera para
Android.

## Descargas

Todo lo que se ejecuta está en **Releases**, no en el código:

| Plataforma | Fichero | Notas |
|---|---|---|
| Windows 10/11 x64 | [`DNXWallpaper-v3-win64.zip`](../../releases/latest) | Descomprimir y ejecutar `DNXWallpaper.exe`. Incluye motor, UI, plugins, editor de dispositivos y ffmpeg/ffprobe. |
| Android 8.0+ | [`DNXWallpaper-v3-companion-debug.apk`](../../releases/latest) | App compañera: catálogo, búsqueda, editor de encuadre, subida al PC. |

No se distribuyen wallpapers, cachés ni configuración de usuario.

## Qué incluye

- **Motor**: vídeo (Media Foundation) e imagen (WIC) sobre el escritorio, bucle
  con crossfade, playlist, salvapantallas, HDR, pausas y optimizaciones.
- **Editor de escritorio** (`/ui2/` → Pantallas): el preview *es* el escritorio.
  Modos Individual / Clonar / Panorámico / Ninguno, zoom y paneo por monitor,
  y un único botón **Aplicar** que compone el escritorio.
- **Asignación real por monitor**: el motor reproduce un vídeo por pantalla en
  su propia celda (sin recomponer ni generar ficheros) cuando el modo es
  Individual/Clonar y el bucle va sin fundido.
- **Catálogo de plugins v2** (Python): `list` / `detail` / `thumb` / `fetch` /
  `health` / `categories`. El medio lo descarga siempre el plugin (tokens,
  referer, cookies, preview alternativo), no el host.
- **Búsqueda en vivo**: filtra en local mientras escribes y, con **Enter**,
  pregunta a todos los plugins en paralelo (catálogo y selector).
- **App Android**: catálogo del PC, búsqueda, favoritos, editor de encuadre,
  subida de vídeo/imagen y aplicación del fondo animado (ExoPlayer + GL).

## Requisitos

- Windows 10/11 x64.
- Python 3 para los plugins (el host lo detecta del PATH o de `tools\`).
- ffmpeg/ffprobe: incluidos en el zip (`tools\`); si faltan, la app los ofrece
  descargar.

## Compilar desde el código

Se necesita Visual Studio (MSVC x64) y el SDK de CEF 120 en
`D:\Tools\cef120-standard` (ajustar la ruta en `tools\build_ui.cmd`).

```powershell
tools\build_launcher.cmd        # DNXWallpaper.exe
tools\build_motor.cmd           # DNXWallpaper.Motor.exe
tools\build_ui.cmd              # DNXWallpaper.UI.exe (CEF)
tools\build_device_editor.cmd   # device-editor.exe
```

La app Android se compila con Gradle:

```powershell
cd appcompanion\DnxCompanion
gradle :app:assembleDebug
```

## Estructura

```
wallpaper-launcher-re\     Lanzador y bandeja
wallpaper-motor-re\        Motor: D3D11, DirectComposition, Media Foundation
wallpaper-ui-re\           Host CEF + servidor LAN + composición (ffmpeg)
device-editor-re\          Editor de dispositivos (ventana)
appcompanion\              App Android (Kotlin/Compose)
dist\res\dnxui\            UI nueva (/ui2/): HTML/CSS/JS sin build
dist\res\editor\           Editor de dispositivos (web)
dist\plugins\              Plugins v2 en Python
docs\                      Contratos, planes y notas técnicas
```

## Estado

Proyecto en cierre de la v3. Pendientes y decisiones en
`.antigravity/ToDo.txt` y `.antigravity/Memory.txt`.
