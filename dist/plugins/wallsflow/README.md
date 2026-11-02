# Wallsflow (v2)

Vídeos de **wallsflow.com**. Es un sitio DLE con listados de 12 tarjetas y
detalle en JSON-LD.

## Proveedor y límites

- Listado general: `/live-wallpapers/` y `/live-wallpapers/page/N/`.
- Categorías: `/live-wallpapers/<cat>/` y
  `/live-wallpapers/<cat>/page/N/`.
- Categorías disponibles: `anime`, `games`, `cars`, `nature`, `space`,
  `movies`, `animals`, `minimalist`, `winter`, `pixel-art`.
- Búsqueda: `/index.php?do=search&subaction=search&story=<texto>`. La
  paginación de búsqueda de DLE es por AJAX; el plugin devuelve la primera
  página y `hasMore: false` (no inventa páginas).
- Detalle: `/live-wallpapers/<categoria>/<id>-<slug>.html`; incluye
  `Article`/`VideoObject` con `contentUrl`, `datePublished`, autor,
  descripción y `image`.
- El **MP4 público** de `play.wallsflow.com` es 1280×720 y ~10 s (un preview
  ligero). El archivo 4K original (p. ej. 3840×2160, ~83 MB) está detrás del
  botón `/download/<id>.html`, que **exige iniciar sesión**; el plugin no
  intenta saltarlo.
- Límite orientativo declarado: 20 peticiones/minuto, burst 3.

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 10
python main.py list --category cars --limit 10
python main.py list --query anime --limit 5
python main.py detail --id wf-anime@1454-earphone-girl-at-the-bus-stop
python main.py thumb --id wf-anime@... --out tmp.jpg --size 480
python main.py thumb --id wf-anime@... --out tmp.webp --animated
python main.py fetch --id wf-anime@... --out video.mp4 --max-bytes 5000000
```

## Identificadores

`id = "wf-<categoria>@<id>-<slug>"` (por ejemplo
`wf-anime@1454-earphone-girl-at-the-bus-stop`). Es estable y `detail`
reconstruye la URL original.

## Orden y paginación

- Solo existe el orden de novedad del sitio. `newest` se declara soportado;
  `popular`/`relevance` devuelven el orden del sitio con `"sorted": false`.
- Total calculado con la última página de la paginación (12 por página).
- `nextCursor` codifica el offset; `--cursor` tiene prioridad sobre `--page`.
- En búsqueda no hay cursor (`hasMore: false`).

## Miniaturas y descargas

- `thumb` estática extrae un fotograma del MP4 público (1280×720), sin usar el
  póster recortado.
- `thumb --animated` usa el MP4 público y genera un WebP animado sin recorte.
- `fetch` descarga `contentUrl` (1280×720, ~1 MB) y devuelve `width`, `height`
  y `durationSec` reales con ffprobe.
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

`wallsflow` es nuevo en v2: no existía en el repositorio v1. Se documenta la
limitación del MP4 4K (requiere cuenta) en lugar de inventar una descarga.

