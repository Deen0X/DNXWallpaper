---
description: Sube el APK actual a TCloud (Telegram) y reporta msg_id/enlace
---

Sube el APK actual al servicio TCloud.

Ejecuta:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/upload_apk.ps1 -Caption "$ARGUMENTS" -Lane low
```

Reglas:
- No imprimas `TCLOUD_KEY` ni el contenido de `.tcloud.env`.
- El destino por defecto es el de `.tcloud.env` (`TCLOUD_CHAT`, `TCLOUD_TOPIC`).
- Para release usa `-Lane high` (y `-Front` si es importante).
- Reporta el `msg_id` y el enlace `t.me/c/...` que devuelve el script.
- Si el servicio no responde en `127.0.0.1:8099`, pregunta por la IP.
