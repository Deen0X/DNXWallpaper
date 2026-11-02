# wallpaper-launcher-re

Reconstrucción **local y sin Steam** del lanzador de DNXWallpaper v3
(`DNXWallpaper.exe`), mediante ingeniería inversa con **REA** + **Rizin 0.9.1**
(herramientas en `D:\Tools`). Evidencia completa en `evidence/`.

## Qué hace el original (y esta reconstrucción)

1. **VerifyFileHashes** (`src/file_verify.*`): verifica SHA256 de cada
   binario distribuido (`bin/libcef.dll`, `DNXWallpaper.Motor.exe`, ...) contra la
   tabla embebida. Mensajes originales preservados.
2. **InstallDistribution** (`src/distribution.*`): cierra procesos en uso
   (Toolhelp32) y copia la distribución con `std::filesystem`.
3. **Lanzador** (`src/launcher.cpp`): localiza `DNXWallpaper.Motor.exe`
   (fallback `DNXWallpaper.Motor.exe`), construye argumentos y lo lanza con
   `CreateProcessW`, esperando su salida.

## Qué se eliminó (Steam)

| Original | Estado aquí |
|---|---|
| Flag `-steamtrackhours` | Eliminado |
| `-op getsteamlanguage` (hijo que preguntaba a Steam) | Sustituido por idioma del SO |
| `-op steamtrackhours -messagehandler __steam_trck_hrs -parentprocess <pid>` | Eliminado |
| `steam_api.dll`, tickets, Workshop | No referenciados |

## Verificación (2026-10-25, MSVC 14.51 — misma versión que el original)

- `DNXWallpaper.exe` (277 KB, PE64 GUI) compilado sin errores con `cl.exe`.
- Sin `DNXWallpaper.Motor.exe` presente → MessageBox "Launcher cannot find..." (verificado).
- Con dummy `DNXWallpaper.Motor.exe` (registra args, sale con 42):
  `DNXWallpaper.exe -final` → hijo recibió `[-final -language es]`,
  exit code propagado `42`. **Cero flags de Steam.**

## Recursos externos (res/, nada embebido)

| Recurso | Descripción |
|---|---|
| `res/hashes.json` | `{"files": {"ruta": "sha256hex"}}` — tabla de verificación. Sin archivo o entradas vacías → modo desarrollo (se omite). |

Verificado: hash real → lanza (exit 42); hash falso → bloquea (exit 1,
mensaje "VerifyFileHashes hash failed for ..."). Regenera hashes con
`rz-hash -a sha256 <archivo>` (Rizin en `D:\Tools`).

## Compilar (requiere MSVC v143 + Windows SDK)

```
msbuild launcher.vcxproj /p:Configuration=Release /p:Platform=x64
```

Rellena `kExpectedHashes` en `src/launcher.cpp` con los SHA256 reales
(puedes calcularlos con `rz-hash -a sha256 <archivo>`) o deja la tabla
vacía para omitir la verificación.

## Uso

```
DNXWallpaper.exe [-run <exe>] [-final] [-forceupdate] [-start]
             [-showbrowse] [-nobrowse] [-language XX] [-clearWindowPositions]
```

Uso responsable: proyecto educativo/de interoperabilidad, sin código
original. Úsalo solo con software que poseas o estés autorizado a examinar.
