# Wallhaven (v3)

Wallpapers estáticos de **wallhaven.cc** mediante su API oficial v1.

## Proveedor y límites

- API: `https://wallhaven.cc/api/v1/search` y `/api/v1/w/<id>`.
- Solo contenido **SFW** (`purity=100`); no se usa API key.
- La API devuelve **24 elementos por página**. El plugin compone la página
  lógica de `--limit` a partir de una o varias páginas de la API.
- Límite orientativo declarado: 45 peticiones/minuto, burst 5.
- `total` y `last_page` salen de `meta` de la API.
- Sin título en la fuente: se usa el id como `title`.

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 24 --sort newest
python main.py list --query "space blue" --category anime --limit 12
python main.py detail --id wh-8k2m1p
python main.py thumb --id wh-8k2m1p --out tmp.jpg --size 480
python main.py fetch --id wh-8k2m1p --out wall.jpg --max-bytes 20000000
python main.py config
```

## Identificadores

`id = "wh-" + id canónico de Wallhaven` (por ejemplo `wh-8k2m1p`). Es estable,
no depende de la posición en el catálogo y `detail` reconstruye el id original.

## Orden y paginación

- `newest` → `sorting=date_added&order=desc`.
- `popular` → `sorting=favorites&order=desc`.
- `relevance` → `sorting=relevance&order=desc` (solo tiene sentido con
  `--query`/`--tags`; sin búsqueda se devuelve `sorted: false`).
- `nextCursor` codifica el offset lógico; si el servidor manda `--cursor`, se
  usa en lugar de `--page`.

## Miniaturas y descargas

- `thumb` usa `path` (imagen original, sin recorte) y la escala; si falla, cae
  a `thumbs.original` (también sin recorte). `--animated` devuelve la imagen
  estática con `"animated": false`.
- `fetch` descarga `path` (el archivo original). Respeta `--max-bytes` y
  devuelve `TOO_LARGE` sin dejar el `.part`.
- No hay vídeo en esta fuente.

## Configuración

No requiere configuración. `config` devuelve un esquema vacío.

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

- `fetch` escribe en `--out` y ya no devuelve una ruta local.
- `health` no sale a la red (v1 hacía una petición).
- Se añaden `--sort`, `--cursor`, `nextCursor`, `hasMore` y recorte a 200.
- Los errores usan los códigos v2 (`NETWORK`, `RATE_LIMITED`, `PARSE`...).
