# Editor de pantallas (DNXWallpaper v3)

Estado: 2026-10-30. Cubre `/ui2/` → **Pantallas**, el servidor LAN y la
composición del escritorio.

## Idea

El motor pinta **una sola superficie** que cubre el escritorio virtual completo
(`SM_XVIRTUALSCREEN`). Por eso no puede poner un vídeo distinto por monitor.
En vez de tocar el renderer, el editor **prepara** el escritorio y al pulsar
Aplicar el servidor **compone un único vídeo** con cada monitor en su sitio.

## Modelo de estado (`res/config/settings.json`)

```json
"displays": {
  "mode": "individual | clone | expand | none",
  "fit": "expand | fill | center",
  "fade": 0.8,
  "cloneLow": false,
  "perMonitor": {
    "0": { "key": "lively:...", "type": "Video", "fit": "expand",
           "rect": { "x": 0, "y": 0, "w": 1, "h": 1 } },
    "1": { ... }
  },
  "appliedSig": "…", "appliedKey": "optimized:…"
}
```

- `rect` es la región visible del origen, normalizada. `w < 1` es zoom; `x/y`
  pueden salir de `[0,1]` (bandas negras).
- `appliedSig` es la firma del plan aplicado: si el estado actual coincide,
  Aplicar no genera nada.

## Modos

| Modo | Composición |
|------|-------------|
| Individual | una celda por monitor con su vídeo, ajuste y encuadre |
| Clonar | el vídeo del monitor principal repetido en cada celda |
| Panorámico | un vídeo mapeado al escritorio virtual; cada monitor ve su trozo |
| Ninguno | negro a tamaño del escritorio |

Clonar con `cloneLow` codifica a la altura del monitor más pequeño.

## Servidor

- `POST /api/compose` — cuerpo `{canvasW, canvasH, fade, maxSizeMB, outH?,
  cells:[{key,x,y,w,h,cx,cy,cw,ch,fit,aspect}] | panorama:{...} | black:true}`.
  Genera `res/providers/optimized/media/ws*.mp4`, lo registra y lo aplica.
  Usa `crop`+`scale`+`pad` por celda y un `split/trim/xfade/concat` para el
  fundido de bucle.
- `GET /api/search?q=&provider=&limit=` — búsqueda **en vivo en los plugins**
  (`list --query --sort relevance`), una hebra por plugin, resultados mezclados
  y registrados. La usa la caja de búsqueda con Enter.
- `GET /api/library?q=&provider=&type=&orient=&minh=` — filtrado en servidor
  sobre toda la instantánea del catálogo.
- `POST /api/prepare` — prepara una celda por adelantado (cola con prioridad).
  `GET /api/prepare/status`, `POST /api/prepare/cancel?slot=N`.
- `GET /api/progress` — avance real de ffmpeg (0-99) mientras compone.
- `GET /api/preview/<key>` — WebP animado 480 px / 8 fps / 4 s, cacheado en
  `res/cache/preview`. Es la vista previa (no compite por el decodificador).
- `GET /api/probe?key=` — dimensiones, **sin descargar**.
- `POST /api/playlist/activate|stop|interval` — rotación en el motor.
- `POST /api/screensaver/install` — registra `DNXWallpaper.scr` con la espera.

## Detalles que importan

- **Un ffmpeg pesado a la vez** (`g_encodeMutex`): dos 4K simultáneos agotan la
  memoria y tumban el proceso de la UI.
- Al lanzar ffmpeg **sólo se hereda la tubería de progreso**
  (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`). Con `bInheritHandles` a secas el hijo
  heredaba sockets y handles de CEF y la UI se cerraba al terminar.
- El comando de ffmpeg se arma con `std::wstring`, sin búfer fijo.
- Caché de celdas en `res/cache/cells/<hash>.mp4`; el hash incluye clave,
  ajuste, encuadre y tamaño. La composición la reutiliza si existe.

## UI (`dist/res/dnxui/`)

- `displays-v2.js` — editor: `renderWorkspace`, `repositionAll` (sin recrear
  elementos al zoom/pan), `buildPlan`, `planSignature`, `applyWorkspace`.
- `placeShot` / `placePanorama` — misma matemática que el servidor.
- `app.js` — catálogo, selector, playlist, salvapantallas, conexión.

## Pendiente

- Asignación real por monitor en el motor (hoy se resuelve con un vídeo
  compuesto).
- Playlist con vídeos optimizados por monitor.
