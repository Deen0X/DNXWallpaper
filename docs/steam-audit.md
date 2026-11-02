# Eliminación de Steam / Workshop / DLC / EULA

> Estado: **inventario y plan** (2026-10-28). La eliminación real del bundle
> original llega con la UI nueva (`docs/ui-redesign.md`).
> Objetivo: que DNXWallpaper v3 no contenga ninguna referencia funcional ni
> visible a Steam, Workshop, DLC, EULA ni al nombre comercial anterior.

---

## 1. Estrategia

La UI actual es un bundle AngularJS minificado con las vistas del producto
original. **No se puede “borrar” Steam de forma quirúrgica** sin romperlo.

Plan:

1. **Ahora (Fase 0)**: retirar del bridge los métodos de Steam/Workshop y los
   shims locales; ocultar en la UI vieja lo que dependa de ellos; actualizar la
   marca visible. Es una limpieza parcial y temporal.
2. **UI nueva (Fase 3)**: no incluye ninguna vista, cadena ni método de Steam.
   Al llegar a paridad se borra el bundle Angular, sus locales y el CSS de
   ocultación. Esa es la eliminación real.
3. **Docs**: los documentos de ingeniería inversa pueden mencionar el origen
   técnico; la UI y los textos de usuario, no.

No se edita el JS minificado a mano. Las cadenas visibles se controlan con
locales/overrides mientras exista la UI vieja.

---

## 2. Inventario preliminar

Menciones en el código (incluye código, claves y textos):

| Fichero | “steam” | “workshop” |
|---|---:|---:|
| `dist/ui/dist/scripts/scripts.js` | 296 | 687 |
| `wallpaper-ui-re/src/cef/cef_app_impl.cpp` | 29 | 50 |

Las cadenas visibles son un subconjunto. Los ficheros de idioma existen en
`dist/locales/ui_*.json` y `core_*.json`.

### 2.1 Áreas visibles

- Cuenta/avatar de Steam, “Connecting to Steam”, “Steam Services Unavailable”.
- Pestañas/fuentes “Installed / Explore / Workshop”.
- Suscribir, valorar, favoritos, comentar, autor, “preset de Workshop”.
- Publicar: assets, EULA, rating de contenido, reglas, visibilidad.
- Tienda, DLC, “advertise workshop”, trial/MOTD, backup/convert.
- Página móvil con el mensaje de instalar el producto original desde Steam.
- Wizard/FAQ/ajustes con textos de Steam.
- Badges “portrait” (móvil del original).
- Icono de Steam en el botón de Plugins/Workshop.
- Avisos de backup offline, filtros por autor, “Steam offline notice”.

### 2.2 Métodos del bridge

De `cef_app_impl.cpp` (`kBrowseMethods`, `kUiMethods`, `kEditorMethods`):

- Cuenta/auth: `pollSteamUserInfo`, `refreshAuthToken`, `updateProfile`,
  `profilesChanged`, `getUserShortcutSystemHash`.
- Workshop: `queryWorkshop`, `queryWorkshopIds`, `subscribeWorkshopItem`,
  `unsubscribeWorkshopItem`, `updateWorkshopItemMetaData`, `setFavorited`,
  `setUserVote`, `getUserVote`, `openStorePage`, `openSteamWorkshopPage`.
- Publicación/editor: `preparePublishAssets`, `writeProjectFile`,
  `choosePublishMethod`, `preparePublishPreset`, `convertWorkshopBackup`,
  `isDLCInstalled`, `checkWallpaperPKGVersions`, `getWallpaperResolutions`.
- Móvil del original: `discoverMobileDevices`, `requestMobileDeviceUpload`,
  `exportMobilePkg`, `cancelMobileUploads`, `removeMobileDevice`,
  `getScenePreviewImage`, `installEdgeWebView`.
- Shims nuestros (temporales): `queryWorkshop` local, `steamWorkshopStatus`,
  el matador del modal “Steam Services Unavailable”, la lista CSS de ocultación,
  el overlay “Connecting to Steam”.

### 2.3 Rutas/templates

- `views/views/browsewallpapers.html` (fuentes Installed/Explore/Workshop,
  alertas, trial, backup, autor, monitor).
- `views/templates/workshopdetails.html`.
- `views/templates/mobilemanager.html`.
- `views/views/editor.html` (editor de escenas; se sustituye por herramienta
  externa).
- `views/views/settings.html` (ajustes duplicados; la UI nueva los sustituye).
- `views/views/welcome.html` (textos de Steam).
- Modales de publicación/EULA/rating/reglas.

---

## 3. Matriz de acciones

| Elemento | Acción ahora | Acción con la UI nueva |
|---|---|---|
| Textos visibles de Steam/Workshop | Ocultar/override de locale | Eliminar con el bundle |
| Botón/pestaña Workshop | Ocultar (ya) | Eliminar |
| Icono de Steam | Sustituir por `plug`/`puzzle` | Eliminar |
| Cuenta/avatar/tienda/DLC | Ocultar | Eliminar |
| Publicar/EULA/rating | Ocultar | Eliminar |
| Autor/favoritos/votos/comentarios | Ocultar | Eliminar |
| Backup/convert/MOTD/trial | Ocultar | Eliminar |
| Métodos del bridge | Retirar del dispatcher | Eliminar |
| Shims locales | Mantener hasta que la UI vieja no los use | Eliminar |
| Rutas `/editor`, `/settings` | No tocar | Eliminar |
| Locales `ui_*`/`core_*` | No tocar | Eliminar/reemplazar |
| Nombre comercial anterior | Renombrar a DNXWallpaper v3 | Igual |

---

## 4. Checklist de cierre

- [ ] No queda ninguna cadena visible con Steam/Workshop/DLC/EULA.
- [ ] No queda ningún botón/enlace que abra tienda, Workshop o cuenta.
- [ ] Los métodos retirados no se llaman (probar arranque, biblioteca,
      descubrir, ajustes, plugins).
- [ ] El icono de Plugins no es Steam.
- [ ] Los textos de bienvenida/FAQ/Acerca de son de DNXWallpaper v3.
- [ ] La UI nueva no importa el bundle Angular ni sus locales.
- [ ] El CSS de ocultación se ha borrado.
- [ ] `docs/` de usuario no menciona Steam/Workshop; la documentación técnica de
      ingeniería inversa puede conservar el contexto histórico.

---

## 5. Notas

- “Ocultar” no es el objetivo final; es una transición. La eliminación real es
  la UI nueva.
- El renombrado de binarios/carpetas internas (`wallpaperui.exe`,
  `wallpaper64.exe`) puede hacerse después, para no romper rutas, scripts y
  accesos directos. La marca visible es DNXWallpaper v3.
- El icono de la sección de plugins será un `plug`/`puzzle` monoline (Lucide).
---

## 6. Progreso 2026-10-28 (Fase 0)

Hecho:
- Título de la UI sin “Steam Edition”; `ui_caption_browse` limpio en todos los
  locales.
- Icono de Plugins sustituido por un `plug` monoline (inyección JS).
- Botones del editor (`callbackShowEditor`/`callbackOpenInEditor`) ocultos.
- Textos visibles de descarga/estado/tray neutralizados; valores de locales
  limpiados (`Steam`→`DNX`, `Workshop`→`Plugins`, `DLC`→`Extras`,
  `EULA`→`Acuerdo`).
- Bridge `editorObject` y la lista `kEditorMethods` eliminados.
- `dist/` sin exes ni logs antiguos; `DNXWallpaper.UI.exe` con regla de firewall
  privada (8477).

Pendiente (Fase 3.3):
- Borrar el bundle Angular y sus locales.
- Eliminar los métodos/shims internos de Steam/Workshop que la UI vieja aún
  invoca (`pollSteamUserInfo`, `queryWorkshop`, `subscribeWorkshopItem`, etc.).
- Borrar el CSS de ocultación y los matchers del modal.
- Decidir qué hacer con `wallpaper-engine-re/` y `evidence/` (archivo histórico
  de REA; no se empaqueta).

