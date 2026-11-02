# Formato de proyecto Wallpaper Engine (tipo `scene`)

Documentado a partir del proyecto oficial `beach`
(`projects/defaultprojects/beach/`).

## Archivos

| Archivo | Propósito |
|---|---|
| `project.json` | Manifiesto del wallpaper (título, tipo, preview, propiedades) |
| `scene.json` | Escena: cámara, iluminación global y lista de objetos |
| `preview.jpg` | Miniatura visible en el navegador de wallpapers |
| `models/*.json` | Modelos/imágenes que componen la escena |
| `materials/`, `shaders/` | Materiales y shaders personalizados |

## `project.json`

| Campo | Tipo | Descripción |
|---|---|---|
| `file` | string | Archivo de escena principal (`scene.json`) |
| `title` | string | Título visible |
| `type` | string | Tipo de wallpaper (`scene`, `video`, `web`, ...) |
| `preview` | string | Ruta de la miniatura |
| `visibility` | string | `public` / privada |
| `official` | bool | `true` solo en contenido oficial |
| `authorsteamid` | string | SteamID64 del autor |
| `general.properties` | objeto | Propiedades editables por el usuario (`{ text, type, value }`; ej. `color` con valor `"r g b"` en rango 0-1) |

## `scene.json`

- `camera`: `center`, `eye`, `up` (vectores `"x y z"`).
- `general`: `ambientcolor`, `skylightcolor`, `clearcolor` (colores `"r g b"`),
  `bloom` (bool), `norecompile` (bool),
  `orthogonalprojection` (`width`/`height`, ej. 1920x1080).
- `objects[]`: cada objeto tiene `id` numérico único, `name`, `image`
  (ruta al modelo), `origin` (`"x y z"` en píxeles), `angles` (`"x y z"` en
  grados), `scale` (`"x y z"`).

## Tipos de wallpaper (observados en la UI)

`scene` (escena 2D/3D), `video`, `web`, `application`, además de assets
`image`/`texture`, `particle`, y capas/effects/sons (`importImage`,
`importSound`, `addSound`, `importTextureFile`, `createParticle` vía bridge).
