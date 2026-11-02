# TCloud — subida de APKs

Servicio local de TCloud que transporta ficheros a Telegram por HTTP.
Solo transporta bytes: no firma, no comprime, no analiza el APK.

## Credenciales

Están en `.tcloud.env` (raíz del proyecto, **ignorado por git**):

```
TCLOUD_URL=http://192.168.88.115:8931
TCLOUD_KEY=<token>
TCLOUD_CHAT=-1004423392613
TCLOUD_TOPIC=8
```

- El token **no** se escribe en código, logs, capturas, commits ni en
  `Memory.txt`.
- No se imprime el contenido de `.tcloud.env`.
- Servidor dedicado desde 2026-10-29 (`192.168.88.115:8931`). El
  health-check válido es `/health`; `/api/health` y `/v1/health` devuelven 404.
- La API se autentica con la cabecera **`X-API-Key`** (no `Authorization`).
  Una clave válida con cuerpo vacío devuelve `422` (validación), no `401`.
- Si el servicio no responde, preguntar por la IP antes de cambiar la
  configuración.

## Scripts

- `tools/tcloud_upload.py` — cliente stdlib en streaming (bloques de 1 MB).
- `tools/upload_apk.ps1` — carga `.tcloud.env`, calcula SHA-256 corto, añade
  caption/fecha y llama al cliente.
- `tools/upload_apk.cmd` — atajo para PowerShell.

### Uso

```powershell
# Debug (por defecto)
powershell -NoProfile -ExecutionPolicy Bypass -File tools/upload_apk.ps1 `
  -Caption "DNXWallpaper v3 - debug - cambios" -Lane low

# Release
powershell -NoProfile -ExecutionPolicy Bypass -File tools/upload_apk.ps1 `
  -Apk "ruta\app-release.apk" -Caption "DNXWallpaper v3 - release - cambios" `
  -Lane high -Front

# Otro chat/tema
powershell -NoProfile -ExecutionPolicy Bypass -File tools/upload_apk.ps1 `
  -Chat "@mi_canal" -Topic 2
```

El APK por defecto es
`appcompanion\DnxCompanion\app\build\outputs\apk\debug\app-debug.apk`.

## Destino

- Chat: `-1004423392613`
- Tema: `8`
- Debug y release van al mismo tema.
- Caption: resumen/changelog + fecha + `sha256:<12 chars>`.
- El nombre original del APK se conserva (`app-debug.apk`).

## Reglas

1. Streaming: bloques de 1 MB; nunca `read_bytes()`/`read()` sin tamaño.
2. `202` = sigue el mismo job; `200` = terminado (`msg_id`); `410` cancelado;
   `502` fallo.
3. `401/403`: parar y reportar. `502`: reintentar **una vez** con job nuevo.
4. Tras cada subida, reportar `msg_id` y enlace (`t.me/c/...`).
5. Máximo 2 GB por fichero; sin reanudación.
6. El servidor usa ~2× el tamaño en disco mientras sube.

## Registro de subidas

| Fecha | Fichero | Variante | Bytes | msg_id | Enlace |
|---|---|---|---:|---:|---|
| 2026-10-28 15:42 | app-debug.apk | debug (Fase 0) | 13.546.614 | 10 | https://t.me/c/4423392613/8/10 |
| 2026-10-28 16:21 | app-debug.apk | debug (vídeo local + timeout) | 13.546.614 | 11 | https://t.me/c/4423392613/8/11 |
| 2026-10-28 16:30 | app-debug.apk | debug (FydeOS + vídeo local) | 13.546.614 | 12 | https://t.me/c/4423392613/8/12 |
| 2026-10-28 17:18 | app-debug.apk | debug (Fase 1: /m + editor) | 13.546.614 | 13 | https://t.me/c/4423392613/8/13 |
| 2026-10-28 17:26 | app-debug.apk | debug (selector de fondo /m) | 13.546.614 | 14 | https://t.me/c/4423392613/8/14 |
| 2026-10-28 17:37 | app-debug.apk | debug (diagnostico de red) | 13.546.614 | 15 | https://t.me/c/4423392613/8/15 |
| 2026-10-28 18:07 | app-debug.apk | debug (quitar fondo + pestañas editor) | 13.546.614 | 16 | https://t.me/c/4423392613/8/16 |
| 2026-10-28 18:16 | app-debug.apk | debug (quitar fondo con fallback estatico) | 13.546.614 | 17 | https://t.me/c/4423392613/8/17 |
| 2026-10-28 18:41 | app-debug.apk | debug (guardar y restaurar fondo anterior) | 13.546.614 | 18 | https://t.me/c/4423392613/8/18 |
| 2026-10-28 20:29 | app-debug.apk | debug (restaurar fondo: imagen elegida) | 13.546.614 | 19 | https://t.me/c/4423392613/8/19 |
| 2026-10-28 21:40 | app-debug.apk | debug (biblioteca del PC en el movil) | 13.546.614 | 20 | https://t.me/c/4423392613/8/20 |
| 2026-10-28 22:03 | app-debug.apk | debug (buscar PC en la red, UDP 7884) | 13.546.614 | 21 | https://t.me/c/4423392613/8/21 |
| 2026-10-28 23:01 | app-debug.apk | debug (rediseno 3 pestanas + iconos) | 13.548.224 | 22 | https://t.me/c/4423392613/8/22 |
| 2026-10-29 00:06 | app-debug.apk | debug (catalogo local/remoto + favoritos + editor de encuadre) | 13.548.224 | 23 | https://t.me/c/4423392613/8/23 |
| 2026-10-29 10:10 | app-debug.apk | debug (video local al PC + iconos pestana + fix zoom WebP) | 13.548.224 | 28 | https://t.me/c/4423392613/8/28 |
| 2026-10-29 18:05 | app-debug.apk | debug (fix encuadre a escala real + plugins v2) | 13.548.224 | 30 | https://t.me/c/4423392613/8/30 |
| 2026-10-29 19:12 | app-debug.apk | debug (preview animado + export sin descargar + recorte en su sitio) | 12.269.969 | 33 | https://t.me/c/4423392613/8/33 |
| 2026-10-29 20:05 | app-debug.apk | debug (preview con aspecto real + ficha tras generar + recarga del fondo) | 16.823.644 | 55 | https://t.me/c/4423392613/8/55 |
| 2026-10-29 21:40 | app-debug.apk | debug (Local solo wallpapers preparados + import va al editor) | 16.644.632 | 60 | https://t.me/c/4423392613/8/60 |
| 2026-10-29 22:20 | app-debug.apk | debug (encuadre sin deformar: aspecto real + recorte al viewport) | 16.644.632 | 64 | https://t.me/c/4423392613/8/64 |
| 2026-10-29 23:05 | app-debug.apk | debug (anadir wallpaper abre su ficha para aplicar) | 16.644.632 | 69 | https://t.me/c/4423392613/8/69 |
| 2026-10-30 00:15 | app-debug.apk | debug (cabecera grande + emojis + barajar + ficha local) | 23.499.392 | 72 | https://t.me/c/4423392613/8/72 |
| 2026-10-30 01:30 | app-debug.apk | debug (icono grande + selector plugin + filtros reales + toast) | 23.499.392 | 82 | https://t.me/c/4423392613/8/82 |
| 2026-10-30 02:20 | app-debug.apk | debug (aviso de filtro vacio + quitar filtros) | 23.499.392 | 84 | https://t.me/c/4423392613/8/84 |
| 2026-11-02 12:40 | app-debug.apk | debug (busqueda en vivo con Enter + selector con estado + plugin fetch) | 23.499.392 | 244 | https://t.me/c/4423392613/8/244 |
| 2026-11-02 13:35 | app-debug.apk | debug (insets de la barra de navegacion en Android) | 23.499.392 | 245 | https://t.me/c/4423392613/8/245 |
