# Plan de implementación — servicio externalizable

Estado: en curso. Última actualización: 2026-10-29.

## Fase 0 — Contrato del servidor ✅

Objetivo: que el servidor sea identificable, direccionable y arrancable sin
depender de la UI.

- [x] `GET /api/info` devuelve `product`, `instanceId`, `name`, `port`,
      `auth`, `items` e `interfaces`.
- [x] `GET /api/net/interfaces` (interfaces IPv4 con puerta de enlace, marca de
      principal y de virtual).
- [x] Selección de puerto con reintento (`8477` → `+30`).
- [x] `SO_EXCLUSIVEADDRUSE` (en Windows `SO_REUSEADDR` permite que dos procesos
      escuchen el mismo puerto; aquí queremos lo contrario).
- [x] Persistencia del puerto elegido en `res/config/server.json` para que el
      QR y los clientes emparejados no queden obsoletos.
- [x] `instanceId` estable (GUID) persistido junto al puerto.
- [x] Firma en el descubrimiento UDP (`product`).

Pendiente de esta fase:

- [ ] Adjuntarse a una instancia existente en vez de arrancar otra (requiere que
      la UI pueda apuntar el `fetch` de `/api/*` a un origen externo).
- [ ] Flags de línea de comandos `--port`, `--data-dir`, `--no-ui` para que el
      binario sea utilizable como servicio y en contenedor.

## Fase 1 — UI web: conexión y catálogo ✅ (parcial)

- [x] Sección **Conexión**: servidor en uso, estado de token, checkboxes de
      interfaces, QR (una URL o lista en JSON) y copiar dirección.
- [x] Contador de catálogo (“N cargados de M en el servidor”).
- [x] Placeholder para items sin miniatura (en vez de hueco vacío).
- [x] `/api/monitors` (EnumDisplayMonitors) y `/api/current`.
- [x] `/api/apply?key=` — resuelve el item, lo descarga si hace falta,
      escribe `res/current.json` y señaliza `dnxWallpaperV3Reload`.
- [x] Sección **Pantallas** real en `/ui2/` (resolución, wallpaper actual,
      botón “Elegir wallpaper…” → selector modal → aplicar).
- [ ] Asignación **por monitor** (el motor hoy aplica uno por equipo).
- [ ] Persistencia de salvapantallas y playlist.


## Fase 1b — Plugins v2 en el servidor ✅

Los 9 plugins migrados (`temp/plugins`) están integrados en `dist/plugins` y
funcionando con el contrato v2 (runtime Python).

Cambios necesarios en el host:

- [x] `runtime: "python"` en `plugin.json` → el host lanza
      `python main.py <cmd>` con `cwd` = carpeta del plugin.
- [x] Búsqueda del intérprete: `DNX_PYTHON` → `runtime\python\python.exe`
      (embebido) → `py -3` → `python.exe` del PATH.
- [x] `--cache-dir` por plugin en `res/cache/plugin-data/<id>`.
- [x] Invalidación de la caché de páginas al cambiar la versión del plugin
      (`res/cache/plugins/<id>/.version`).
- [x] Respaldo al `fetch` del plugin cuando la descarga directa falla
      (CDNs con 403 por falta de User-Agent/Referer).
- [x] Respaldo al `thumb` del plugin cuando no hay póster.
- [x] Hasta 6 miniaturas en paralelo y sin duplicados por item.

Resultado: 5.305 items (9 plugins + `local`), miniaturas 60/60 y aplicar
comprobado en pixabay, wikimedia y wallhaven.

Nota de migración: al pasar de v1 a v2 hay que borrar `res/cache/catalog.json`
y `res/cache/plugins/*` una vez, o los ids antiguos se mezclan con los nuevos.

## Fase 2 — Cliente de escritorio como cliente más

- [x] Subida desde la UI web: botón “Subir vídeo…” y arrastrar sobre el
      catálogo.
- [x] `persist=1` → el vídeo entra en el catálogo como provider `uploads`
      (media en `res/providers/uploads/media`, póster en la caché de thumbs).
- [x] `persist` ausente o `0` → efímero (comportamiento anterior del móvil).
- [x] `PruneProviders` conserva `uploads` (no es un plugin).
- [x] Verificado: subida, catálogo, `/api/thumb`, `/dl`, `/api/apply` y
      persistencia tras reiniciar.
- [ ] Store local (índice + miniaturas) en el escritorio.
- [ ] `showDirectoryPicker` para carpetas y diálogo nativo como último recurso.
- [ ] Marcar `persist` desde el companion Android.

## Fase 3 — Cache de transcodes

- [ ] Clave `(fuente, params)` → fichero de salida + metadatos.
- [ ] Limpieza por TTL y por tamaño máximo.
- [ ] `/api/editor/export` consulta la cache antes de transcodificar.

## Fase 4 — Empaquetado

- [ ] `Dockerfile` (binario + ffmpeg estático, `--data-dir /data`).
- [ ] `docker-compose.yml` con volumen.
- [ ] ffmpeg estático embebido en el instalador de Windows.

## Fase 5 — Salidas multiplataforma

- [x] Nivel 0/1/2 definidos (imagen / imagen animada / vídeo).
- [ ] Export animado WebP (APNG/GIF como variantes).
- [ ] Prueba real en SteamOS escritorio (Plasma 5.27: plugin "Animated Image").

## Fase 6 — Agentes por SO (no requisito)

- [ ] Linux X11 con libmpv.
- [ ] Plugin KDE Wayland (cubre SteamOS escritorio).
- [ ] Extensión GNOME Wayland.

## Notas de verificación

| Comprobación | Resultado |
|---|---|
| `/api/info` con firma y `instanceId` | ok (2026-10-29) |
| `/api/net/interfaces` | ok, Wi-Fi `192.168.88.139` marcada principal |
| Puerto ocupado → `8478` y persistido | ok |
| QR en `/ui2/` con la interfaz marcada | ok |
| Catálogo 5.811 items, 0 miniaturas rotas en la primera página | ok |
| `/api/monitors` → 1920×1080 primaria | ok |
| `/api/apply` → `current.json` + motor aplica (`apply: video OK`) | ok |
| Subida `persist=1` → `uploads:<id>` visible y reproducible tras reinicio | ok |
| Subida efímera → `up:<id>` (no aparece en el catálogo) | ok |
