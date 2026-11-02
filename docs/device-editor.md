# Editor de dispositivos — herramienta externa

> Estado: **especificado; implementación pendiente** (2026-10-28).
> Objetivo: adaptar un wallpaper (vídeo o imagen) a la pantalla de un móvil,
> tablet, TV o PC, generar una versión optimizada y entregarla por QR.
> No es el editor de escenas del producto original: es una herramienta propia,
> offline, que produce un vídeo/imagen final.

---

## 1. Arquitectura

```
┌──────────────────────────┐      lanza       ┌──────────────────────────┐
│ DNXWallpaper (app)       │ ───────────────► │ device-export.exe        │
│  UI / catálogo           │                  │  UI CEF + dxcore         │
│  tools.json              │ ◄─────────────── │  ffmpeg / ffprobe        │
│  LAN server /m/<id>      │   exports.json   │  perfiles / cola         │
└──────────────────────────┘                  └──────────────────────────┘
```

- **EXE aparte** desde el principio, con núcleo compartido `dxcore` (config,
  rutas, ffmpeg/ffprobe, thumbs, log).
- La app lo lanza con:
  `--source <ruta> --key <key> --config <dir> --api <base> --token <token>`.
- La herramienta escribe en `res/mobile/` y actualiza `exports.json` con bloqueo
  y rename atómico. La app refresca al cerrar o por watcher.
- Puede abrirse sola (drag&drop de un fichero) y exportar sin la app; el QR
  necesita el servidor LAN de la app para servir el fichero.
- UI: CEF reutilizando el binario existente, un solo browser, una instancia a la
  vez. Se cierra al terminar.
- Registro `res/config/tools.json`:
  ```json
  {
    "id": "device-export",
    "name": "Adaptar a dispositivo",
    "exe": "tools\\device-export.exe",
    "args": ["--source", "{path}", "--key", "{key}"],
    "accepts": ["video", "image"],
    "singleInstance": true
  }
  ```
- El botón “Móvil” de la app es un acceso directo a esta herramienta.

---

## 2. Flujo de usuario

1. Abrir la herramienta con un wallpaper seleccionado (o arrastrar un fichero).
2. Elegir **destino**: orientación, aspect ratio (presets + custom), resolución
   objetivo, calidad, cuadrado y fade.
3. Mover/redimensionar el **viewport** sobre el vídeo/imagen:
   - coords normalizadas (0..1), no píxeles de pantalla;
   - 8 tiradores, bloqueo de aspect, arrastre;
   - imán al borde más próximo con `Ctrl`/`Shift`/`Alt`;
   - “Centrar”, “Reset”, flechas ±1 px.
4. Ver el resultado:
   - un solo `<video>` a tamaño completo + rectángulo encima;
   - lo que se ve dentro del rectángulo **es** el resultado a esa escala;
   - oscurecer el exterior; toggle “Ver todo / Como se verá”;
   - `cover` (recorte) por defecto; `contain` (bandas/fondo) opcional;
   - `stretch` opcional explícito, con aviso (no por defecto).
5. Exportar: progreso, estimación, cancelar.
6. Al terminar: preview del resultado, póster, QR + URL, copiar, abrir carpeta,
   borrar. Guardar perfil.

---

## 3. Semántica de resolución

- **Objetivo = tope del lado corto** (720/1080/1440/2160/custom; default 720;
  persiste). Si el recorte es mayor, se reduce; si es menor, se queda en la
  resolución del recorte (no upscale en el editor).
- La app escala al mostrar; `cover` garantiza que no haya bandas.
- Un 1080p 16:9 recortado a 9:19.5 da 498×1080; con objetivo 720 no se convierte
  en 720 (498 < 720). Se acepta la blandura, se usa fondo desenfocado o se parte
  de un 4K.
- **Cuadrado universal** (preset, off por defecto): master cuadrado; la app
  recorta al aspect de la pantalla y panea. Lado = lado largo objetivo. No
  sustituye al modo dispositivo; es para reutilizar el mismo master en vertical
  y horizontal y para parallax.
- `stretch` no uniforme: solo si el usuario lo elige; se avisa de la deformación.

---

## 4. Pipeline ffmpeg

### 4.1 Crop + escala

```
ffmpeg -y -i src.mp4 \
  -vf "crop=<w>:<h>:<x>:<y>,scale=<ow>:<oh>:flags=lanczos,setsar=1" \
  -c:v libx264 -preset medium -crf <crf> -profile:v high -level 4.1 \
  -pix_fmt yuv420p -an -movflags +faststart out.mp4
```

- Crop y escala en pares (H.264 yuv420p).
- `w/h` = recorte máximo del aspect dentro del fuente; `ow/oh` = recorte escalado
  al objetivo (sin upscale).
- `-an`: los wallpapers van mudos; ahorra tamaño y evita problemas.
- Calidad: 5 niveles → CRF 28/26/23/21/18; `preset medium`.
- HDR: detectar `smpte2084`/`arib-std-b67` y aplicar antes del crop:
  `zscale=t=linear:npl=100,tonemap=hable:desat=0,zscale=p=bt709:t=bt709:m=bt709:r=tv`.
- `ffprobe` con `autorotate`: usar dimensiones mostradas, no codificadas.
- Temporales `.part`; rename atómico; borrar al cancelar.

### 4.2 Fade loop (variante A, rotación)

Objetivo: loop exacto sin salto. Con `D` = duración del fade y `T` = duración:

```
out = V[D .. T-D] + xfade(V[T-D .. T], V[0 .. D], D)
```

- Empieza en `V(D)` y termina en `V(D)`; al reiniciar no hay salto.
- Duración final `T-D`; todos los fotogramas se ven (los primeros `D` durante el
  fundido).
- Alternativa B (congelado) descartada por el freeze; C (añadido con retroceso)
  descartada por el salto atrás.
- Duración configurable (0,25–2 s).
- Audio fuera.

### 4.3 Póster y WebP animado

- Póster: primer fotograma del recorte, JPG de alta calidad, para la biblioteca
  del móvil.
- WebP animado: 3–6 s, 320–480 px, 8–10 fps, bucle, sin audio, para el grid de
  la app. Se genera una vez y se cachea.

### 4.4 Progreso y cancelación

- `-progress pipe:1 -nostats`; parsear `out_time_ms`.
- Un job a la vez (CPU); cola simple.
- Cancelar = terminar el proceso y borrar `.part`.
- Estimar tamaño/tiempo antes de exportar cuando sea posible.

---

## 5. Salida y entrega

```
dist/res/mobile/
  exports.json
  <id>.mp4
  <id>.jpg
  <id>.webp
```

`exports.json` (una entrada por exportación):

```json
{
  "id": "a1b2c3d4",
  "sourceKey": "lively:2b-blindfold",
  "sourcePath": "...",
  "title": "2B Blindfold",
  "device": "Movil vertical",
  "width": 720,
  "height": 1560,
  "duration": 15.0,
  "fade": 1.0,
  "quality": 3,
  "size": 4821331,
  "poster": "a1b2c3d4.jpg",
  "preview": "a1b2c3d4.webp",
  "createdAt": "2026-10-28T12:00:00Z"
}
```

- URL: `http://<ip-lan>:8477/m/<id>`; `HEAD` solo cabeceras.
- `Content-Disposition: attachment; filename="<título> (<destino>).mp4"`.
- `GET /m/<id>` con Range.
- `GET /api/mobile/<id>` → `{title,type,width,height,size,duration,poster,url,device}`.
- El QR codifica la URL normal (funciona sin la app) y se ofrece el deep link
  `dnxwallpaper://open?u=<url>`.
- Cache por hash de (fuente+mtime, rect, aspect, resolución, CRF, fade, HDR,
  cuadrado); si existe, se reutiliza.
- Limpieza de exportaciones no usadas y espacio total.

---

## 6. Perfiles de destino

```json
{
  "name": "Móvil vertical",
  "orientation": "portrait",
  "aspect": "9:19.5",
  "targetShortSide": 720,
  "quality": 3,
  "fade": 1.0,
  "square": false
}
```

- Nombres genéricos (no “Pixel 8”/“iPad”).
- Orientación + aspect + custom.
- “Cuadrado universal” como preset.
- Persistir el último perfil y la última selección de resolución.
- Duplicar/editar/borrar.

---

## 7. Criterios de aceptación

- Exportar un 4K 16:9 a 9:19.5 720p con `cover` y verlo en el móvil sin
  deformación.
- Exportar con `contain` y comprobar las bandas/fondo.
- `stretch` explícito deforma y avisa.
- Cuadrado 2160² → recorte vertical 996×2160 y horizontal 2160×1215 en la app.
- Fade A: el loop no salta; duración final correcta.
- HDR→SDR sin lavado.
- QR sirve el fichero con nombre legible y Range.
- Cancelar no deja `.part`; repetir reutiliza la caché.
- La app aplica la exportación sin descargar el original.

---

## 8. Estimación

| Bloque | Días |
|---|---|
| `dxcore` + registro de herramientas + host CEF | 4–6 |
| UI del editor (preview, viewport, perfiles) | 5–7 |
| Pipeline ffmpeg (crop/escala/HDR/fade/póster/WebP) | 4–6 |
| `/m/<id>`, `/api/mobile/<id>`, exports.json, QR | 2–3 |
| Pruebas y pulido | 2–3 |

Total: **2–3 semanas** de una persona.
---

## 10. Estado de implementación (2026-10-28)

**Hecho (hito 1)**
- `editor_export` (C++): ffprobe, export ffmpeg con crop/escala, HDR→SDR,
  fade A (rotación), póster y metadata; progreso/cancelación.
- Salida `res/mobile/<id>.mp4/.jpg/.json`.
- `/api/editor/current|info|file|export|status|cancel`, `/api/editor/lan`.
- `/m/<id>` (Range) y `/api/mobile/<id>`.
- Página `dist/res/editor/` con viewport, aspect, resolución objetivo, calidad,
  fade, perfiles, resultado y QR. Servida por la app.
- App Android: soporte `/m/` y filtro de intent.

**Validado por el usuario (2026-10-28)**: exportar → QR → escanear en la
app → aplicar el fondo. La bandeja abre el editor con el fondo actual
(`?file=`) y `tools.json` registra la herramienta.

**Hecho (hito 2 — EXE host, 2026-10-28)**
- `device-editor-re/src/main.cpp` → `dist/device-editor.exe` (Win32, sin
  consola): lee `res/current.json` y `res/config/mobile.json`, arranca
  `DNXWallpaper.exe` si el servidor LAN no responde (espera hasta 25 s) y abre
  `http://127.0.0.1:<puerto>/editor/?file=...` en una **ventana de aplicación**
  de Edge/Chrome (`--app=`, 1280×880, sin barra de direcciones). Si no hay
  navegador, usa el predeterminado.
- Argumentos: `--file <ruta>`, `--file=<ruta>`, `--no-wait`.
- La bandeja (`DNXWallpaper.Motor`) y `res/config/tools.json` lanzan
  `device-editor.exe`.
- **Fondos fuera de `res\`**: `editor_export` acepta rutas absolutas con
  extensión de vídeo/imagen **sólo si la petición llega de 127.0.0.1**
  (`/api/editor/info|file|export`); desde la LAN se sigue exigiendo `res\`.
  Así el editor abre el fondo real (p. ej.
  `D:\Users\DNX\Videos\Wallpaper\...mp4`, 1920×1080, 21 s) y exporta sin copias.
- Verificado: `device-editor.exe` arranca la app si está cerrada, abre la
  ventana, carga el vídeo y exporta (`720×1012`, 9,7 MB) con QR.

**Pendiente**
- Cuadrado universal e imán fino; perfiles persistentes.
- Probar HDR real y fade con clips largos.

