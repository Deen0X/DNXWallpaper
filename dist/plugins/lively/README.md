# LivelyWallpaper.app (v2)

Vídeos de **livelywallpaper.app**, un sitio fan-made no oficial. Scraping del
sitemap de vídeo y de los listados de categoría.

## Proveedor y límites

- Sitemap: `https://livelywallpaper.app/video-sitemap.xml` (840 vídeos en la
  última comprobación).
- Listados: `/live-wallpapers/` y `/live-wallpapers/<categoria>/`, 36 tarjetas
  por página.
- Categorías: `anime`, `games`, `cars`, `nature`, `space`, `art-fantasy`, `4k`.
- Medios directos en `cdn.livelywallpaper.app`:
  `download.mp4`, `preview.mp4`, `-1280.webp`.
- Límite orientativo declarado: 20 peticiones/minuto, burst 3.
- El sitemap no incluye resolución; los listados de categoría sí
  (`data-resolution`, `data-size`).

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 10
python main.py list --query gojo --limit 5
python main.py list --category anime --limit 10
python main.py detail --id lv-gojo-satoru-unlimited-void
python main.py thumb --id lv-gojo-satoru-unlimited-void --out tmp.jpg --size 480
python main.py thumb --id lv-gojo-satoru-unlimited-void --out tmp.webp --animated
python main.py fetch --id lv-gojo-satoru-unlimited-void --out video.mp4 --max-bytes 50000000
```

## Identificadores

`id = "lv-" + slug del vídeo` (por ejemplo
`lv-gojo-satoru-unlimited-void`). Es estable y no depende de la posición.

## Orden y paginación

- `newest` ordena por `publication_date` del sitemap (descendente).
- `popular` y `relevance` no existen en la fuente: se devuelve el orden de
  novedad con `"sorted": false`.
- `--query` busca en título, descripción, tags y categorías.
- `--since`/`--until` aceptan `YYYY-MM-DD`.
- `nextCursor` codifica el offset; `--cursor` tiene prioridad sobre `--page`.

## Miniaturas y descargas

- `thumb` estática extrae un fotograma de `preview.mp4` (conserva la
  proporción); `thumbnail_loc` (WebP 1280) queda como respaldo.
- `thumb --animated` usa `preview.mp4` y genera un WebP animado sin recorte.
  Si falla, devuelve la estática con `"animated": false`.
- `fetch` descarga `content_loc` (`download.mp4`). Para vídeo devuelve
  `width`, `height` y `durationSec` (ffprobe del archivo descargado).
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

- `health` no sale a la red.
- `fetch` escribe en `--out` y declara metadatos del vídeo.
- Se añaden `--sort`, `--cursor`, `nextCursor` y el recorte a 200.
- La lista usa el sitemap (rápido) y solo scrapea listados de categoría cuando
  se pide `--category`.
