# DNX Wallpaper Companion (Android)

App mínima para recibir wallpapers del escritorio **DNX Wallpaper** por la red
local, sin cuentas y sin formatos propietarios.

## Qué hace

- **Escanea el QR** que muestra el escritorio (o pega la URL).
- Consulta el fichero (`HEAD`) y muestra nombre, tipo y tamaño.
- **Descarga el original** (mp4/jpg/webm) en `Downloads/`.
- Si es **imagen**: puede **establecerla como fondo** directamente.
- Si es **vídeo**: lo abre con la app que elijas.
- Acepta **deep links** `dnxwallpaper://open?u=<url>`.

El QR del escritorio es simplemente:

```
http://<ip-del-pc>:8477/dl/<key>
```

Funciona incluso sin la app instalada (se descarga desde el navegador).

## Compilar

Requiere **Android Studio** (Ladybug o superior) y JDK 17+:

1. Abrir esta carpeta (`DnxCompanion`) en Android Studio.
2. Dejar que Gradle sincronice (descarga AGP 8.5 + Compose).
3. Ejecutar en un dispositivo/emulador **Android 8.0+** (minSdk 26).

Por línea de comandos (con el SDK instalado):

```
gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

> En la máquina de desarrollo actual **no hay SDK de Android**, así que este
> proyecto no se ha compilado aquí: se entrega como fuente lista para abrir.

## Estructura

```
app/src/main/java/io/dnx/wallpaper/companion/MainActivity.kt   UI + descarga
app/src/main/AndroidManifest.xml                              permisos + deep link
app/build.gradle.kts                                          dependencias
```

## Requisitos en el escritorio

`res/config/mobile.json`:

```json
{ "enabled": true, "port": 8477, "token": "" }
```

- `token` vacío = sin autenticación (LAN doméstica).
- El servidor sólo responde a rutas del catálogo (`/dl/<key>`), nunca a rutas
  arbitrarias de disco.

## Próximos pasos

- Pantalla de **biblioteca** (navegar el catálogo del PC con miniaturas).
- **Descubrimiento UDP** para no escribir IPs.
- **Vídeo como fondo** (requiere un `WallpaperService` propio).
