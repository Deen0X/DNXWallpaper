# Plan de implementación — Companion móvil DNX Wallpaper

> Estado: **plan aprobado e implementación iniciada** (2026-10-27).
> Objetivo: app Android propia, **abierta** (sin cuentas), que reciba
> wallpapers del escritorio por **QR** o navegando la biblioteca, sin formato
> propietario (mp4/jpg/webm tal cual).

## 1. Principios de diseño

1. **Sin cuentas ni login.** Todo es local (LAN) y opcional.
2. **El QR es una URL HTTPS/HTTP normal**: funciona aunque el usuario no tenga
   la app instalada (se descarga el fichero desde el navegador).
3. **Deep link** `dnxwallpaper://...` para que el sistema abra nuestra app
   (permite, en el futuro, una web de wallpapers con QR por ficha).
4. **Cero formato propio**: se sirve el mp4/jpg/webm original.
5. **El escritorio manda**: la app nunca necesita códecs nuevos; el render del
   escritorio sigue siendo el motor.

## 2. Arquitectura

```
┌──────────────────────┐        LAN (HTTP)         ┌───────────────────┐
│  Escritorio (CEF)    │ ◄───────────────────────► │  App Android      │
│  lan_server (C++)    │   /api/info               │  Kotlin + Compose │
│  ├─ /w/<key> (QR)    │   /api/library            │  ├─ QR scanner    │
│  ├─ /api/library     │   /api/thumb/<key>        │  ├─ Biblioteca    │
│  └─ /api/info        │   /dl/<key> (Range)       │  └─ Descarga      │
└──────────────────────┘                           └───────────────────┘
```

- El servidor HTTP vive en el **host CEF** (proceso browser) y lee el catálogo
  ya existente (`g_registry` de `plugin_host`).
- No se abre ningún puerto hacia Internet: sólo escucha en la LAN y sólo
  mientras la app está arrancada.

## 3. Contrato HTTP (v1)

Puerto configurable en `res/config/mobile.json` (por defecto **8477**).

| Método | Ruta | Devuelve |
|---|---|---|
| GET | `/api/info` | `{"name","guid","version","port","auth":false}` |
| GET | `/api/library?offset=0&limit=50` | `[{"id","title","type","width","height","thumb","url"}]` |
| GET | `/api/thumb/<key>` | WebP (estático o animado) |
| GET | `/dl/<key>` | **Fichero original** (mp4/jpg/webm) con `Range` y `Content-Disposition: attachment`. Si aún no está descargado, lo baja en ese momento (120 s máx.) |
| POST | `/api/apply` | `{"id"}` → aplica en el escritorio (v2) |

**QR**: `http://<ip-lan>:8477/dl/<key>` (con `&t=<token>` si hay token).

Deep link equivalente: `dnxwallpaper://open?u=<url-encoded>`.

## 4. App Android

- **minSdk 26**, targetSdk 34, Kotlin + Jetpack Compose (Material 3).
- Pantallas:
  1. **Inicio**: botón *Escanear QR*, campo para pegar URL, y "buscar en la red"
     (multicast UDP 7884 con `WPX/Ping`, igual patrón que WE).
  2. **Ficha**: miniatura animada, título, tipo, resolución, tamaño y botones
     *Descargar* / *Establecer fondo* (imágenes) / *Guardar en Galería*.
  3. **Biblioteca** (si se descubrió un PC): lista con miniaturas y buscador.
  4. **Ajustes**: token, puerto manual, tema.
- Descarga con `DownloadManager` (reanudable, va a `Downloads/DNX Wallpaper`).
- Imágenes → `WallpaperManager.setStream()`; vídeos → guardar + compartir
  (poner vídeo de fondo requiere un `WallpaperService` propio: **v2**).
- QR con `journeyapps:zxing-android-embedded` (sin Google Play Services).
- Deep link: `intent-filter` con `scheme="dnxwallpaper"` + host `open`.

## 5. Fases

| Fase | Alcance | Entregable |
|---|---|---|
| **F1** | Servidor LAN en el escritorio (`/api/info`, `/api/library`, `/dl`) | Verificado con `curl` |
| **F2** | QR por ficha + copiar URL en la UI | QR visible en el panel |
| **F3** | App: escanear/pegar URL + descarga + guardar | APK debug |
| **F4** | App: biblioteca + miniaturas + aplicar imagen | APK beta |
| **F5** | Descubrimiento UDP automático | sin escribir IP |
| **F6** | Vídeo como fondo en el móvil (`WallpaperService`) | opcional |

## 6. Seguridad

- Escucha sólo en interfaces privadas; el token (opcional) se genera en
  `mobile.json`.
- Sin TLS en v1 (LAN doméstica); v2: certificado autofirmado + *pinning* en la
  app (mismo criterio que la app oficial con RSA).
- El servidor no expone rutas de disco: sólo resuelve `key` → fichero del
  catálogo (`MediaPathForCheck`), así no hay *path traversal*.

## 7. Cómo compilar la app

El SDK sí está en `D:\Tools` (se descubrió tarde). Compilación usada:

```
set JAVA_HOME=D:\Tools\JDK21
set ANDROID_HOME=D:\Tools\android-sdk
set GRADLE_USER_HOME=D:\Tools\gradle-home
cd appcompanion\DnxCompanion
D:\Tools\gradle-8.14.6\bin\gradle.bat assembleDebug --no-daemon
```

APK: `app/build/outputs/apk/debug/app-debug.apk` (~9,1 MB).

> Nota: esta máquina tiene poco *commit* de memoria (pagefile pequeño). El
> build solo pasa con `gradle.properties` recortado (`-Xmx1100m`,
> `workers.max=1`, Kotlin `in-process`).

## 8. Estado de implementación (2026-10-27)

- [x] Plan y contrato.
- [x] **F1**: `lan_server` (C++): `/api/info`, `/api/library`, `/dl/<key>` con
      Range + `Content-Disposition`. **Verificado con curl**: info devuelve
      `items`, `/api/library` devuelve ítems y `/dl/<key>` responde 200 con
      `Content-Length: 1984520` y 206 al pedir `Range: bytes=0-99`.
      (El servidor arranca en el proceso **renderer**, que es donde vive el
      catálogo de plugins; en el browser estaba vacío.)
- [x] **F2**: QR en la ficha del escritorio: librería `qrcode.min.js` (MIT) en
      `dist/ui/dist/scripts/`, método de bridge `getLanInfo` (IP local + puerto)
      y sección "Enviar al móvil" con el QR + URL. Verificado: el QR se pinta en
      el panel y la URL responde (200, video/mp4).
- [x] **F3**: app compilada (`app-debug.apk`, 9,1 MB): QR, comprobar, descargar
      y **poner fondo animado de vídeo** (`VideoWallpaperService`).
- [ ] **F4-F6** (ver §9).

---

## 9. Actualización v3 (2026-10-28)

Decisiones cerradas para el companion:

- **Editor de dispositivos externo** (device-export.exe): adapta el wallpaper
  al aspect del dispositivo y genera el fichero optimizado. Especificación en
  docs/device-editor.md. La app consume sus exportaciones.
- **Salida**: 
es/mobile/<id>.mp4 + exports.json; URL /m/<id> y metadatos
  en /api/mobile/<id>. El QR se genera solo en el editor (fuera del preview).
- **Encuadre**: cover por defecto (recorte, sin deformar); contain con bandas
  o fondo desenfocado; stretch solo como opción explícita.
- **Resolución objetivo**: tope del lado corto (720/1080/1440/2160/custom;
  default 720; persiste). Si el recorte es menor, no se upscala en el editor.
- **Cuadrado universal**: preset opcional (lado largo) para reutilizar el mismo
  master en vertical/horizontal y para parallax. Off por defecto.
- **Fade loop**: variante A (rotación), duración configurable; loop exacto sin
  salto.
- **HDR**: detección + tonemap a SDR en la exportación móvil.
- **App**: biblioteca local con índice JSON (sin Room), dedupe por SHA-256,
  aplicar directo si ya está descargado, borrar para redescargar, solo imagen y
  vídeo, fit/cover/stretch con renderer GL, parallax on/off por wallpaper (solo
  master cuadrado), WebP animado en el grid con límites, minSdk 26 con fallback.
- **Subida al PC**: POST /api/upload con token y límite; guardar en
  res/imported/, generar thumb y refrescar la biblioteca del escritorio.
- **Descubrimiento**: UDP en el puerto 7884 (o el que se defina) para encontrar
  el PC; el QR sigue siendo el pareado rápido.
- **Catálogo**: snapshot res/cache/catalog.json para que /api/library
  responda al instante aunque el PC acabe de arrancar.

### Fases revisadas

| Fase | Alcance | Semanas |
|---|---|---|
| F4 | Biblioteca local + aplicar/eliminar/info + WebP animado | 2–3 |
| F5 | Subida al PC + descubrimiento UDP + streaming de preview | 2 |
| F6 | Renderer GL (fit/cover/stretch + parallax) | 1 (fit hecho; parallax pendiente) |

> El renderer GL se adelanta a la Fase 0 del plan general para quitar el
> estiramiento actual; la biblioteca y la subida van después del editor.

### Estado

- [x] F1: servidor LAN (/api/info, /api/library, /api/thumb, /dl).
- [x] F2: QR en la ficha del escritorio + getLanInfo + /dl bajo demanda.
- [x] F3: APK con QR vertical, descarga con progreso, imagen y vídeo animado
      (ExoPlayer + proxy 1080p).
- [x] /api/item/<key> (metadatos sin descargar) y HEAD sin descarga.
- [x] F6 (parcial): renderer GL con `cover`/`fit`/`stretch` y selector en la
      app. Probado en emulador. Falta fit por wallpaper y parallax.
- [x] **F4 (parcial)**: biblioteca del PC en la app: `GET /api/library`
      paginada (24 por página) con miniaturas `/api/thumb/<workshopid>`, botón
      "Cargar biblioteca"/"Recargar"/"Más" y toque para cargar la ficha (usa
      `/dl/<key>`). La base del PC se recuerda en preferencias tras abrir
      cualquier QR/URL. APK msg 20.
- [x] **F5 (parcial)**: descubrimiento del PC por UDP 7884
      (`WPX/Ping` → `{"app","name","port","version"}`; hilo `DiscoveryLoop` en
      `lan_server.cpp`, arrancado con el servidor). En la app, botón
      "Buscar PC": rellena la base, la guarda y carga la biblioteca. APK msg 21.
      (En el emulador el broadcast no sale del NAT: probar en móvil real.)
- [ ] F5 (subida de vídeos del móvil al PC).

## 7. Gestión del fondo activo (2026-10-28)

- **Quitar fondo**: botón `Quitar fondo (restaurar predeterminado)` →
  `WallpaperManager.clear()`. Android no tiene estado «sin fondo»: se aplica el
  fondo por defecto del sistema. Si tras `clear()` sigue activo un fondo
  animado (`wallpaperInfo != null`), se aplica un bitmap estático neutro para
  desactivarlo. Verificado en emulador (API 36): vuelve a `ImageWallpaper`.
- **Fondo anterior** (`WallpaperKeep.kt`):
  - El fondo se copia **antes** de aplicar uno nuevo: `current.mp4` →
    `prev_video.mp4` cuando el fondo activo es `VideoWallpaperService`; si es
    estático, se guarda en `prev_static.png`.
  - Botones `Guardar actual` y `Restaurar`.
  - Límites: los fondos animados de otras apps no son legibles. En Android 13+
    el sistema deniega `READ_EXTERNAL_STORAGE` en silencio (`getDrawable`
    lanza `SecurityException`), así que al pulsar `Guardar actual` la app abre
    el selector de imágenes: el usuario elige la imagen que tenía de fondo y esa
    pasa a ser el respaldo (`prev_static.png`). En Android ≤ 12, con
    `READ_EXTERNAL_STORAGE` concedido, la copia es automática.
  - Sólo se restaura la copia **más reciente** (vídeo o imagen) y la etiqueta
    muestra tipo y fecha (`Guardado: imagen · 28/10 19:28`).

## 8. Rediseño de la app (2026-10-28, APK msg 22)

Estructura nueva en tres pestañas (`MainActivity` + `ui/Screens.kt`):

| Pestaña | Contenido |
|---|---|
| **Wallpapers** | Buscador, filtros (Todo / Vídeos / Imágenes / Vertical), cuadrícula de la biblioteca del PC (24/página) y hoja de detalle con "Aplicar versión móvil (1080p)", "Aplicar original" y "Guardar en Descargas". |
| **Mi fondo** | Elemento actual, encuadre (recortar/ajustar/estirar), fondo anterior (guardar/restaurar), elegir vídeo local y quitar fondo. |
| **Ajustes** | Conexión con el PC (buscar, QR, pegar, dirección manual) y "Acerca de" con el avatar del proyecto. |

- Estado centralizado en `CompanionState` (conexión, biblioteca, selección,
  descargas/aplicar, copia de seguridad).
- Red/descarga en `CompanionApi.kt` (`loadItem`, `loadLibrary`, `discoverPc`,
  `download`, `copyLocalVideo`, `applyImage`, `enqueueDownload`...).
- Tema oscuro propio (`ui/DnxTheme.kt`) alineado con el editor.
- Progreso por fases en la barra inferior: "Guardando copia del fondo
  anterior…" (indeterminado), descarga con porcentaje y "Aplicando…".
- La biblioteca se carga sola al abrir si hay PC recordado.
- Iconos generados desde `resources/` con `tools/sync_icons.py`
  (EXE, bandeja, editor y APK); el editor usa `Mobile.png` como logo y favicon.

## 9. Rediseño v2 (2026-10-29, APK msg 23)

Modelo acordado con el usuario:

| Sección | Contenido |
|---|---|
| **Catálogo local** | Ficheros internos (`filesDir/wallpapers`) + "Añadir wallpaper" (vídeo o imagen). Miniaturas propias, tamaño, favorito (★), fijar (orden), borrar. |
| **Catálogo remoto** | Catálogo del PC con filtros y buscador. Todo se descarga al catálogo local. |
| **Favoritos** | Subconjunto marcado como favorito; fijar solo afecta al orden. |
| **Ajustes** | Conexión, encuadre de vídeo, quitar fondo, tope de tamaño y "Acerca de" (icono de la suite + icono Android). |

### Editor de encuadre (móvil)
- La **pantalla del móvil es el viewport**: el preview se dibuja con la relación
  de aspecto real del dispositivo (no la de la caja del componente).
- Zoom (0.2×–6×) y paneo; al alejar por debajo de "cover" aparecen bandas con
  color elegible (negro/gris/azul). Botón **Ajustar** vuelve a rellenar.
- "Generar para mi móvil" envía al PC `{key, rect, target= lado corto del
  móvil (≤1080), quality, fade, maxSizeMB, padColor}` a `/api/editor/export`.
- El PC recorta, escala y **rellena** con `pad` cuando el rect sale del vídeo;
  el tope de tamaño se aplica como **bitrate** (`maxSizeMB*8*0.92/duración`), en
  una sola pasada (sin re-codificar por pasos).
- Al terminar, la app descarga `/m/<id>` y lo añade al catálogo local con la
  marca "ópt".

### Servidor
- `/api/editor/export` acepta `key` (clave del catálogo) además de `file`; el
  servidor resuelve la ruta (la LAN no puede enviar rutas de disco).
- `maxSizeMB`, `padColor` y rect fuera de [0,1] soportados.
- Windows: icono de la ventana principal forzado con `WM_SETICON` (CEF ponía
  el de `libcef.dll`).

### Pendiente
- Aviso al añadir ficheros locales grandes (> tope configurado).
- Validar "versión móvil" y original desde el catálogo remoto.
- Reordenar por "fijado" en favoritos (ya aplica el orden).
