# Wikimedia Commons (v2)

Imágenes y vídeos de **commons.wikimedia.org** mediante la API MediaWiki. No
requiere API key.

## Proveedor y límites

- `GET https://commons.wikimedia.org/w/api.php` con
  `action=query&generator=search`.
- Imágenes por defecto: query `landscape` + `filetype:bitmap`.
- Vídeos: `--animated true` + `space filetype:video filemime:video/webm`
  (WebM VP8).
- `gsrinfo=totalhits` da el total; `gsroffset` pagina; `gsrlimit` máximo 50.
- `iiurlwidth=1280` devuelve una miniatura JPG incluso para WebM.
- Límite orientativo declarado: 30 peticiones/minuto, burst 5.
- Cada ítem incluye `license` y `attribution` de `extmetadata`.

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 10 --sort newest
python main.py list --query nebula --limit 5
python main.py list --category space --animated true --limit 5
python main.py detail --id wm-1499793
python main.py thumb --id wm-1499793 --out tmp.jpg --size 480
python main.py thumb --id wm-150032342 --out tmp.webp --animated
python main.py fetch --id wm-150032342 --out video.webm --max-bytes 50000000
```

## Identificadores

`id = "wm-" + pageid de Commons` (por ejemplo `wm-1499793`). Es estable y no
depende de la posición; `detail` recupera el `pageid` original.

## Orden y paginación

- `newest` → `gsrsort=create_timestamp_desc`.
- `relevance` → `gsrsort=relevance`.
- `popular` no existe en la API: se usa relevancia y se devuelve
  `"sorted": false`.
- `nextCursor` codifica el offset de `gsroffset`; `--cursor` tiene prioridad
  sobre `--page`.

## Miniaturas y descargas

- `thumb` estática usa `thumburl` (JPG 1280, ya respeta la proporción); si no
  existe, extrae un fotograma del media.
- `thumb --animated` descarga el WebM y genera un WebP animado sin recorte
  (máximo 320-480 px). Solo se intenta si el archivo es **≤ 20 MB**; si no, se
  devuelve la estática con `"animated": false`.
- `fetch` respeta `--max-bytes`; para vídeo devuelve `width`, `height` y
  `durationSec`, y un `contentType` `video/webm`.
- `fetch --kind preview` solo tiene sentido en vídeos ≤ 20 MB.

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
- `fetch` escribe en `--out` y devuelve metadatos del vídeo.
- Se añaden `--sort`, `--cursor`, `nextCursor`, `hasMore` y recorte a 200.
- `thumb --animated` degrada a estática en vez de fallar.
