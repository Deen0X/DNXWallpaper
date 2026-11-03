# MoeWalls (v3)

Vídeos de **moewalls.com**. Scraping de listados y del token `data-url` del
detalle para descargar el MP4 directo.

## Proveedor y límites

- Listados: home `/`, categorías `/category/<cat>/`, resoluciones
  `/resolution/<res>/`, tags `/tag/<tag>/` y búsqueda `/?s=<texto>`.
- 16 tarjetas por página; paginación `/page/N/`.
- Detalle: `/<categoria>/<slug>/`; contiene `og:image`, un preview WebM en
  `/wp-content/uploads/preview/<año>/<base>-preview.webm`, resolución, tamaño y
  un token `data-url`.
- Descarga: `https://go.moewalls.com/download.php?video=<token>` devuelve un
  **MP4 directo 4K** (sin Referer).
- Límite orientativo declarado: 20 peticiones/minuto, burst 3.

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 10
python main.py list --category anime --limit 10
python main.py list --query 4k --limit 10
python main.py detail --id "mw-anime@ichigo-bankai-blood-chains-eclipse-live-wallpaper"
python main.py thumb --id "mw-anime@..." --out tmp.jpg --size 480
python main.py thumb --id "mw-anime@..." --out tmp.webp --animated
python main.py fetch --id "mw-anime@..." --out video.mp4 --max-bytes 50000000
```

## Identificadores

`id = "mw-<categoria>@<slug>"` (por ejemplo
`mw-anime@ichigo-bankai-blood-chains-eclipse-live-wallpaper`). Es estable y
`detail` reconstruye la URL original.

## Orden y paginación

- Solo existe el orden de novedad del sitio. `newest` se declara soportado;
  `popular`/`relevance` devuelven el orden del sitio con `"sorted": false`.
- El total se calcula con la última página de la paginación y el número de
  tarjetas.
- `--query` acepta alias de resolución (`4k`, `1080p`, `1440p`, `ultrawide`...)
  que se traducen a la categoría `/resolution/.../`.
- `nextCursor` codifica el offset; `--cursor` tiene prioridad sobre `--page`.

## Miniaturas y descargas

- `thumb` estática extrae un fotograma del preview WebM (conserva la
  proporción real). `og:image` 1920×1080 queda como respaldo y el ítem se marca
  `staticCropped` si no es 16:9.
- `thumb --animated` usa el preview WebM y genera un WebP animado.
- `fetch` descarga el MP4 del token (`media`) o el preview WebM
  (`--kind preview`). Devuelve `width`, `height` y `durationSec` (ffprobe).
- Para `--limit <= 12` la lista enriquece cada tarjeta con su detalle para que
  `media.url` sea ya el MP4 y los metadatos sean completos. Para límites
  mayores usa el preview WebM predecible desde el thumbnail (evita 200
  peticiones de detalle).
- Respeta `--max-bytes` y no deja `.part`.

## Configuración

No requiere configuración.

## Miniaturas sin recorte

- `thumb` no recorta: escala conservando la proporción real y limita el lado
  mayor a `--size` (640 px por defecto en estática, 320 px en animada).
- `thumbs.staticUrl` / `thumbs.animatedUrl` usan la variante del proveedor que
  no recorta cuando existe. Si el proveedor solo ofrece un póster recortado, el
  ítem incluye `thumbs.staticCropped: true` (y `animatedCropped` si aplica) y
  `thumb` genera la miniatura correcta desde el vídeo/imagen original.
- `width` / `height` del ítem son los del medio real, no los de la miniatura.
- La caché de miniaturas usa una clave con `-nocrop-v2`, así que las
  miniaturas antiguas recortadas no se reutilizan.

## Modo `serve` (protocolo 3)

`python main.py serve --config <config.json> [--cache-dir <dir>]` abre un
proceso persistente. La primera línea es
`{"id":0,"ok":true,"event":"ready","protocol":3}`. Después acepta una petición
JSON por línea (`{"id":1,"cmd":"health","args":{}}`) y responde con el mismo
JSON del comando más el campo `id`. `shutdown` cierra el proceso; EOF también.

## Cambios respecto a v1

- `health` no sale a la red.
- `fetch` escribe en `--out` y declara metadatos del vídeo.
- Se añaden `--sort`, `--cursor`, `nextCursor` y el recorte a 200.
- `--animated false` devuelve una lista vacía (la fuente solo tiene vídeo).

