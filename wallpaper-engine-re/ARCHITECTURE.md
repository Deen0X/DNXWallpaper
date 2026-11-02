# Arquitectura — Wallpaper Engine UI (reconstruida con REA)

```
┌─────────────────────────────────────────────────────────┐
│  BINARIO NATIVO (launcher.exe / editor, CEF host)       │
│  - Expone bindings: window.editorObject, window.ui      │
│  - Sirve archivos: http://wpx.internal/__file/<ruta>    │
│  - Responde vía: window.<metodo>Callback(...)           │
└──────────────────────▲──────────────────────────────────┘
                       │ CEF bindings + callbacks globales
┌──────────────────────┴──────────────────────────────────┐
│  UI WEB (AngularJS 1.x, módulo wallpaperbrowserApp)     │
│  index.html → vendor.js + scripts.js                    │
│                                                         │
│  Rutas (ngRoute):                                       │
│    /  /welcome        → WelcomeCtrl                     │
│    /browsewallpapers  → BrowseWallpapersCtrl            │
│    /editor            → EditorCtrl                      │
│    /settings          → SettingsCtrl                    │
│                                                         │
│  Servicios: host (bridge), modals, utils, skin,         │
│    localeLoader(+Support), compiler, contextMenu,        │
│    dragdrop                                           │
│  56 controladores · 55 directivas (canvas, timeline,     │
│  curveEditor, codeEditor, preview, knob, ...)            │
└─────────────────────────────────────────────────────────┘
                       │ project.json + scene.json
┌──────────────────────┴──────────────────────────────────┐
│  WALLPAPER (ej. examples/beach-clone/)                  │
│  project.json (manifiesto) + scene.json (cámara,        │
│  luces, objetos) + preview.jpg + models/                │
└─────────────────────────────────────────────────────────┘
```

## Decisiones de reconstrucción

1. **Bridge `host` idéntico al protocolo observado**, incluyendo limpieza de
   callbacks (`window[x] = undefined`), sufijos `genericSceneAction_<sub>` y
   supresión de errores por método. Es el contrato que el binario espera.
2. **Sin copiar el bundle**: la UI se reescribe sobre el mismo módulo, rutas,
   nombres de controladores/servicios y query params (`skinStyle`, `skinKey`,
   `uiperf`, `win11`) para mantener compatibilidad conceptual.
3. **Mock del host** (`native-bridge/mock-host.js`) con el mismo protocolo
   para desarrollar sin el binario.
4. **Formato `scene` documentado campo a campo** desde el proyecto `beach`.

## Cómo ejecutar la reconstrucción (solo UI, sin binario)

1. Servir `src/` con cualquier servidor estático.
2. Incluir `../native-bridge/mock-host.js` antes de `app.js`
   (o inyectar los bindings CEF reales desde un host propio).
3. Se necesita AngularJS 1.x + ngRoute en `scripts/vendor.js`
   (no incluido: usar el original o un CDN compatible).

## Alcance y límites

- Incluye: estructura UI, bridge, rutas, controladores base, formato scene.
- No incluye: lógica interna de los 56 controladores/55 directivas
  (minificada en 1,2 MB), motor de render, ni binario nativo.
- Siguiente paso sugerido: instrumentar con un host CEF propio
  (p. ej. CEF Python/C#) que implemente `editorObject`/`ui` y sirva
  `wpx.internal`, observando el tráfico real del editor.
