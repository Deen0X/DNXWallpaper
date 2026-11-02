# Contrato de plugins v2 (servidor)

Estado: especificación para migrar los plugins actuales (`.exe`/PowerShell) al
nuevo diseño, donde **los plugins viven en el servidor**.

## 0. Qué cambia y qué no

**No cambia** el modelo de ejecución: un plugin sigue siendo un **proceso
externo** que recibe argumentos por línea de comandos y devuelve **un objeto
JSON por stdout**. Eso ya funciona y da aislamiento, timeouts y posibilidad de
escribirlo en cualquier lenguaje.

**Cambia:**

| Antes (v1, cliente Windows) | Ahora (v2, servidor) |
|---|---|
| Runtime PowerShell / `.exe` | Python 3.11 (intérprete embebido del servidor) |
| Rutas del disco del usuario | El plugin corre **en el servidor**; solo ve su propio host |
| `fetch` devuelve una ruta local | `fetch --out <ruta>` **escribe** el fichero donde el servidor diga |
| Configuración con ventanas WinForms | `config` devuelve un **esquema JSON**; la UI lo pinta |
| `list` solo pagina | `list` pagina **y** ordena, con `cursor` recomendado |
| Miniaturas opcionales | `thumb` obligatorio (estático) y `--animated` deseable |
| Sin límites declarados | `rateLimit`, `net`, timeouts y tamaños declarados |

**El plugin `local` no se migra.** Su función (mis ficheros) pasa al cliente:
el usuario elige un fichero y lo sube. Un plugin de servidor no debe escanear
discos de usuarios.

## 1. Interfaz de proceso

```
python main.py <comando> [opciones]
```

- **stdout**: exactamente **una** línea JSON (el resultado). Nada más.
- **stderr**: logs, avisos, trazas. El servidor los guarda y no los parsea.
- **Código de salida**: `0` éxito; distinto de `0` error (ver §6).
- **Timeout**: 60 s por llamada normal, 300 s para `fetch`.
- **Sin estado entre llamadas**: cada invocación es independiente. La caché va
  a disco si hace falta, pero nunca en memoria de proceso.

Regla de oro: si escribes algo en stdout que no sea el JSON, el plugin se
considera roto. Un `print()` de depuración es el fallo más común.

## 2. Manifiesto `plugin.json` (schemaVersion 2)

```json
{
  "schemaVersion": 2,
  "id": "wallhaven",
  "name": "Wallhaven",
  "version": "2.0.0",
  "description": "Wallpapers de wallhaven.cc",
  "author": "DNXWallpaper",
  "license": "personal-use",
  "runtime": "python",
  "entry": "main.py",
  "capabilities": ["health", "categories", "list", "detail", "thumb", "fetch", "config"],
  "mediaTypes": ["image", "video"],
  "net": ["wallhaven.cc", "th.wallhaven.cc"],
  "rateLimit": { "requestsPerMinute": 45, "burst": 5 },
  "config": {
    "version": 1,
    "fields": [
      { "key": "apiKey", "label": "API key", "type": "secret", "required": false },
      { "key": "purity", "label": "Contenido", "type": "select",
        "options": ["sfw", "sketchy"], "default": "sfw" }
    ]
  },
  "attribution": "Imágenes de wallhaven.cc",
  "redistributable": false
}
```

- `net`: dominios que el plugin puede contactar. El servidor puede bloquear el
  resto.
- `redistributable: false` → el servidor **no** guarda los medios en su
  catálogo permanente, solo caché temporal.
- `capabilities` debe ser honesto: si declaras `thumb`, tiene que funcionar.

## 3. Comandos

### 3.1 `health`

```json
{ "ok": true, "version": "2.0.0", "latencyMs": 42 }
```

Opcional: `"authRequired": true`, `"warnings": ["sin apiKey: límite reducido"]`.

Debe responder en < 2 s y **no** puede salir a la red.

### 3.2 `categories`

```json
{ "ok": true, "categories": [ { "id": "anime", "name": "Anime" } ] }
```

Puede devolver `[]`. El servidor usa los `id` en `list --category`.

### 3.3 `list`

```
list --page 1 --limit 100 --sort newest [--query texto] [--category id] [--cursor c]
```

```json
{
  "ok": true,
  "total": 12840,
  "hasMore": true,
  "nextCursor": "eyJvZmZzZXQiOjEwMH0",
  "items": [ ... ]
}
```

- `--sort`: `newest` (defecto), `popular`, `relevance`. Si no puedes ordenar,
  devuelve lo que tengas **y** `"sorted": false`.
- `total` es obligatorio si lo conoces; si no, `null`.
- **`nextCursor` es lo preferido.** La paginación por número se rompe cuando el
  catálogo cambia entre páginas (items repetidos o saltados). El servidor usa
  cursor si existe y cae a `page` si no.
- `--limit` máximo 200. Si te piden más, recorta y di la verdad en `limit`.
- Nunca descargues medios en `list`: solo metadatos.

### 3.4 `detail`

```
detail --id <id>
```

```json
{ "ok": true, "item": { ... } }
```

### 3.5 `thumb`

```
thumb --id <id> --out <ruta> [--size 480] [--animated]
```

```json
{ "ok": true, "path": "<la ruta que te dieron>", "mime": "image/jpeg", "width": 480, "height": 270 }
```

- Escribe el fichero en `--out` (el servidor decide la ruta).
- `--animated`: si puedes, genera un **WebP animado** (varios frames); si no,
  devuelve el estático y `"animated": false`.
- Si no hay miniatura posible: `{"ok": false, "code": "NO_THUMB"}` con salida 4.
  El servidor pintará el placeholder; **no** es un error fatal.

### 3.6 `fetch`

```
fetch --id <id> --out <ruta> [--max-bytes N]
```

```json
{ "ok": true, "path": "<ruta>", "bytes": 18342011, "contentType": "video/mp4",
  "width": 3840, "height": 2160, "durationSec": 18.4 }
```

- Descarga y **escribe** en `--out`.
- Respeta `--max-bytes`: si el medio lo supera, corta y devuelve
  `{"ok": false, "code": "TOO_LARGE", "bytes": N}`.
- `width/height/durationSec` son obligatorios para vídeo: el servidor decide el
  recorte sin abrir el fichero.

### 3.7 `config`

```
config              -> { "ok": true, "values": {...}, "fields": [...] }
config --set k=v    -> { "ok": true, "values": {...} }
```

- Devuelve **esquema + valores actuales**; la UI pinta el formulario.
- Tipos: `text`, `secret`, `number`, `bool`, `select`, `path`.
- Nunca abras ventanas. El servidor puede estar en un NAS sin escritorio.

## 4. Objeto `item`

```json
{
  "id": "wh-8k2m1p",
  "externalId": "8k2m1p",
  "title": "Neon alley",
  "mediaType": "video",
  "animated": true,
  "publishedAt": "2026-09-14T10:22:00Z",
  "sizeBytes": 18342011,
  "width": 3840,
  "height": 2160,
  "durationSec": 18.4,
  "fps": 60,
  "codec": "h264",
  "hdr": false,
  "license": "CC-BY-4.0",
  "attribution": "Autor: nadie",
  "sourceUrl": "https://wallhaven.cc/w/8k2m1p",
  "tags": ["neon", "city"],
  "media": { "type": "video", "url": "https://cdn.../8k2m1p.mp4", "contentType": "video/mp4" },
  "thumbs": { "staticUrl": "https://cdn.../8k2m1p.jpg", "animatedUrl": null }
}
```

Obligatorios: `id`, `title`, `mediaType`, `media.url`.
Recomendados: `width`, `height`, `sizeBytes`, `publishedAt`, `sourceUrl`,
`license`, `attribution`.

- `id` **estable**: hash del identificador canónico del proveedor. Nunca
  posicional ni aleatorio.
- `publishedAt` en ISO-8601 UTC con `Z`.
- Si el proveedor da URLs de miniatura, ponlas en `thumbs`; aun así implementa
  `thumb` como respaldo.

## 5. Rendimiento y cortesía

- Reutiliza conexión HTTP dentro de una misma llamada.
- Caché en disco del plugin: `--cache-dir` (te lo pasa el servidor) o
  `<plugin>/cache/`. TTL mínimo 5 min para listados.
- No paralelices más de 4 descargas.
- Respeta `rateLimit`. Un 429 del proveedor debe devolver
  `{"ok": false, "code": "RATE_LIMITED", "retryAfterSec": N}`.

## 6. Códigos de error

| code | exit | significado |
|---|---|---|
| `BAD_ARGS` | 2 | faltan opciones o son inválidas |
| `NOT_FOUND` | 4 | el id no existe |
| `NO_THUMB` | 4 | sin miniatura disponible (no fatal) |
| `CONFIG_REQUIRED` | 5 | falta configuración (apiKey, etc.) |
| `TOO_LARGE` | 6 | supera `--max-bytes` |
| `RATE_LIMITED` | 7 | límite del proveedor, con `retryAfterSec` |
| `NETWORK` | 8 | fallo de red/timeout |
| `PARSE` | 9 | el proveedor cambió el formato |
| `INTERNAL` | 1 | cualquier otra cosa |

Formato de error: `{"ok": false, "code": "...", "message": "..."}`.

## 7. Migración desde PowerShell

| Hoy | Ahora |
|---|---|
| `plugin.cmd <cmd>` | `python main.py <cmd>` |
| `Get-Arg $args '--page'` | `argparse` |
| `Out-Json $obj` | `json.dump(obj, sys.stdout)` + `\n` |
| `Write-Host` para depurar | `print(..., file=sys.stderr)` |
| `& $ffmpeg ... -o $out` | `subprocess.run([...], check=True)` |
| `Copy-Item $cache $out` | `shutil.copyfile(cache, out)` |
| `Get-ChildItem -Recurse` | **eliminar** (es el plugin `local`, no se migra) |
| `System.Windows.Forms` | **eliminar** (config por esquema) |
| `http://wpx.internal/__file/...` | URL real del proveedor o `fetch --out` |

## 8. Esqueleto

```python
#!/usr/bin/env python3
import argparse, json, sys

def emit(obj):
    json.dump(obj, sys.stdout, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write("\n")

def fail(code, message, exit_code=1):
    emit({"ok": False, "code": code, "message": message})
    sys.exit(exit_code)

def cmd_health(_a):
    return {"ok": True, "version": "2.0.0", "latencyMs": 1}

def cmd_list(a):
    items, total, cursor = fetch_page(a.page, a.limit, a.sort, a.query)
    return {"ok": True, "total": total, "nextCursor": cursor,
            "hasMore": cursor is not None, "items": items}

def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("health")
    s = sub.add_parser("list")
    s.add_argument("--page", type=int, default=1)
    s.add_argument("--limit", type=int, default=100)
    s.add_argument("--sort", default="newest")
    s.add_argument("--query", default="")
    s.add_argument("--category", default="")
    s.add_argument("--cursor", default="")
    a = p.parse_args()
    handler = {"health": cmd_health, "list": cmd_list}[a.cmd]
    emit(handler(a))

if __name__ == "__main__":
    main()
```

## 9. Criterios de aceptación

Un plugin está listo cuando pasa todo esto:

1. `python main.py health` responde en < 2 s sin red.
2. `list --page 1 --limit 5` devuelve 5 items válidos y `total`.
3. `list --page 2` (o `--cursor`) **no repite** ningún `id` de la página 1.
4. `detail --id <uno de la lista>` devuelve el mismo item.
5. `thumb --id ... --out tmp.jpg` crea un JPEG válido (> 1 KB).
6. `fetch --id ... --out tmp.mp4` crea el fichero y declara `width/height/durationSec`.
7. `fetch --max-bytes 1000` devuelve `TOO_LARGE` sin dejar basura.
8. Todas las llamadas: **una sola línea** en stdout.
9. Con `--limit 500` no explota: recorta a 200 y lo dice.
10. Sin red: devuelve `NETWORK`, no una traza de Python.

## 10. Qué NO hacer

- No escribir en stdout nada que no sea el JSON.
- No usar rutas absolutas del disco del usuario.
- No abrir ventanas ni pedir input interactivo.
- No devolver ids basados en la posición.
- No descargar medios en `list`.
- No guardar estado en memoria entre llamadas.
- No ignorar `--max-bytes`.
- No migrar el plugin `local`.
