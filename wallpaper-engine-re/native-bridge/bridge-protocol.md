# Protocolo del bridge nativo (CEF)

Reconstruido con REA desde `ui/dist/scripts/scripts.js` (servicio AngularJS `host`).

## Objetos expuestos por el binario (bindings CEF)

| Objeto global | Propósito |
|---|---|
| `window.editorObject` | Acciones del editor nativo (proyectos, escena, assets, publicación) |
| `window.ui` | Utilidades nativas (portapapeles, auth Steam, teclas, log) |
| `window.wpxGlobalData` | Datos globales de sesión |

## Patrón de llamada (idéntico al original)

```
JS:  host.callEditor("openProject", name)
  -> window["openProjectCallback"] = resolve
  -> window["openProjectCallbackError"] = reject
  -> window.editorObject.openProject(name)   // binding CEF, sin retorno útil
NATIVO: ... ejecuta ...
  -> window.openProjectCallback(resultado)   // o window.openProjectCallbackError(err)
JS: promesa $q resuelta/rechazada, callbacks limpiados (undefined)
```

Variantes: `host.callEditor(...)` usa `editorObject`, `host.callUI(...)` usa `ui`.

## Nombres de método con sufijo

- `genericSceneAction` + subacción → callback `<metodo>_<subacción>Callback`
- `modelAction` + subacción → callback `<metodo>_<subacción>Callback`

## Protocolo de archivos locales

```
window.toLocalUrl(ruta) -> "http://wpx.internal/__file/<ruta-url-encoded>"
```

El binario sirve archivos locales del wallpaper bajo el esquema interno
`http://wpx.internal/__file/`. Las barras invertidas se normalizan a `/`.

## Métodos `callEditor` observados (frecuencia en el bundle)

genericSceneAction (70), sendSceneOrderChange (8), sceneMouseMove (7),
dragRenderableItem (6), saveSceneGeneralData (4), importTextureFile (4),
setViewTransforms (4), preparePublishAssets (3), importImage (3),
importSound (3), addSound (3), getBoneInfos (3), saveProjectData (3),
setAnimationPreviewFrame (3), setAnimationPreviewPlayback (3),
getEffectConfig (3), addEffectFromConfig (3), addCompositionLayer (3),
createParticle (3), centerRenderable (3), checkBadGIF (3),
getTexturePreview (3), startEffectPreview (3), choosePublishMethod (2),
listAvailableEffects (2), writeProjectFile (2), moveRenderable (2),
forceSave (2), saveEditorSettings (2), isDLCInstalled (2), addSolidLayer (2),
rotateRenderable (2), getImportFileList (2), getProjects (2),
getImportFileList/getProjects/sceneKey/selectModelTexture (2),
setGridVisible, setSelectionBoxVisible, setEditorGraphicsFlagsVisible,
checkUnsavedChanges, openProject, closeProject, deleteProject, ...

## Backend público

- `https://www.wallpaperengineapi.com` (modo dev: `http://localhost:3000`)
- Auth Steam: ticket vía `ui.refreshAuthToken`, reintento x3 ante rechazo.
