---
name: tcloud-upload
description: Sube APKs a TCloud/Telegram en streaming, con caption y registro de msg_id. Úsala cuando haya que publicar un build de la app Android.
---

# TCloud upload (DNXWallpaper v3)

## Credenciales

- `.tcloud.env` en la raíz del proyecto (ignorado por git).
- No imprimir el token ni el contenido del fichero.
- No escribirlo en logs, capturas, commits ni `Memory.txt`.

## Scripts

- `tools/tcloud_upload.py` — cliente stdlib, streaming 1 MB.
- `tools/upload_apk.ps1` — carga el entorno, calcula SHA-256 corto, añade
  caption/fecha y sube.
- `tools/upload_apk.cmd` — atajo.

## Destino

- Chat `-1004423392613`; tema `8` (debug y release).
- APK por defecto:
  `appcompanion\DnxCompanion\app\build\outputs\apk\debug\app-debug.apk`.
- Caption: changelog breve + fecha + `sha256:<12>`.
- Se conserva el nombre original del fichero.

## Uso

```powershell
# Debug
powershell -NoProfile -ExecutionPolicy Bypass -File tools/upload_apk.ps1 `
  -Caption "DNXWallpaper v3 - debug - cambios" -Lane low

# Release
powershell -NoProfile -ExecutionPolicy Bypass -File tools/upload_apk.ps1 `
  -Apk "ruta\app-release.apk" -Caption "DNXWallpaper v3 - release - cambios" `
  -Lane high -Front
```

## Reglas

1. Streaming por bloques de 1 MB; nunca cargar el APK entero en memoria.
2. `202` = mismo job; `200` = terminado (`msg_id`); `410` cancelado; `502` fallo.
3. `401/403`: parar y reportar. `502`: reintentar una vez con job nuevo.
4. Reportar siempre `msg_id` y enlace `t.me/c/...`.
5. Si `127.0.0.1:8099` no responde, preguntar por la IP antes de cambiar nada.
6. Registrar la subida en `docs/tcloud-upload.md`.
