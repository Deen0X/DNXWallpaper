# Contrato de plugins v3 — modo `serve` (worker persistente)

> Estado: **host implementado**. Los plugins lo activan declarando
> `"protocol": 3` en `plugin.json` y añadiendo el subcomando `serve`.
> Sin esa marca (o si falla) el host sigue usando el modo de un solo uso (v2).

## Motivación

Hasta v2 cada operación lanzaba un proceso Python y lo mataba al terminar.
Medido: **250–360 ms por llamada** solo de arranque + importación. En el
arranque del catálogo se lanzan ~60 procesos (10 plugins × páginas).

Con `serve`, cada plugin mantiene **un** proceso vivo y responde por líneas
JSON. Se elimina el arranque por comando y se conservan sesiones/cachés.

## Detección

`dist/plugins/<id>/plugin.json`:

```json
{
  "id": "mylivewallpapers",
  "version": "3.0.0",
  "protocol": 3,
  ...
}
```

- `protocol` ausente o `< 3` → modo de un solo uso.
- `protocol: 3` → el host intenta `serve`; si no arranca, expira o muere,
  **cae al modo de un solo uso** y no vuelve a intentarlo en esa sesión.
- Interruptor global en `res/config/plugins.json`: `"worker": false`.

## Arranque

```
python main.py serve --config "<config.json>" [--cache-dir "<dir>"]
```

Nada más arrancar, el plugin escribe **una** línea y hace flush:

```json
{"id":0,"ok":true,"event":"ready","protocol":3}
```

El host espera como máximo **5 s**. Si no llega, cierra el proceso.

## Peticiones

Una línea JSON por petición (UTF-8, sin BOM), por `stdin`:

```json
{"id":1,"cmd":"list","args":{"limit":"100","page":"1","sort":"newest"}}
```

| Campo | Descripción |
|---|---|
| `id` | entero; el host lo usa para emparejar la respuesta |
| `cmd` | `list` \| `detail` \| `thumb` \| `fetch` \| `health` \| `categories` \| `config` \| `shutdown` |
| `args` | opciones CLI sin `--`. Valores cadena o booleano |

Ejemplos:

```json
{"id":2,"cmd":"detail","args":{"id":"ww-anime@2b-live-wallpaper-2144"}}
{"id":3,"cmd":"thumb","args":{"id":"...","out":"C:\\ruta\\t.webp","size":"640","animated":true}}
{"id":4,"cmd":"fetch","args":{"id":"...","out":"C:\\ruta\\m.mp4","kind":"media"}}
```

Un booleano `true` equivale a pasar el flag sin valor (`--animated`).

## Respuestas

Una línea JSON por petición, con **el mismo payload que el comando de un solo
uso** y el `id` delante:

```json
{"id":1,"ok":true,"total":123,"hasMore":true,"items":[...]}
{"id":3,"ok":true,"path":"...","width":640,"height":360,"bytes":12345}
{"id":2,"ok":false,"code":"NOT_FOUND","message":"no hay media para ..."}
```

- Un `PluginError` se responde como error y **el proceso sigue vivo**.
- Un fallo inesperado: traceback a `stderr`, respuesta
  `{"id":N,"ok":false,"code":"INTERNAL","message":"..."}` y sigue vivo.
- `fetch` y `thumb` siguen escribiendo el fichero en `out`.

## Cierre

- `{"id":N,"cmd":"shutdown"}` → responde `{"id":N,"ok":true}` y sale con 0.
- EOF en `stdin` (el host cerró la tubería) → sale con 0.
- El host además usa un *job object* con `KILL_ON_JOB_CLOSE`: si la app muere,
  los workers mueren con ella.

## Reglas

1. `stdout` es **solo** protocolo: una línea JSON por respuesta, con flush.
   Los logs van a `stderr` (el host los guarda en
   `res/cache/plugin-data/<id>/worker-stderr.log`).
2. Sin hilos: una petición a la vez por proceso.
3. Solo stdlib (Python 3.8+).
4. Los comandos de un solo uso deben seguir devolviendo exactamente lo mismo.

## Lado del host (implementado)

- `plugin_host.cpp`: `StartWorker`, `RunViaWorker`, `StopPluginWorkers`.
- `RunPluginCommand` traduce `list --limit 100` a
  `{"cmd":"list","args":{"limit":"100"}}`, quita el `id` de la respuesta y
  devuelve el payload de siempre, así que el resto del código no cambia.
- Timeout por comando: si expira, mata el worker, lo marca roto y usa el modo
  de un solo uso para el resto de la sesión.
- Log: `[dl] worker <id> listo`, `[dl] worker <id> list OK (N bytes)`,
  `[dl] worker <id> no disponible: se usa un solo uso`.
