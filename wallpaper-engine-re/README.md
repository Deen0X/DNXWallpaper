# wallpaper-engine-re

Reconstrucción arquitectónica de la interfaz de **Wallpaper Engine**
(`steamapps/common/wallpaper_engine/ui`) mediante ingeniería inversa con
**REA** (rea-agents v6.8.0). Detalle de evidencia en `evidence/rea-findings.md`.

## Contenido

| Ruta | Descripción |
|---|---|
| `src/index.html` | Estructura de la página (skins, perf flags, ng-view) |
| `src/app.js` | Módulo `wallpaperbrowserApp`, rutas y utilidades globales |
| `src/services/host.js` | **Bridge CEF** `host` reconstruido (callDeferred/callEditor/callUI) |
| `src/controllers/main.js` | Controladores principales (Browse/Editor/Settings/Welcome) |
| `native-bridge/bridge-protocol.md` | Documentación del protocolo nativo |
| `native-bridge/mock-host.js` | Mock de `window.editorObject`/`window.ui` para desarrollo |
| `examples/beach-clone/` | Wallpaper `scene` de ejemplo (project.json + scene.json) |
| `examples/project-format.md` | Formato de proyecto documentado campo a campo |
| `ARCHITECTURE.md` | Diagrama y decisiones de reconstrucción |
| `evidence/rea-findings.md` | Evidencia REA completa con comandos y limitaciones |

## Hallazgo principal

Toda la comunicación UI → binario pasa por el servicio `host`:

```js
host.callEditor('openProject', name)   // → window.editorObject.openProject(name)
host.callUI('copyToClipboard', url)    // → window.ui.copyToClipboard(url)
```

con respuesta asíncrona por callbacks globales
`window.<metodo>Callback / <metodo>CallbackError`.
Ver `native-bridge/bridge-protocol.md`.

## Uso responsable

Proyecto con fines educativos y de interoperabilidad, a partir de software
legalmente instalado. No incluye código original de Wallpaper Engine.
