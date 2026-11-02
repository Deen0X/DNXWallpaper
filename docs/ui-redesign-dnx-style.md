# Rediseño de la interfaz principal (DNX UI Style)

Estado: **prototipo navegable** en `http://127.0.0.1:8477/ui2/`
(entrada de bandeja: “Interfaz nueva (beta)”).
Backup previo: `backups\2026-10-29_pre-ui-redesign\`.

## 1. Idea

La app principal deja de ser una galería con botones y pasa a ser una
**aplicación de ajustes** (estilo Configuración de Windows / Magpie):

- **Barra lateral** con las secciones: icono de línea + nombre.
- **Área de contenido** con tarjetas (`#1B1F26`, borde `#2E3542`, radio 14).
- Mismo lenguaje visual que “Adaptar a dispositivo”.

El **catálogo no es una sección permanente**: es una **ventana de selección**
que se abre cuando una sección necesita un wallpaper (como “Abrir fichero”).
La sección guarda el elemento elegido; el catálogo se cierra.

## 2. Secciones

| Sección | Contenido |
|---|---|
| **Pantallas** | Una tarjeta por monitor: wallpaper actual, botón “Elegir wallpaper…”, encuadre por monitor. |
| **Catálogo** | Vista completa (opcional) para explorar y gestionar plugins, con buscador y filtros por fuente. |
| **Herramientas** | Accesos: “Adaptar a dispositivo” (device-editor) y futuro “Editor de wallpapers”. |
| **Salvapantallas** | Activar/desactivar, wallpaper elegido (con el mismo selector), espera en minutos. |
| **Playlist** | Lista ordenada de wallpapers con “Añadir del catálogo…”, reordenar y quitar. |
| **Acerca de** | Avatar del proyecto, versión, autor. |

Flujos:

```
Pantallas  → [Elegir wallpaper…] → Catálogo (modal) → selecciona → tarjeta del monitor
Salvapantallas → [Elegir wallpaper…] → mismo modal → se guarda en la sección
Playlist   → [Añadir del catálogo…] → modal → se añade a la lista
Herramientas → abre el editor con el wallpaper elegido
```

## 3. Selector de catálogo (modal reutilizable)

- Una sola implementación (`openPicker(title, callback)`).
- Cabecera: título + buscador + cerrar.
- Rejilla con paginación incremental (“Cargar más”, 60 por página).
- Devuelve `{key, title}` al llamador; el llamador decide qué hacer.

## 4. Lo que falta para que sea funcional

El prototipo ya lee `/api/library` y `/api/thumb/<key>`, pero el host todavía
no expone:

1. **Monitores**: lista (nombre, resolución, posición), wallpaper actual y
   aplicar por monitor (`/api/monitors`, `/api/apply?monitor=`).
2. **Salvapantallas**: leer/escribir ajustes (activado, espera).
3. **Playlist**: leer/guardar (ya existe `savePlaylist` en el bridge de la UI
   antigua; hay que exponerlo por HTTP).
4. **Plugins**: configuración desde la UI (`plugin config`) y habilitar/deshabilitar.

## 5. Notas de implementación

- `dist/res/dnxui/` (HTML/CSS/JS sin framework), servido por `lan_server` en
  `/ui2/`.
- Tema: variables CSS `--bg/--card/--line/--accent` iguales que el editor.
- Iconos: SVG en línea, trazo 1.6 (líneas simples).
- La UI antigua sigue intacta; la nueva se abre aparte hasta que cubra todo.
