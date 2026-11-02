# Evidencia REA — Ingeniería inversa de Wallpaper Engine UI

Herramienta: `rea` (rea-agents v6.8.0, paquete npm global).
Objetivo: `D:\SteamLibrary\steamapps\common\wallpaper_engine\ui` (bundle web de la app).
Fecha: 2026-10-25.

## Comandos ejecutados

1. `rea analyze "…\ui" --format json`
   → timeout parcial por Monaco embebido (límite 300s por fuente).
2. `rea analyze-javascript-application "…\ui" --source-path dist/index.html
   --source-path dist/scripts/scripts.js --analysis-timeout-ms 600000`
   → éxito. Evidencia en `D:\Temp\opencode\we-core.json` (47 MB).
3. `rea recover-javascript-sources scripts.js <dir>`
   → no disponible: requiere Wakaru ^1.13.0 en Linux x64 (`capability_unavailable`).
   Se reconstruyó manualmente a partir del grafo semántico + lectura del bundle.

## Resultados del análisis (normalized_result)

- `statistics`: 64 archivos relevantes, 1 JS parseado (`scripts.js`),
  172.306 nodos AST visitados, 304 findings, 0 fallos de parseo.
- `semantic_graph`: 20.058 nodos
  (8.497 call-site, 3.969 function, 3.647 parameter, 2.798 binding,
  1.088 return-site, 59 module).
- `summary.ipc`: 0 operaciones (no es app Electron con contextBridge;
  el bridge es CEF nativo, fuera del modelo IPC de REA → documentado manualmente).

## Hallazgos estructurales (bundle scripts.js, 1.245.189 caracteres)

- Framework: AngularJS 1.x, módulo `wallpaperbrowserApp`, `ng-view` (ngRoute).
- Rutas (`.when("…")`): `/`, `/browsewallpapers`, `/editor`, `/settings`, `/welcome`.
- 56 controladores (Browse*/Editor*/Settings/Welcome + ~20 modales).
- 55 directivas (canvas, timeline, curveEditor, knob, codeEditor, preview, ...).
- 9 servicios: compiler, contextMenu, dragdrop, host, localeLoader,
  localeLoaderSupport, modals, skin, utils.
- Menciones: `wallpaper` x1319, `cef` x23, `native` x35, `external` x60.

## Hallazgo principal: servicio `host` (bridge CEF)

Definición en el bundle (offset ~303129, `.service("host", ["$q","$http",
"$timeout","$translate","$httpParamSerializer","modals","utils", ...])`):

- `callDeferred(obj, method, ...args)`: registra
  `window[method+"Callback"]` / `window[method+"CallbackError"]`, invoca
  `window[obj][method](...args)`, devuelve promesa `$q`.
- `callEditor(m, ...)` → `callDeferred("editorObject", m, ...)` (194 usos).
- `callUI(m, ...)` → `callDeferred("ui", m, ...)`.
- Sufijos: `genericSceneAction_<sub>`, `modelAction_<sub>`.
- `window.wpxGlobalData`, `window.editorCtrl`, `window.browseWallpapersCtrl`, etc.
- `window.toLocalUrl(p)` → `http://wpx.internal/__file/<p>` (archivos locales).
- Backend: `https://www.wallpaperengineapi.com` (dev: `http://localhost:3000`).
- Auth Steam: `ui.refreshAuthToken`, reintento x3.

## Formato de proyecto (verificación directa en disco)

`projects/defaultprojects/beach/`: `project.json` + `scene.json` +
`preview.jpg` + `models/` (+ `materials/`, `shaders/` vacíos).
Tipos UI: scene, video, web, application, image/texture, particle.
Skins: `ui/skins.json` + `ui/dist/styles/skin*.css` (12 variantes).
Query params: `skinStyle`, `skinKey`, `uiperf` (1|2), `win11`.

## Limitaciones de la evidencia

- Bundle minificado: funciones anónimas, grafo sin nombres → firmas
  reconstruidas por posición/patrón + verificación manual de contextos.
- Sin Wakaru en Windows: no hay recuperación automática de módulos;
  la reconstrucción es arquitectónica (bridge + rutas + controladores +
  formato), no una copia línea a línea.
- El binario nativo (launcher.exe/editor) queda fuera del alcance
  (requeriría Hopper/Ghidra/IDA como proveedor REA).
