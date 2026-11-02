# Análisis de `appcompanion/base.apk` — viabilidad de la app móvil

> Fecha: 2026-10-27. Fuente: `appcompanion/base.apk` (73,9 MB) y
> `Wallpaper Engine 2.8.8.apk+` (78,9 MB). Decompilado con **jadx 1.5.1**
> en `D:\Temp\opencode\apk_src` (sólo código; recursos no extraídos).

## 1. Qué es realmente

`io.wallpaperengine.weclient` **no es un mando a distancia**: es **el motor de
Wallpaper Engine portado a Android**, completo.

| Dato | Valor |
|------|-------|
| Paquete / versión | `io.wallpaperengine.weclient` 2.8.8 |
| minSdk | 32 (Android 12L) |
| Tamaño | 73,9 MB (93,9 MB descomprimido) |
| Código | 2 dex: 9,7 MB + 2,3 MB — **Kotlin/Java**, sin `.so` propios |
| Motor | `assets/materials/**` (partículas, PBR, HDR), `assets/shaders/**` (~80 HLSL portados a GLSL), `assets/models/**` |
| Contenido | 9 wallpapers de ejemplo en `assets/wallpapers/*.mpkg` |
| Locales | 40 idiomas (`assets/locale/ui_*.json`, `core_*.json`) |

Es decir: **renderiza escenas 3D en el teléfono** (mismo formato `.mpkg` que el
escritorio), además de recibir wallpapers del PC.

Componentes propios (paquete pequeño, ~200 KB de código):

| Clase | Función |
|---|---|
| `WEClient` | `Application`; bindea `TransferService` según el ciclo de vida |
| `TransferService` (34 KB) | Conexión con el PC: descubrimiento, auth, descargas |
| `DiscoverService` (11 KB) | Descubrimiento por **multicast UDP** |
| `PairingActivity` (21 KB) | Emparejamiento con PIN |
| `BrowseActivity` / `BrowseFragment` | Navegador de la biblioteca del PC |
| `WEWallpaperService` | Render del wallpaper (GL) |
| `weutil/TcpClient` (31 KB) | **El protocolo** |
| `weutil/Util` (83 KB) | Utilidades, pairings, resolución, logs |

## 2. Protocolo (reconstruido del cliente)

### 2.1 Descubrimiento — UDP multicast

- Puerto **7884**, grupo **239.100.0.1** (`MulticastSocket`), `MulticastLock` de Wi-Fi.
- El PC emite datagramas de texto UTF-8:

```
WallpaperEngine/Ping:<puerto>:<nombre del PC>:<versión de protocolo>:<guid>
```

- Se parsea con `split(':')` esperando **5 campos**; `[1]` = puerto (5 dígitos),
  `[2]` = nombre (trim), `[3]` = versión, `[4]` = GUID del PC.
- El cliente guarda `deviceIP` (origen del datagrama), `port`, `guid` y
  `protocolVersion` en `DeviceItem`.

### 2.2 Conexión — TCP + RSA + AES

`TcpClient.connector()` (1.518 instrucciones, decompilado con `--show-bad-code`):

1. `Socket(ip, puerto)`.
2. Lee `int len` + `len` bytes → **módulo RSA** del PC.
3. Lee `int len` + `len` bytes → **exponente RSA**.
   (Ambos big-endian, se anteponen 4 ceros para `BigInteger`.)
4. Envía `int len` + `RSA/ECB/OAEPPadding(deviceUUID)`.
5. Envía `int len` + `RSA/OAEP(**IV**, 16 bytes aleatorios)`.
6. Envía `int len` + `RSA/OAEP(**clave AES** generada por el cliente)`.
7. A partir de aquí, **todo va cifrado** con `AES/CBC/PKCS5Padding`:
   cada mensaje = `int longitud` + `AES(GSON)`.
   Límites: comandos ≤ 4096 B, datos ≤ 1.048.592 B (1 MB).
8. **Auth**: `EncAuthRequest` (GSON) con

```json
{"pin":"1234","name":"<modelo>","deviceUserName":"...",
 "displayWidth":1080,"displayHeight":2400,
 "version":4,"mpkgVersion":"PKGM0020","appVersion":"..."}
```

9. Respuesta `EncAuthResponse`: `{"status":"AuthOK"}` o el error.
10. Bucle de comandos con `DownloadInfo`
    (`{command, title, file, size, bytesRemaining, data}`).
11. Keep-alive: `"heartbeat\n"` cada ~3 s.

### 2.3 Emparejamiento

- PIN que el usuario teclea en el móvil (lo muestra el PC).
- Al aceptar, el móvil guarda `PairedServer{guid, nombre, modulus, exponent}`
  (`usedPairings` en `Util`), es decir **pinning de la clave pública**: en
  conexiones posteriores cifra el IV/clave AES con **la clave guardada** en vez
  de la que anuncia el servidor (protección contra suplantación).
- FAQ oficial: `https://help.wallpaperengine.io/mobile/pairing.html`.

### 2.4 Transferencia

- `DownloadInfo.command` + `file` + `size` + `bytesRemaining` y trozos en
  `data`; `RangedInputStream`/`VirtualFileRange` → **descarga por rangos**.
- El móvil también **sube** (`TransferService` + `AsyncPreviewLoader`).

## 3. Opciones

### A. Clonar el protocolo (que la app oficial funcione con nuestro escritorio)

Implementar en nuestro motor el **lado servidor**:
multicast ping + TCP + par RSA + OAEP + AES-CBC + GSON + semántica de comandos.

- **A favor**: la app oficial ya existe, probada, con motor 3D y 40 idiomas.
- **En contra**:
  - Hay que reproducir **exactamente** la semántica de cada `command` (el
    cliente sólo dice qué envía; el comportamiento lo define el servidor, que
    está en el `.exe` del escritorio).
  - Se ata a la **versión de protocolo 4** y a `mpkgVersion "PKGM0020"`:
    cualquier actualización de la app puede romperlo.
  - Nuestros wallpapers son **mp4/jpg**, no `.mpkg`: el móvil esperaría
    proyectos WE (habría que empaquetar o limitar a "ver miniatura").
  - Riesgo legal/ToS y de soporte (usuarios con la app oficial actualizada).
- **Esfuerzo estimado**: 3–4 semanas (servidor + pruebas con la app real),
  con riesgo alto de incompatibilidad.

### B. Companion propia (recomendada)

App Android nueva que habla **nuestro** protocolo, reutilizando lo que ya
tenemos (catálogo, descargas, motor, servidor de ficheros del host CEF).

- **A favor**: control total de ambos extremos, sin dependencia de versiones
  ajenas, sin ingeniería inversa pendiente, código 100 % nuestro.
- **En contra**: hay que escribir la app (UI + red). No habrá render 3D en el
  móvil (sólo control + previsualización); el render seguiría en el PC.
- **Esfuerzo estimado**: 1–2 semanas para un v1 útil.

## 4. Propuesta de contrato propio (v1)

```
Descubrimiento: UDP 7884 (mismo patrón que WE, para no chocar)
  WPX/Ping:<puerto>:<nombrePC>:1:<guid>

HTTP (reutiliza el servidor de ficheros del host CEF)
  GET  /api/info            -> {name, version, guid, pairing:true|false}
  POST /api/pair  {pin}     -> {token}            (PIN de 6 dígitos, TTL 5 min)
  GET  /api/library?page=   -> [{id,title,type,thumb,size,duration}]
  GET  /api/thumb/<id>      -> WebP (estático o animado)
  POST /api/apply {id}      -> {ok}
  GET  /api/stream/<id>     -> mp4/jpg con Range (preview en el móvil)
  POST /api/upload          -> sube un wallpaper al PC
  WS   /api/events          -> estado (aplicado, playlist, descargas)
Auth: cabecera `X-WPX-Token` (HMAC del PIN + guid); TLS opcional con
certificado autofirmado + pinning (igual que hace WE con RSA).
```

Ventaja: el mismo backend sirve para la app y para un futuro cliente web.

## 5. Recomendación

**Opción B**. La app oficial es un motor completo, no un mando; clonar su
protocolo ata el proyecto a una versión propietaria y a un formato (`.mpkg`)
que no producimos. Con un protocolo propio ganamos robustez, y el trabajo se
concentra en la app Android (Kotlin + Compose), reutilizando catálogo,
miniaturas y descargas que ya funcionan.

**Plan sugerido (10 días laborables)**

| Día | Entregable |
|---|---|
| 1–2 | Endpoints HTTP + token + PIN en el escritorio (sobre el host CEF actual) |
| 3–4 | Descubrimiento UDP + `GET /api/info` + pairing desde app mínima |
| 5–6 | Biblioteca con miniaturas (WebP animado) y aplicar wallpaper |
| 7 | Streaming de preview (Range) y subida de ficheros |
| 8–9 | Playlist y ajustes básicos; notificaciones |
| 10 | Empaquetado, pruebas en emulador/dispositivo, documentación |

## 6. Notas técnicas sueltas (útiles si algún día hace falta A)

- El móvil usa `GSON` para todos los mensajes (nombres de campo exactos arriba).
- `TransferService.CONNECTION_PROTOCOL_VERSION = 4`;
  `MPKG_SUPPORT_VERSION = "PKGM0020"`.
- El PC anuncia el nombre con `Util.getDeviceName()` (≤ 32 caracteres en el móvil).
- El móvil recuerda el `guid` del PC; el ping lo incluye en el campo `[4]`.
- La app **no** tiene librerías nativas propias: todo GL va por
  `android.opengl` desde Kotlin/Java, lo que confirma que el render está
  portado a mano (no reutiliza el D3D11 del escritorio).
