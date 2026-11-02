# wallpaper-ui-re

Reconstrucción **local y sin Steam** del host UI (`distribution/bin/
DNXWallpaper.UI.exe`, PE64 12 MB, MSVC 14.51): embedding **CEF** (C API,
`libcef.dll` x52 imports) que sirve la UI AngularJS y expone los
bindings `window.editorObject` / `window.ui`. Evidencia en `evidence/`.

## Arquitectura

```
wWinMain: -weuimain -language -skin ... → CefEngine::Init
  ├── cef_register_scheme_handler_factory("wpx.internal", LocalFileSchemeFactory)
  ├── cef_register_scheme_handler_factory("wpx.app", LocalFileSchemeFactory)
  ├── create_browser("http://wpx.internal/index.html?skinStyle=..&lang=..")
  ├── OnContextCreated: V8 editorObject/ui → MethodDispatcher
  └── run_message_loop
LocalFileSchemeFactory: wpx.internal/__file/<ruta> → ui/dist/<ruta>
MethodDispatcher: <metodo>Callback / <metodo>CallbackError (+ sufijos)
Ventanas: BrowseWallpapersWindow / EditorWindow / SettingsWindow / JSWindow
```

Clases originales (símbolos): `CefEngine::{OnContextCreated,
OnContextInitialized}`, `CEFWallpaper`, `EditorWindow::{OpenProject,
ChoosePublishMethod, ...}`, `BrowseWallpapersWindow::{...}`,
`SettingsWindow::OnSettingsObjectMethodCall`, `JSWindow::OnUiMethodCall`.
Fuente original referenciada: `src/ui/src/web/cefengine.cpp` (CEF 7680).

## Recursos externos (res/)

| Recurso | Descripción |
|---|---|
| `res/ui_skel/index.html` | Esqueleto que implementa el contrato (skin por query, toLocalUrl, callEditor) |
| `res/ui_skel/skins.json` | Lista de skins (`ui/skins.json` en el original) |

La UI completa (`ui/dist/...`) se sirve tal cual desde disco: el scheme
factory la lee de `ui/dist`, sin embeber nada.

## Núcleo testeable sin CEF

`src/core/` (scheme_factory + method_dispatch) y `src/windows.cpp`
compilan sin libcef. Prueba: compilar `src/test_core.cpp` en consola:

```
cl /EHsc /std:c++17 test_core.cpp core/scheme_factory.cpp
   core/method_dispatch.cpp windows.cpp /link /SUBSYSTEM:CONSOLE ...
test_core.exe  →  "CORE TESTS OK" (verificado 2026-10-25)
```

El host CEF completo (`src/cef/`) requiere CEF binary distribution
(branch 7680 x64): definir `RE_HAVE_CEF` y `CEF_ROOT`.

## Estado actual (2026-10-26)

- UI original servida por `LocalFileSchemeFactory` (`wpx.internal`),
  localización desde `locales/`, sin overlay "Connecting to Steam".
- **Galería**: 42 vídeos de carpetas configuradas + proyectos locales,
  con miniaturas reales (`res/cache/thumbs`).
- **Descubrir**: sin Steam se inyecta el catálogo local
  (`updateWallpapers`) al activar la pestaña.
- **Aplicar**: `acceptAndClose` → `selectWallpaper` → `res/current.json`
  + evento al motor.
- **Preview**: al seleccionar un vídeo se superpone un `<video>` en
  `.previewContainer` (reintentos hasta que el panel existe).
- **Añadir fondos**: `openFromFile` (diálogo nativo) importa a
  `res/projects/<nombre>/` y refresca la galería.
- Bridge completo: `editorObject`, `ui`, `browseWallpaperObject`,
  `settingsObject`, `welcomeObject` (métodos no implementados = stub que
  evita romper el flujo JS).

## Eliminado (Steam)

`Steamworks::{QueryWorkshop, ...}`, `EditorWindow::RequestPublishToSteamDelegate`,
`CheckWorkshopAgreementPrompt`, `IsWorkshopAgreementValid`,
`OnCollectionDetailsQueried`, `RunMobileDeviceDiscovery`,
`SteamAPI_*`, `-steamlanguage` (alias local). Publicación → `PublishToFolder`.

Uso responsable: solo con software que poseas o estés autorizado a examinar.
