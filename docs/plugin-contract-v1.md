# Contrato de plugins de catálogo — v1

Objetivo: alimentar el catálogo de la aplicación (galería "Descubrir" y
búsqueda) con wallpapers de fuentes externas, sin acoplar la app a ninguna
web concreta.

Un plugin es un **ejecutable de línea de comandos** (cualquier lenguaje) que
implementa este contrato y se coloca en `dist\plugins\<plugin-id>\`. La app lo
invoca con argumentos, lee JSON por `stdout` y descarga archivos a rutas que
la app indica.

---

## 1. Estructura del plugin

```
dist\plugins\<plugin-id>\
  plugin.json          # manifiesto (obligatorio)
  plugin.exe           # ejecutable (o plugin.cmd / plugin.py con runtime)
```

### 1.1 `plugin.json`

```json
{
  "schemaVersion": 1,
  "id": "mi-sitio",
  "name": "Mi Sitio de Wallpapers",
  "version": "1.0.0",
  "description": "Proveedor de ejemplo",
  "author": "autor",
  "license": "MIT",
  "homepage": "https://mi-sitio.example/",
  "entry": "plugin.exe",
  "icon": "icon.png",
  "capabilities": ["list", "categories", "detail", "thumb", "fetch", "config"],
  "mediaTypes": ["video", "image"],
  "requiresAuth": false,
  "config": {
    "version": 1,
    "mode": "form",
    "fields": [
      { "key": "apiKey", "label": "API key", "type": "secret", "required": true }
    ]
  },
  "rateLimit": { "requestsPerMinute": 30, "burst": 5 },
  "attribution": "Contenido de Mi Sitio. Uso permitido según sus términos.",
  "termsUrl": "https://mi-sitio.example/terms",
  "notes": "El plugin respeta robots.txt y no accede a contenido con login."
}
```

- `id`: `[a-z0-9-]+`, único.
- `entry`: ruta relativa al ejecutable.
- Si `requiresAuth` es `true`, el plugin debe devolver `AUTH_REQUIRED` cuando
  falte credencial; la app mostrará un aviso (v1 no gestiona logins).
- **`icon` (opcional)**: ruta relativa a la carpeta del plugin con su icono.
  - Si falta o no se puede leer → la app usa el icono del ejecutable y, si no
    hay, un icono genérico.
  - Debe quedar **dentro** de la carpeta del plugin (sin `..`).
  - Formatos: **PNG** recomendado (256×256 o 512×512, RGBA). Se permite ICO
    multi-tamaño. SVG solo si la app lo soporta (no exigirlo).
- **`config` (opcional)**: describe si el plugin necesita configuración y cómo
  editarla.
  - `mode: "external"` (por defecto): el plugin abre su propia ventana o
    prompt de configuración (el host solo lanza `config`).
  - `mode: "form"`: la app PUEDE renderizar `fields` como formulario nativo.
    Si no lo implementa, debe comportarse como `external` (lanzar `config`).
  - `fields[].type`: `"text" | "secret" | "number" | "bool" | "path" | "select"`
    (`select` admite `options: [{ "value", "label" }]`).
  - `required: true` indica que, si falta, el plugin debe responder
    `CONFIG_REQUIRED` con la lista `missing` (§6).

---

## 2. Protocolo de invocación

- Argumentos en ASCII/UTF-8. Respuesta: **un único objeto JSON por `stdout`**.
- `stderr`: solo logs humanos (la app los guarda, no los parsea).
- Códigos de salida: `0` éxito, `2` error de uso, `3` error de red,
  `4` no encontrado, `5` requiere autenticación **o configuración**
  (`AUTH_REQUIRED` / `CONFIG_REQUIRED`), `1` error interno.
- La app impone timeout (por defecto 20 s; descargas 120 s).
- Los mensajes de error SIEMPRE en JSON (ver §6).

### 2.0 Resolución de configuración

Cualquier comando debe localizar su configuración en este orden:

1. argumento `--config <ruta>`,
2. variable de entorno `DNX_PLUGIN_CONFIG`,
3. `config.json` junto al ejecutable del plugin.

Si una operación necesita un campo requerido y no está disponible, el plugin
responde `CONFIG_REQUIRED` (§6) sin ejecutar la operación.

### 2.1 Comandos

#### `config`
Abre la interfaz de configuración del plugin (ventana propia o prompt),
escribe el fichero de configuración y responde al cerrar.

```
plugin.exe config --config "<ruta>" [--parent-hwnd <n>]
```

- `--config`: ruta donde guardar/leer la configuración (obligatorio).
- `--parent-hwnd`: opcional; permite mostrar la ventana como modal centrada
  sobre la ventana de la app (identificador numérico HWND).
- La apariencia de la UI **no forma parte del contrato**: solo el dato
  (`config.json`), cómo se localiza y cómo se lanza su editor.

Respuesta:

```json
{ "ok": true, "config": "<ruta escrita>" }
```

Errores posibles: `BAD_ARGS`, `INTERNAL` (y `CONFIG_REQUIRED` si el usuario
cancela sin completar campos requeridos).

#### `categories`
Devuelve categorías/géneros normalizados (puede ser estático).

```json
{ "ok": true, "categories": [ { "id": "nature", "name": "Naturaleza" } ] }
```

#### `list`
Listado con filtros, orden y paginación.

```
plugin.exe list --query "space" --category nature --tags "4k,loop" \
  --sort newest --page 1 --limit 40 --animated true --min-rating 4
```

Parámetros (todos opcionales):
- `--query` texto libre
- `--category` id de categoría
- `--tags` lista separada por comas (AND)
- `--sort` uno de: `newest|oldest|rating|popular|title`
- `--page` (1-based), `--limit` (1..100)
- `--animated` `true|false`
- `--min-rating` 0..5
- `--since` / `--until` ISO-8601 (por `publishedAt`)

Respuesta:

```json
{
  "ok": true,
  "page": 1,
  "limit": 40,
  "total": 1234,
  "hasMore": true,
  "items": [ { /* Item, §3 */ } ]
}
```

#### `detail`
`plugin.exe detail --id <item-id>` → `{ "ok": true, "item": { … } }`

#### `thumb`
Genera/entrega una miniatura en la ruta indicada por la app.

```
plugin.exe thumb --id <id> --kind static|animated --out "C:\...\thumb.webp"
```

- `static`: escribe la imagen tal cual (png/jpg/webp). Si el sitio solo tiene
  miniatura estática, devuélvela.
- `animated`: si el sitio ofrece un clip/GIF/WebM de preview, descárgalo y
  **conviértelo** (ver §5). Si no existe, devuelve `{ "ok": false, "code":
  "ANIMATED_UNAVAILABLE" }` para que la app aplique su estrategia (§5).
- Respuesta: `{ "ok": true, "path": "<ruta escrita>", "mime": "image/webp" }`

#### `fetch`
Descarga el medio completo (imagen o vídeo) a la ruta indicada.

```
plugin.exe fetch --id <id> --out "C:\...\media.mp4" [--kind media|preview]
```

- `media`: archivo final reproducible por Media Foundation (`mp4/h264`,
  `webm`, `mkv`, `avi`, `jpg/png/webp`).
- `preview`: clip corto si el sitio lo ofrece (opcional).
- Respuesta: `{ "ok": true, "path": "...", "bytes": 123456,
  "sha256": "…", "contentType": "video/mp4" }`

#### `health`
`{ "ok": true, "version": "1.0.0", "latencyMs": 120 }` — comprobación rápida.

---

## 3. Esquema `Item`

```json
{
  "id": "provider-unico-123",
  "title": "Nebulosa Azul",
  "description": "texto opcional",
  "pageUrl": "https://mi-sitio.example/wallpaper/123",
  "publishedAt": "2026-09-14T10:00:00Z",
  "updatedAt": "2026-09-20T08:00:00Z",
  "categories": ["nature"],
  "tags": ["space", "4k", "loop"],
  "author": { "name": "autor", "url": "https://..." },
  "rating": { "average": 4.6, "count": 128 },
  "popularity": 15230,
  "animated": true,
  "mediaType": "video",
  "durationSeconds": 12.5,
  "resolution": { "width": 1920, "height": 1080 },
  "sizeBytes": 24911112,
  "media": { "type": "video", "url": "https://cdn.../file.mp4",
             "contentType": "video/mp4", "sizeBytes": 24911112 },
  "thumbs": {
    "staticUrl": "https://cdn.../thumb.jpg",
    "animatedUrl": "https://cdn.../preview.webm"
  },
  "attribution": "© autor — Mi Sitio",
  "license": "personal-use",
  "downloadable": true
}
```

Reglas:
- `id` estable y único **dentro del plugin** (la app lo prefija con el id del
  plugin).
- `publishedAt`/`updatedAt` en ISO-8601 UTC (si no hay fecha, omitir).
- `rating` solo si el sitio lo publica; si no, omitir el campo.
- `animated` = existe animación (aunque no esté descargada).
- `downloadable=false` para ítems que solo se pueden ver en su web.
- No inventar datos: campos desconocidos se omiten.

---

## 4. Almacenamiento y caché (lado app)

```
dist\res\providers\<plugin-id>\
  index.json           # último listado (TTL configurable)
  thumbs\<itemId>.jpg|webp
  media\<itemId>.<ext>
  tmp\                 # descargas temporales (se borran)
```

- Las miniaturas y medios descargados se cachean; la app no vuelve a pedirlos
  salvo expiración o borrado manual.
- Descargas a `tmp` y movimiento atómico al destino final.
- Límite de concurrencia: 2 descargas simultáneas por plugin (la app lo
  impone; el plugin debe tolerarlo).

---

## 5. Miniaturas animadas (regla obligatoria)

1. El plugin intenta `thumbs.animatedUrl` (webm/gif/mp4 de preview).
2. Si existe, descarga y convierte a **WebP animado** (o GIF) con este perfil:
   - 320×180, 10 fps, máx. 6 s, bucle infinito, sin audio.
   - `ffmpeg -i in -t 6 -vf "fps=10,scale=320:-2:flags=lanczos" -an -loop 0 out.webp`
3. Si no existe `animatedUrl` pero el ítem es `animated` y `downloadable`:
   - descarga temporal del `media`, genera el WebP, **borra el temporal** y
     conserva solo la miniatura.
4. Si nada es posible: devolver `ANIMATED_UNAVAILABLE`.

La app mostrará **badge "Animado"** cuando `animated=true` y aún no exista
medio descargado; no reproduce animación en la UI hasta tener el archivo
local (con miniatura animada cacheada sí se anima la miniatura).

---

## 6. Errores

```json
{ "ok": false, "code": "RATE_LIMITED", "message": "429 del servidor",
  "retryAfterSeconds": 30 }
```

Códigos: `BAD_ARGS`, `NOT_FOUND`, `NETWORK`, `TIMEOUT`, `RATE_LIMITED`,
`AUTH_REQUIRED`, `CONFIG_REQUIRED`, `UNSUPPORTED`, `ANIMATED_UNAVAILABLE`,
`INTERNAL`.

### `CONFIG_REQUIRED`

Cuando una operación necesita configuración ausente o incompleta:

```json
{ "ok": false, "code": "CONFIG_REQUIRED",
  "missing": ["apiKey"],
  "message": "Falta la configuración del plugin" }
```

- `missing`: lista de `fields[].key` que faltan (puede ser vacía si el usuario
  canceló).
- Exit code: **5** (compartido con `AUTH_REQUIRED`).

---

## 7. Cumplimiento y límites

- Respetar `robots.txt`, términos de uso y derechos de autor.
- No eludir Cloudflare, logins, paywalls ni protecciones técnicas.
- Identificarse con un `User-Agent` honesto que incluya el `id` del plugin.
- Caché obligatoria del lado del plugin para no martillear el sitio
  (mínimo: listados 10 min, detalle 1 h, miniaturas 7 días).
- Nada de credenciales embebidas en el ejecutable.

---

## 8. Adaptador a la app (referencia)

La app transformará cada `Item` en su formato interno de galería:

```json
{
  "file": "<ruta local tras fetch>",
  "title": "<title>",
  "type": "video|image",
  "official": false,
  "local": true,
  "status": "installed|downloadable",
  "preview": "<url miniaturas cacheada>",
  "previewsmall": "<igual>",
  "provider": "<plugin-id>",
  "remoteId": "<id>",
  "animated": true,
  "publishedAt": "...",
  "rating": 4.6
}
```

`status="downloadable"` mientras no exista el archivo local; al pulsar OK la
app invoca `fetch` y pasa a `installed`.

---

## 9. Responsabilidades del host (app) — configuración

Al recibir `CONFIG_REQUIRED` en cualquier comando:

1. Mostrar una acción **"Configurar"** junto al error del plugin.
2. Al pulsarla, ejecutar:
   ```
   <entry> config --config "<ruta-de-config>" --parent-hwnd <hwnd-de-la-app>
   ```
   como ventana **modal centrada sobre la app** (el plugin decide la UI; el
   host solo pasa el HWND y espera a que el proceso termine).
3. Si `config.mode = "form"` y el host implementa formularios, PUEDE renderizar
   `fields` en su propia UI (DNX UI) y escribir `config.json` directamente;
   si no, se comporta como `external`.
4. Reevaluar la configuración y **reintentar la operación** que falló
   (list/detail/thumb/fetch) al cerrar el editor.
5. Añadir `CONFIG_REQUIRED` al catálogo de errores y al mapeo de exit code 5.

Ruta de configuración recomendada por la app:
`res\providers\<plugin-id>\config.json`, pasada siempre con `--config`.

Notas:
- El contrato **no define la apariencia** de la UI de configuración.
- Los comandos `list/detail/thumb/fetch/health` y el adaptador §8 no cambian.
- Los ítems estáticos se tratan igual que los animados en la galería
  (`type="image"`).

---

## Nota de implementación (app DNX, 2026-10-26)

La app ya cachea miniaturas por su cuenta y **no invoca** `thumb` todavía:

- `thumbs.staticUrl` se descarga directo por HTTPS a
  `res\cache\thumbs\remote\<plugin>\<id>.<ext>`.
- `thumbs.animatedUrl` (mp4/webm/gif/webp) se descarga y se convierte a WebP
  animado con ffmpeg (mismo perfil del §5). Sólo entonces se usa como
  `previewsmall`.
- Los `id` con `/` se sanean a `_` en el nombre de archivo de la miniatura.
- El comando `thumb` sigue siendo válido y se usará como fallback para sitios
  sin `animatedUrl` (p. ej. generar la animada a partir de un clip temporal).

---

## 10. v1.1 — tipos de plugin (2026-10-28)

La UI nueva (DNXWallpaper v3) necesita distinguir tres tipos de plugin. El
contrato de catálogo (§1–§9) se mantiene; se añade metadata.

### 10.1 `kind`

```json
{ "schemaVersion": 1, "id": "local", "kind": "catalog" }
```

| kind | Implementación | Ejemplos |
|---|---|---|
| `catalog` | CLI + JSON (contrato v1) | Local (built-in), Telegram, Pixabay, Wallhaven |
| `wallpaper` | Nativo o helper con textura compartida | Vídeo/imagen (built-in), Web (helper CEF), Aplicación (WGC) |
| `extras` | Proveedor de datos + widgets declarativos | Ping, RTSS, HWiNFO, reloj, clima |

- Los `catalog` son los únicos que usan el protocolo CLI de este documento.
- Los `wallpaper` y `extras` necesitan acceso por frame o datos de sistema; se
  implementan como módulos nativos o procesos helper con un contrato propio.
  No se cargan binarios de terceros sin sandbox/allowlist.
- Si `kind` falta, se asume `catalog` (compatibilidad).

### 10.2 Configuración por formulario

`config.mode = "form"` pasa a ser el modo recomendado para la UI nueva. Se
amplían los tipos de campo:

- `text`, `secret`, `number`, `bool`, `select` (ya existían).
- `path`: una ruta (selector de carpeta/fichero).
- `paths`: lista de rutas (carpetas de búsqueda del plugin local).
- `url`: URL validada.
- `folder`: carpeta existente.

Reglas:
- La UI renderiza Settings Cards y escribe `config.json`; el plugin lo lee por
  `--config` (prioridad ya definida en §2.0).
- `config.mode = "external"` sigue siendo válido: la app lanza
  `<entry> config --config <ruta> --parent-hwnd <hwnd>`.
- `CONFIG_REQUIRED` sigue igual; la UI muestra “Configurar” y reintenta.

### 10.3 Plugin local

- `kind: catalog`, `builtin: true`, `id: local`.
- Sustituye al plugin `mock`.
- Configuración: `paths` (carpetas), `recursive` (bool), `extensions` (texto),
  `includeDownloads` (bool), `autoRefresh` (bool), `exclude` (texto).
- `list/detail/thumb/fetch` pueden ser nativos (recomendado) para no lanzar un
  proceso por escaneo; el manifiesto y la configuración son los mismos.

### 10.4 Wallpaper plugins (built-in/helper)

Metadata propuesta:

```json
{
  "kind": "wallpaper",
  "builtin": true,
  "render": "web",
  "requires": ["cef"],
  "config": { "mode": "form", "fields": [] }
}
```

- `render`: `video|image|web|application`.
- `requires`: `cef`, `wgc`, `rtss`, `hwinfo`, `admin` (solo informativo; la UI
  avisa si falta).
- El host compone la textura; el plugin solo aporta configuración y, si aplica,
  un helper.

### 10.5 Extras plugins (overlays)

Metadata propuesta:

```json
{
  "kind": "extras",
  "providers": ["ping", "rtss", "clock"],
  "widgets": ["text", "bar", "sparkline", "icon"]
}
```

- El host pinta con DirectWrite sobre la capa del wallpaper (1 Hz, cacheado).
- Sin scripts arbitrarios: widgets declarativos + proveedores nativos/allowlist.
- Configuración: fuente de datos, posición, tamaño, color, opacidad, fondo.
- Se pintan debajo de los iconos del escritorio (parte del fondo), no son
  clicables.

### 10.6 Compatibilidad

- Los plugins de catálogo actuales siguen funcionando sin cambios.
- La UI vieja ignora `kind`; la nueva lo usa para agrupar y configurar.
- El `mock` se retira cuando el plugin local esté operativo.

