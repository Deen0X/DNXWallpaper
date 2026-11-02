# Evidencia REA + Rizin — launcher.exe

Binario: `D:\SteamLibrary\steamapps\common\wallpaper_engine\launcher.exe`
Fecha: 2026-10-25. Herramientas instaladas en `D:\Tools` (Rizin 0.9.1, JDK 21).

## Identificación (REA `inspect-artifact` + Rizin `rabin2 -I`)

- PE32+ x64 (machine 34404), 837.616 bytes
- SHA256: `bbb416da3e34d74beee7b1fd60984312c9c5ca6dc8435a5dbe9575f19404af02`
- Compilador: MSVC Linker 14.51, lenguaje C, subsistema **Windows GUI**
- Compilado: 29-jun-2026. PDB: `launcher.pdb`. Firmado (DigiCert).
- Protecciones: NX, PIE, stack canary. Firmado.

## Imports (REA `inspect-pe-imports`, 6 DLLs, sin Steam estático)

| DLL | Funciones | Uso inferido |
|---|---|---|
| KERNEL32.dll | 134 | Procesos, ficheros, memoria, sincronización |
| USER32.dll | 37 | MessageBox, ventanas |
| ADVAPI32.dll | 20 | Registro + servicios Windows |
| SHELL32.dll | 3 | ShellExecuteExW/W, SHGetFolderPathW |
| d3d11.dll | 1 | D3D11CreateDevice (chequeo GPU) |
| COMCTL32.dll | 1 | Controles comunes |

`steam_api.dll` NO está en imports: el original la usa (si acaso) por
carga dinámica desde `bin/`. En la reconstrucción se elimina por completo.

## Secciones (Rizin `rabin2 -S`)

`.text` (500 KB, r-x), `.rdata`, `.data`, `.pdata`, `.fptable`, `.rsrc`, `.reloc`.
Layout MSVC estándar, sin empaquetar.

## Strings clave (Python, 288 interesantes)

- Tabla de archivos verificados: `bin/libcef.dll`, `bin/chrome_*.pak`,
  `bin/*.dll`, `wallpaper32/64.exe`, `bin/webwallpaper64.exe`,
  `bin/edgewallpaper64.exe`, `bin/wallpaperservice64.exe`,
  `bin/steam_api*.dll`, `plugins/...`, con hashes SHA256 hex embebidos.
- `VerifyFileHashes hash failed for %S (%s != %s)` (+ 3 variantes).
- `InstallDistribution: ...` (12 mensajes) + `core_updater_status_*`.
- `[steamusers]`, `SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile`.
- `-steamtrackhours`, ` -run `, ` -op getsteamlanguage`,
  ` -op steamtrackhours -messagehandler __steam_trck_hrs -parentprocess `,
  regex de args: `-steamtrackhours|-final|-start|-forceupdate|(-language\s+[\w]+)|...|-nobrowse|(-run\s+[\w\.]+)`.
- `Launcher cannot find wallpaper32.exe/wallpaper64.exe at path: %s...`
  (mensaje antivirus), `Launcher cannot run the executable: %S (%S, code: %i)!`.

## Funciones (Rizin `aaa` + `afl`, ~80)

| Función | Evidencia | Rol |
|---|---|---|
| fcn.140008100 (169 bl.) | xref a `VerifyFileHashes hash failed` (0x14008c770) | VerifyFileHashes |
| fcn.14000b6f0 | xref a `InstallDistribution: Failed opening...` (0x14008cb30) | InstallDistribution |
| fcn.1400222f0 | xrefs a `Launcher cannot find/run` (0x14008faa0/0x14008fde8), CALL XREF desde entry0 | **main (wWinMain)** |

## Bloques Steam eliminados (desensamblado fcn.1400222f0)

1. `0x140023304` — test del flag `-steamtrackhours` en argv.
2. `0x140023460` — `fcn.14000e670(" -steamtrackhours")` concatenado al cmdline del hijo (condicional a r14b).
3. `0x14002373a` — `fcn.14000e670(" -op getsteamlanguage")` + `CreateProcessW(wallpaper64.exe...)` con flags `0x228`, `Sleep`, `CloseHandle`.
4. `0x14002382f` — `fcn.14000e670(" -op steamtrackhours -messagehandler __steam_trck_hrs -parentprocess ")` + `GetCurrentProcessId` + `fcn.140010690` (append PID) + `CreateProcessW`.

Helpers identificados por uso: `fcn.14001f020` (buscar flag en cmdline),
`fcn.140024430` (test booleano), `fcn.14000e670` (concatenar string),
`fcn.14000faa0` (lanzar proceso y esperar), `fcn.140007070` (wide→UTF8/alloc),
`Sleep`/`CreateProcessW`/`CloseHandle` vía IAT.

## Flujo reconstruido (src/launcher.cpp)

VerifyFileHashes → InstallDistribution → parse args (`-run`, `-final`,
`-clearWindowPositions`[registro local], `-showbrowse`, `-language`) →
localizar wallpaper64/32.exe → CreateProcessW(0x228) → esperar.
Idioma: SO en lugar de Steam. Sin tickets, sin Workshop, sin steam_api.

## Limitaciones

- Sin Ghidra en Windows (no está en winget; requiere descarga manual +
  JDK): no hay pseudocódigo; la reconstrucción es a nivel de flujo + mensajes
  + imports, no traducción instrucción a instrucción.
- La tabla de hashes embebida (`kExpectedHashes`) debe rellenarse con los
  SHA256 reales de la distribución local.
- `wallpaper64.exe` y demás binarios quedan fuera de este proyecto.
