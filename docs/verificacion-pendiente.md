# Verificación pendiente (lista de 7 puntos) — 2026-10-27

> Lista original del usuario (mensaje del chat), guardada aquí para no perderla
> al compactar el contexto. Se actualiza al verificar cada punto.

| # | Punto | Estado | Evidencia |
|---|-------|--------|-----------|
| 1 | **Botones superpuestos**: barra flotante pintada encima de la barra real | ✅ Arreglado | `buildToolbar()` es no-op; capturas sin barra extra |
| 7 | **Controles antiguos** (propiedades/presets) al seleccionar | ✅ Arreglado (2026-10-27, definitivo) | Ya no se borran nodos (Angular los recreaba → parpadeo). Ahora CSS permanente `wpxHideCss`: oculta `.browseWallpaperDetailsProperties` (Properties + Your Presets + Workshop Presets + Mobile) y `.browseWallpaperDetailsWorkshopControls` (Unsubscribe + fila Steam). Verificado: panel limpio |
| 2 | **Llamadas/mensajes de Steam** al filtrar | ✅ Arreglado (2026-10-27) | Causa: al cambiar un filtro la UI llama a `queryWorkshop` (servidor). Sin Steam fallaba → modal *y* página "Steam Services Unavailable". Ahora se intercepta `browseWallpaperObject.queryWorkshop` en JS y responde al instante con el catálogo ya filtrado (`__wpxPasses`), además de forzar `hasShownSteamServiceError=true`. Verificado: filtrar por "2560x1440" deja el grid filtrado, sin modal ni error |
| 3 | **Wikimedia / Wallhaven**: barra verde que parpadea y no descarga | ✅ Arreglado | UA/Accept/Referer + `.part` atómico + percent-encoding UTF-8. Verificado: wikimedia 10,6 MB y wallhaven 459 KB descargan y aplican |
| 4 | **Wallpaperwaifu / MotionBG**: pantalla blanca tras descargar | ✅ Arreglado (causa raíz) | 3 fallos: carrera en `g_remoteItems`, `sprintf_s` desbordado en `getDownloadStatus` (`__fastfail`), bucle `logDebug`. 0 caídas en pruebas de 70-100 s con vídeo de 58 MB |
| 5 | **Previews/thumb animado** de wallpapers descargados | ✅ Arreglado y verificado | `GenerateThumbFromMedia`: vídeo → WebP animado 320 px (317 KB); imagen → WebP estático (18,9 / 35,4 KB) |
| — | **Combo "Ajuste de imagen"** se cerraba al instante | ✅ Arreglado (2026-10-27) | `syncPreview()` ya no reescribe el HTML cada 800 ms; sólo actualiza textos |

## Fases
| Fase | Estado | Nota |
|------|--------|------|
| 3 – Salvapantallas | ✅ Hecho y verificado | `/s` topmost + `/c` instala `DNXWallpaper.scr` |
| 4 – Audio / Ajustes | ✅ Completada | FPS reales (`1000/fps` en el timer), pausas (oculto/fullscreen/maximizado/batería/bloqueo), audio y **calidad** (tope de decodificación: low 720p / medium 1080p / auto nativa, con recarga en caliente) + **HDR** (swapchain 16F + PQ Rec.2020, blanco SDR 203 nits, y captura de pantalla convertida a SDR) |
| 5 – Editor | ⚠️ Mínimo | Modal Tier 1 (fill/fit/stretch, audio, abrir carpeta, borrar descarga). Falta trim, velocidad y `project.json` propio |
| APK companion | ⚠️ Análisis iniciado | `io.wallpaperengine.weclient` v2.8.8, minSdk 32, sin librerías nativas. Falta vía de descompilación (`jadx` de `D:\Tools` es sólo el jar del MCP) |

## Bug extra corregido por el camino
- **Vídeo → imagen**: el renderer seguía pintando el último frame del vídeo
  (`vidSRV_` no se liberaba) y la imagen aplicada nunca se veía. Añadido
  `D3D11Renderer::ClearVideoFrame()` llamado desde `RenderFrame()`.
- **Ruido de log**: ya no se intenta descargar por HTTP las miniaturas locales
  `wpx.internal` (evitaba `err 12007`); traza V8 sólo con `WALLPX_V8LOG=1`.
- **Procesos `plugin.exe` huérfanos**: al cerrar la UI quedaban colgados
  escribiendo en una tubería sin lector (y luego Windows mostraba el diálogo
  modal "plugin.exe – Error de la aplicación"). Ahora se lanzan dentro de un
  *job object* con `KILL_ON_JOB_CLOSE`: mueren con la UI.

## Aviso: diálogo "DNXWallpaper.UI.exe – Error del sistema: Unknown Hard Error"
No lo genera la aplicación: la ventana pertenece a **`csrss.exe`** (subsistema
de Windows) y corresponde a `NtRaiseHardError`. En el equipo coincide con
memoria de *commit* casi agotada (23,3 GB de límite, ~1,9 GB libres; la unidad
C: está sin archivo de paginación). Debe desaparecer al reiniciar con el
pagefile nuevo aplicado.

## Incidencias de la revisión (2026-10-27, tarde-3)

| Tema | Estado | Nota |
|------|--------|------|
| Playlist `Configure` / `Add wallpaper` | ✅ | El original deshabilita Configurar con la lista vacía y `Add` metía el ítem remoto (`wpxr:`). Ahora se guarda la ruta local en `playlist.json` |
| Buscador: texto mal pintado | ✅ | Filtrar en cada tecla re-renderizaba miles de tarjetas; ahora *debounce* 250 ms |
| Filtros no se aplicaban | ✅ | Semántica: si hay casillas marcadas, solo pasan esas (antes solo excluía las desmarcadas) |
| Paginación de plugins | ✅ | `maxItemsPerPlugin` (def. 600) en `res/config/plugins.json`. Catálogo 1.580 → 4.553 |
| Mini modo | ✅ | Tres causas: callback `toggleMiniModeCallback` no invocado (dejaba `$a` bloqueado), resize desde el proceso renderer (no aplica), y el botón. Ahora el renderer manda `wpx-mini` al browser, que redimensiona 560×780 y restaura 1440×830 |
| Preview animado al seleccionar | ✅ (sin verificar a ojo) | `EnsureRemoteThumbAsync(key)` en `selectWallpaper`: genera la miniatura del medio descargado o descarga el clip de preview y lo convierte |
| Thumbs animados en toda la rejilla de Plugins | ⏳ | Requiere convertir bajo demanda lo visible (o preview al hover). Con 4.553 ítems no es viable pre-convertir |

## Cierre 2026-10-27 (noche)

- **Mini modo**: ventana 420×820, restaura 1440×830, contenido a todo el ancho
  y **sin botones duplicados** (se ocultan los de la UI: solo queda `>>`; los
  de Windows siguen en la barra nativa). Verificado con capturas.
- **Descargas previas**: `BuildRemoteItemsJson` marca `status:"installed"` +
  `localpath` si el medio ya existe en `res/providers/<plugin>/media/`.
  `localPathOf()` usa ese campo → "Add wallpaper" funciona sin reaplicar.
- **Miniaturas**: al arrancar se registran los `.thumb.webp` ya generados;
  además `ensureThumb(key)` las genera bajo demanda para los 16 primeros
  ítems visibles (máx. 3 conversiones simultáneas).
- **Instrumentación retirada**: `WALLPX_MINITEST`, `WALLPX_NOUIJS`,
  `WALLPX_V8LOG` y los logs `[mini]`. El manejador de crash queda detrás de
  `WALLPX_CRASHLOG=1`.
---

## Fase 0 (2026-10-28)

- [x] Snapshot: `res/cache/catalog.json` (4.719 ítems, ~3,7 MB); `/api/library`
      y `/api/info` responden en arranque en frío antes de que los plugins
      terminen.
- [x] GL: `cover` llena sin deformar; `fit` muestra bandas; selector en la app.
      Probado en emulador con un vídeo 16:9 en pantalla vertical.
- [x] Proxy `?m=1`: `HEAD` no genera (409); `GET` asíncrono con progreso
      (38,2 MB → 8,7 MB); cancelación limpia sin `.part.mp4`.
- [x] Renombrado: `DNXWallpaper.exe` / `.Motor` / `.UI`; título de ventana
      “DNXWallpaper v3”; icono de Plugins; sin exes antiguos en `dist`.
- [x] Firewall: regla privada para `DNXWallpaper.UI.exe` en 8477.
- [ ] Fase 3.3: eliminar el bundle Angular y los métodos/shims internos de
      Steam/Workshop.

