# WallpaperWaifu (v2)

Vídeos de **wallpaperwaifu.com**. Scraping de listados y del token `data-url`
del detalle para descargar el MP4 directo.

## Proveedor y límites

- Listados: home `/`, categorías `/category/<cat>/`, tags `/tag/<tag>/` y
  búsqueda `/?s=<texto>`.
- 22 tarjetas por página; paginación `/page/N/`.
- Detalle: `/<categoria>/<slug>/`; contiene `og:image`, un preview WebM en
  `/wp-content/uploads/preview/<año>/<base>-preview.webm`, resolución, tamaño,
  `article:published_time` y un token `data-url`.
- Descarga: `https://go.wallpaperwaifu.com/download.php?video=<token>`.
  **Ahora exige `Referer` de la página de detalle**; sin él devuelve un HTML de
  error (`error-file`). El plugin lo envía.
- El MP4 es 3840×2160 (~72 MB en el primer ítem comprobado) y tarda ~80 s.
- Límite orientativo declarado: 20 peticiones/minuto, burst 3.

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 10
python main.py list --category anime --limit 10
python main.py list --query itachi --limit 5
python main.py detail --id "ww-anime@itachi-uchiha-crows-live-wallpaper-2777"
python main.py thumb --id "ww-anime@..." --out tmp.jpg --size 480
python main.py thumb --id "ww-anime@..." --out tmp.webp --animated
python main.py fetch --id "ww-anime@..." --out video.mp4 --max-bytes 90000000
```

## Identificadores

`id = "ww-<categoria>@<slug>"` (por ejemplo
`ww-anime@itachi-uchiha-crows-live-wallpaper-2777`). Es estable.

## Orden y paginación

- Solo existe el orden de novedad del sitio. `newest` se declara soportado;
  `popular`/`relevance` devuelven el orden del sitio con `"sorted": false`.
- Total calculado con la última página de la paginación.
- `nextCursor` codifica el offset; `--cursor` tiene prioridad sobre `--page`.

## Miniaturas y descargas

- `thumb` estática extrae un fotograma del preview WebM (conserva la
  proporción real). `og:image` 1920×1080 queda como respaldo y el ítem se marca
  `staticCropped` si no es 16:9.
- `thumb --animated` usa el preview WebM y genera un WebP animado.
- `fetch` descarga el MP4 del token (`media`) o el preview WebM
  (`--kind preview`). Devuelve `width`, `height` y `durationSec` (ffprobe).
- Si el MP4 falla por red, `fetch media` cae al preview WebM y lo registra en
  stderr. `TOO_LARGE` nunca se reintenta con el preview.
- Para `--limit <= 12` la lista enriquece cada tarjeta con su detalle. Para
  límites mayores usa el preview WebM predecible desde el thumbnail.
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

## Cambios respecto a v1

- **Cambio del proveedor:** el MP4 exige ahora `Referer`; v1 no lo enviaba.
- `health` no sale a la red.
- `fetch` escribe en `--out` y declara metadatos del vídeo.
- Se añaden `--sort`, `--cursor`, `nextCursor` y el recorte a 200.

