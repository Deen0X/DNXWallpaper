# Pixabay (v3)

Imágenes y vídeos de **pixabay.com** mediante la API oficial.

## Configuración

La API exige una `apiKey`. Se guarda en `config.json` del plugin o en la ruta
indicada por `--config` / `DNX_PLUGIN_CONFIG`.

```bash
python main.py config --set apiKey=TU_API_KEY
python main.py config
```

Sin clave, `list`, `detail`, `thumb` y `fetch` devuelven `CONFIG_REQUIRED`
(salida 5). `health` no sale a la red y avisa de que falta la clave.

## Proveedor y límites

- Imágenes: `GET https://pixabay.com/api/`.
- Vídeos: `GET https://pixabay.com/api/videos/`.
- Límite oficial: **100 peticiones/60 s**; caché de respuestas 24 h.
- `per_page` 3..200; `totalHits` está limitado a **500 por consulta** en la API.
- Atribución obligatoria: se rellena `attribution` con `Pixabay · autor`.
- `--animated` (o `--animated true`) devuelve **vídeos**; `--animated false`
  devuelve **imágenes**. El valor por defecto es vídeo, como en v1.

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 20 --sort newest
python main.py list --query "neon city" --category backgrounds --limit 10
python main.py list --animated false --query landscape
python main.py detail --id px-v123456
python main.py thumb --id px-v123456 --out tmp.jpg --size 480
python main.py thumb --id px-v123456 --out tmp.webp --animated
python main.py fetch --id px-v123456 --out video.mp4 --max-bytes 50000000
python main.py fetch --id px-i123456 --out image.jpg
```

## Identificadores

- Vídeo: `px-v<id numérico>` (canónico `video-<id>`).
- Imagen: `px-i<id numérico>` (canónico `image-<id>`).

Son estables y no dependen de la posición. El prefijo evita la colisión entre
los ids numéricos de imagen y vídeo que ya existía en v1.

## Orden y paginación

- `newest` → `order=latest`.
- `popular` → `order=popular`.
- `relevance` no está soportado por la API: se usa `latest` y se devuelve
  `"sorted": false`.
- `nextCursor` codifica el offset lógico; `--cursor` tiene prioridad sobre
  `--page`.

## Miniaturas y descargas

- Vídeo: `thumb` extrae un fotograma del rendition más pequeño (misma
  proporción que el vídeo); el póster `tiny/small` queda como respaldo y el
  ítem se marca `staticCropped` si no es 16:9. La animada (`--animated`)
  descarga ese rendition y genera un WebP animado.
- Imagen: `webformatURL` como miniatura; `imageURL`/`fullHDURL`/`largeImageURL`
  como media.
- `fetch` respeta `--max-bytes` y devuelve `TOO_LARGE` sin dejar `.part`.
- `fetch --kind preview` devuelve el rendition de vídeo más pequeño.

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

- `config` ya no escribe un fichero desde un flag propietario: usa el esquema
  `--set clave=valor`.
- `fetch` escribe en `--out` y devuelve `width`, `height` y `durationSec`.
- Se recorta `--limit` a 200 y se declara en la respuesta.
- La API key nunca aparece en los mensajes de error (se sustituye por `***`).
