# MyLiveWallpapers (v3)

Vídeos de **mylivewallpapers.com**. El sitio está detrás de Sucuri/Cloudproxy y
exige resolver un challenge JavaScript y usar un **User-Agent de navegador**.

## Proveedor y límites

- Home `/` y `/page/N/`; categorías `/<cat>/` (y `/category/<res>/`), tags
  `/tag/<tag>/`, búsqueda `/page/N/?s=<texto>`.
- El challenge se resuelve en Python (`SucuriSolver` portado): base64 `S`,
  concatenaciones, `String.fromCharCode` y cookie
  `sucuri_cloudproxy_uuid_*` (24 h).
- 14 tarjetas por página en la última comprobación; total dinámico por
  paginación (12.118 ítems).
- Detalle: `/<categoria>/<slug>/`; incluye `og:image`, preview
  `PREVIEW-<base>.mp4` (predecible desde el thumbnail), fecha, resolución
  original y enlaces `data-downloadurl` para 4K y móvil.
- El paquete **4K** es un ZIP con `.mlw` propietario (`MLW.VIDEO`). El paquete
  **móvil** devuelve un **MP4 directo** (606×1080, ~5,7 MB): es el media que
  entrega `fetch`.
- Límite orientativo declarado: 20 peticiones/minuto, burst 3.

## Comandos

```bash
python main.py health
python main.py categories
python main.py list --page 1 --limit 10
python main.py list --category anime --limit 10
python main.py list --query 4k --limit 10
python main.py detail --id "ml-anime@ulquiorra-cifer-bleach-live-wallpaper"
python main.py thumb --id "ml-anime@..." --out tmp.jpg --size 480
python main.py thumb --id "ml-anime@..." --out tmp.webp --animated
python main.py fetch --id "ml-anime@..." --out video.mp4 --max-bytes 20000000
```

## Identificadores

`id = "ml-<categoria>@<slug>"` (por ejemplo
`ml-anime@ulquiorra-cifer-bleach-live-wallpaper`). Es estable.

## Orden y paginación

- Solo existe el orden de novedad del sitio. `newest` se declara soportado;
  `popular`/`relevance` devuelven el orden del sitio con `"sorted": false`.
- Total calculado con la última página de la paginación.
- `--query` acepta alias de resolución (`4k`, `1080p`, `1440p`, `mobile`...).
- `nextCursor` codifica el offset; `--cursor` tiene prioridad sobre `--page`.

## Miniaturas y descargas

- `thumb` estática y animada usan el **MP4 móvil** (606×1080), que es el media
  entregado. El preview `PREVIEW-*.mp4` (16:9) y `og:image` quedan como
  respaldo; el ítem se marca `staticCropped`/`animatedCropped`.
- `fetch` intenta el enlace **móvil** (MP4 directo). Si el ZIP 4K solo contiene
  `.mlw`, o el enlace falla, cae al preview MP4.
- Los ítems no declaran `width`/`height` porque el media entregado (móvil) no
  coincide con la resolución original 4K; `fetch` devuelve las dimensiones
  reales y `durationSec` con ffprobe.
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
- `fetch` escribe en `--out` y declara dimensiones/duración reales.
- Se añaden `--sort`, `--cursor`, `nextCursor` y el recorte a 200.
- Los enlaces de descarga ahora pasan por un 307 de Sucuri; el cliente HTTP
  resuelve el challenge también durante la descarga.

