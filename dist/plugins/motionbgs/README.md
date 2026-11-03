# MotionBGs (v3)

Vídeos de **motionbgs.com** mediante scraping de listados y del JSON-LD del
detalle.

## Proveedor y límites

- Home: `https://motionbgs.com/` (60 tarjetas en la primera página, 36 en el
  resto).
- Categorías/tags: `/tag:<tag>/` y `/4k/`, 36 tarjetas por página.
- Búsqueda: `/search?q=<texto>&page=N`.
- Detalle: `/<slug>`; incluye `VideoObject` con `contentUrl` (preview 960×540),
  `duration`, `uploadDate` y `thumbnailUrl`.
- Variantes: `/dl/4k/<mediaId>/`, `/dl/2k/<mediaId>/`, `/dl/hd/<mediaId>/`
  devuelven MP4 directo (con redirección 301 a la barra final).
- Límite orientativo declarado: 20 peticiones/minuto, burst 3.

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 10
python main.py list --category anime --limit 10
python main.py list --query galaxy --limit 5
python main.py detail --id mb-a-galaxy-within
python main.py thumb --id mb-a-galaxy-within --out tmp.jpg --size 480
python main.py thumb --id mb-a-galaxy-within --out tmp.webp --animated
python main.py fetch --id mb-a-galaxy-within --out video.mp4 --max-bytes 50000000
```

## Identificadores

`id = "mb-" + slug del detalle` (por ejemplo `mb-a-galaxy-within`). Es estable.

## Orden y paginación

- Solo existe el orden de novedad del sitio.
- `newest` se declara soportado; `popular` y `relevance` devuelven el orden del
  sitio con `"sorted": false`.
- El total se obtiene de la página canónica final (el sitio redirige
  `/999999/` a la última página) y del número de tarjetas de la última página.
- `nextCursor` codifica el offset; `--cursor` tiene prioridad sobre `--page`.

## Miniaturas y descargas

- `thumb` estática extrae un fotograma de `contentUrl` (MP4 960×540, misma
  proporción que el vídeo); `thumbnailUrl` queda como respaldo.
- `thumb --animated` usa `contentUrl` para generar el WebP; si la proporción
  del medio no es 16:9, usa el MP4 de la mejor variante.
- `fetch` usa la mejor variante (`4k`, `2k`, `hd`) y, si no hay variantes,
  `contentUrl`. Devuelve `width`, `height` y `durationSec` (ffprobe).
- `fetch --kind preview` devuelve el `contentUrl` de 960×540.

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
- Se eliminaron los flags no v2 (`--min-rating`, `--since`, `--until`): se
  ignoran con aviso.
