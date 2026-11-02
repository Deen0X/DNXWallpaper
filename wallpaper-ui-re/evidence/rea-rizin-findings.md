# Evidencia REA + Rizin — wallpaperui.exe (host CEF)

Binario: `distribution/bin/wallpaperui.exe` (12.742.640 bytes).
Fecha: 2026-10-25.

## Identificación

- PE64, MSVC Linker 14.51, lenguaje C, subsistema Windows GUI.
- Compilado: 29-jun-2026. Firmado. GUID `C167A18190274ED4827D7179BF0AB36F1`.

## Imports (REA `inspect-pe-imports`, 22 DLLs)

KERNEL32 (230), USER32 (89), GDI32 (14), SHELL32 (10), ole32 (14),
ADVAPI32 (21), **WS2_32 (22) + MSWSOCK (2)** (red/websockets),
gdiplus (24), COMCTL32 (3), dwmapi (2), WINMM (2), d3d11 (1), DWrite (1),
POWRPROF (1), SHLWAPI (2), dxgi (1), **libcef.dll (52)**,
WINTRUST (WinVerifyTrust), CRYPT32 (7),
**assimp-vc143-mt64 (13)** (modelos 3D, previews),
**FreeImage64 (28)** (imágenes, previews).

## API CEF (C API, 52 imports)

Ciclo de vida: cef_execute_process / cef_initialize / cef_shutdown /
cef_do_message_loop_work / cef_run_message_loop / cef_quit_message_loop.
Esquemas: cef_register_scheme_handler_factory (+ clear).
Navegador: cef_browser_host_create_browser.
V8: cef_v8_value_create_{bool,string,object,function},
cef_v8_context_get_current_context, cef_process_message_create.
Utilidades: cef_*string*, cef_parse_url, cef_get_mime_type,
cef_uriencode/decode, cef_log, stream_reader_create_for_{file,data}.

## Strings (.rdata, 17.455 únicos)

- **LocalFileSchemeFactory**: `wpx.internal`, `/__file/`, `ui/dist`,
  `http://wpx.internal/__file/`, `Not Found`, `text/plain`, `html`,
  `/index.html`, `application/octet-stream`,
  `LocalFileSchemeFactory: kFilePrefix failed: %S`,
  `LocalFileSchemeFactory: CefStreamReader::CreateForFile failed`.
- **wpx.app**: `CefEngine::OnContextInitialized ... wpx.app ...
  Failed registering U...` (segundo esquema, misma raíz).
- **URL arranque**: `http://wpx.internal/` + `?skinStyle=` + `&skinKey=` +
  `&skinColor=#` + `&lang=` + `&provider=` (+ `steamchina`, `ui...`).
- **Métodos editorObject** (adyacentes): updateExportStatus,
  exportStatusCtrl, updateTexturePreview, navigateBackwards/Forwards,
  previewSizeChanged, previewToggleVisibility, restartPreview,
  saveEditorSettings, importFile, getImportFileList + methodName/dstPath.
- **Arranque**: `-weuimain`, `-language`, `-skin`, `ui/skins.json`,
  `remote-debugging`, ` -cefcommandline `, `chromiumerrorsentinel.lock`,
  `OleInitialize failed`, `CEF DevTools`, `CEF load error: %i - %S - URL %S`,
  `Failed subclassing CEF browser.`,
  `API hashes for libcef and libcef_dll_wrapper do not match.`
- **Steam (eliminado)**: `SteamAPI_RegisterCallback/RunCallbacks`,
  `-steamlanguage` (alias local conservado como flag).
- **Fuentes**: `D:\dev\we\windows\src\ui\src\web\cefengine.cpp`,
  `cefutil/include/cefbrowserutil.h`, CEF `welibs/cef_7680`.

## Clases (símbolos `?M@C@@`, 17)

BrowseWallpapersWindow::{CheckWorkshopAgreementPrompt [ELIM],
OnBrowseWallpaperObjectMethodCall, RunMobileDeviceDiscovery [ELIM], Update},
EditorWindow::{AddLayersFromWorkshopAsset, ChoosePublishMethod, OpenProject,
QueryWorkshop [ELIM], RequestPublishToSteamDelegate [ELIM],
UpdateProjectBackendTags}, SettingsWindow::OnSettingsObjectMethodCall,
JSWindow::OnUiMethodCall, CEFWallpaper::{Event, InterProcessMessage},
+ reutilizadas del motor (DataCache, SceneWallpaper, VideoWallpaper,
MaterialSystem, Texture, PropertySystem, DesktopMediaExtensions),
Concurrency (PPL), SDF::sdf_partial, ResourceCompilerErrors.

## Verificación

`src/core` + `src/windows.cpp` + `src/test_core.cpp` compilan con MSVC
14.51 y pasan: `CORE TESTS OK` (scheme wpx.internal/__file, query,
anti-traversal, MIME, dispatch con sufijos y CallbackError).
`src/cef/` requiere CEF 7680 (no disponible offline) — documentado.

## Limitaciones

- Handlers V8 exactos y render de previews (assimp/FreeImage) no
  desensamblados a fondo: contratos reconstruidos por strings + imports.
- libcef no redistribuible aquí: el usuario aporta el CEF build.
