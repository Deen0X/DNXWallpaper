# Rediseño de la UI — DNXWallpaper v3

> Estado: **análisis cerrado; implementación pendiente** (2026-10-28).
> Sustituye a la UI original (AngularJS minificado) por una UI propia.
> Referencias visuales: Configuración de Windows 11 y Magpie.
> Base de diseño: skill `dnx-ui-style` (Fluent/DNX: Mica, Settings Cards,
> tipografía Segoe UI Variable, iconos monoline).

---

## 1. Por qué se reescribe

La UI actual es el bundle Angular original con parches de CSS/JS. No está
“hecha por parches”: **es** la UI original con parches. Eso produce:

| Problema | Detalle | Efecto |
|---|---|---|
| Cuatro capas de navegación | Installed/Explore/Workshop + selector de monitor + grupo wallpaper/screensaver + botones Mobile/Monitors/Settings | El usuario no sabe dónde está cada cosa |
| Ajustes duplicados | Modal de ajustes + ruta `/settings` | Dos experiencias; la página queda muerta |
| Panel derecho sobrecargado | Preview + metadatos + ajuste + QR + propiedades + playlist | Sin jerarquía; el QR ya se quita |
| Filtros que empujan | El panel de filtros se abre encima de la grid | Salto de layout y pérdida de contexto |
| Playlist en barra inferior | Barra + modal de configuración | Compite con la grid; no encaja |
| Steam oculto, no eliminado | Alertas, MOTD, EULA, workshop, autor, backup, trial, avatar | Huecos, estados vacíos y código que sigue ejecutándose |
| Iconografía mezclada | Font Awesome + iconos propios + icono de Steam en Plugins | Inconsistencia; delata el origen |
| Tipografía/espaciado | Skin original | Sin escala clara; contraste irregular |
| Sin estados | Carga, vacío, error | Grid en blanco o con huecos |
| Búsqueda por vista | Solo en la grid | No hay búsqueda global |
| “Installed” mezcla orígenes | Carpetas locales + descargas remotas | No se distingue local/descargado/importado |
| Badges “portrait” | Herencia del móvil original | Sin significado |
| Ruta `/editor` | Editor de escenas del original | No se usa; el generador será externo |
| Mini modo | Resize + CSS | No es un modo compacto real; parked |
| Marca | Nombre comercial anterior en textos/locales | Hay que sustituirlo por DNXWallpaper v3 |

**Conclusión**: para eliminar Steam/Workshop/DLC/EULA de verdad hay que
sustituir la UI. Mientras tanto solo se puede ocultar.

---

## 2. Principios de diseño

- **Una sola jerarquía**: sidebar fija + área de contenido. Sin pestañas
  superiores duplicadas.
- **Settings Cards**: cada opción en una tarjeta (8 px, borde 1 px, padding
  16/12), con título, descripción y control a la derecha.
- **Una acción primaria por pantalla**; secundarias en menú “⋯”.
- **Progressive disclosure**: el detalle va en un panel lateral o en un diálogo,
  no todo a la vez.
- **Estados diseñados**: skeleton, vacío con acción, error con reintentar.
- **Búsqueda global** (`Ctrl+K`) y búsqueda contextual en Biblioteca/Descubrir.
- **Teclado**: foco visible, Enter/Espacio, Escape, flechas en la grid.
- **Tema**: claro/oscuro/sistema, acento del sistema, Mica/Acrylic, alto
  contraste.
- **Iconos monoline** (Lucide, SVG, MIT). Nada de Font Awesome ni Steam.
- **Tipografía**: Segoe UI Variable; 24/28 títulos, 14 cuerpo, 12 secundario.
- **Controles**: `ToggleSwitch` en vez de checkbox; botones de 40 px; combos de
  45 px; radios 8 px.
- **Preview**: clic → lightbox a pantalla completa con play/pausa. No ventana
  aparte.

---

## 3. Arquitectura de información

| Sección | Contenido | Sustituye |
|---|---|---|
| **Inicio** | Wallpaper actual, monitores, acciones rápidas, recientes, salud de plugins/LAN, accesos a herramientas | — |
| **Biblioteca** | Plugin local: grid, búsqueda, filtros, orden, importar, detalle, aplicar, playlist, enviar a dispositivo, eliminar | “Installed” |
| **Descubrir** | Catálogo de plugins fuentes: selector, búsqueda, filtros, descarga, salud | “Explore” + “Workshop” |
| **Reproducción** | Playlist: lista, reordenar, añadir, intervalo, transición, shuffle, por monitor | Barra inferior + modal |
| **Pantallas** | Layout visual, clone/span/independent, asignación por monitor, screensaver | “Displays” + cabecera de monitor |
| **Plugins** | Fuentes / Wallpapers / Extras: activar, configurar, salud, añadir, logs | Pestaña Workshop + ajustes de plugins |
| **Extras** | Overlays: widgets, datos, posición, estilo, preview | — (nuevo) |
| **Herramientas** | Adaptar a dispositivo, generador, futuras; exportaciones recientes | “Móvil” + “Create” |
| **Ajustes** | General, Rendimiento, Reproducción, Red/Móvil, Almacenamiento, Acerca de | Modal + `/settings` |
| **Acerca de** | Versión, licencias, logs, créditos | “About” |

Notas:
- “Inicio” es un dashboard, no una pantalla de bienvenida.
- “Extras” puede aparecer también como categoría dentro de Plugins; la sección
  top-level es la experiencia de usuario.
- El mini modo no entra en la v1 del rediseño; se revisa después.
- El editor de escenas desaparece; el generador es una herramienta externa.

---

## 4. Pantallas

### 4.1 Inicio
- Hero con el wallpaper actual (vídeo mudo en loop) + título + monitores +
  acciones: Pausa, Cambiar, Playlist.
- Tarjetas de acción: “Enviar al móvil”, “Adaptar a dispositivo”, “Generador”,
  “Plugins”.
- Recientes (horizontal).
- Estado: servidor LAN (IP/puerto/dispositivos), catálogo (nº ítems/última
  actualización), almacenamiento (caché/exportaciones).

### 4.2 Biblioteca
- Toolbar: búsqueda, chips de filtros, orden, vista grid/lista, importar.
- Grid de tarjetas: póster, título, badges (vídeo/imagen, resolución,
  descargado/actual), hover con preview, selección múltiple.
- Panel de detalle (derecha; página completa en ventana estrecha): preview,
  metadatos, ajuste, aplicar, añadir a playlist, enviar a dispositivo,
  herramientas, eliminar, abrir carpeta, propiedades.
- Barra de selección inferior para acciones en lote.
- Drag & drop desde Explorer para importar.
- Filtro por origen: local, descargado, importado.
- Badge “Aplicado” en el wallpaper actual.

### 4.3 Descubrir
- Selector de fuentes (chips/tarjetas) de plugins `catalog` habilitados.
- Búsqueda, filtros (categoría, etiquetas, resolución, tipo, tamaño, fuente),
  orden, paginación/infinite scroll.
- Grid con las mismas tarjetas que Biblioteca.
- Detalle: preview (miniatura animada o streaming si está disponible),
  metadatos, descarga con progreso, añadir a playlist, enviar a dispositivo si
  ya está descargado.
- Estados por plugin: cargando, error, `CONFIG_REQUIRED` (botón Configurar),
  rate-limited.

### 4.4 Reproducción
- Lista de la playlist con miniaturas, drag para reordenar, quitar.
- Añadir desde Biblioteca.
- Intervalo, transición (crossfade), shuffle, activar/desactivar.
- Por monitor cuando el modo es independent.
- Botón “Probar transición”.

### 4.5 Pantallas
- Layout visual de monitores (rectángulos con número, resolución, escala,
  principal). Botón “Identificar”.
- Modo: Clone / Span / Independent (tarjetas con descripción).
- Asignación por monitor en independent: wallpaper/playlist/ninguno.
- Screensaver: igual que el wallpaper o separado; abrir diálogo de Windows.
- Solo se lee la disposición de Windows; no se cambia.

### 4.6 Plugins
- Grupos: Fuentes, Wallpapers, Extras.
- Tarjeta por plugin: icono, nombre, versión, autor, descripción, capacidades,
  toggle, Configurar, salud, “requiere helper”, abrir carpeta/logs.
- Añadir plugin (carpeta/manifiesto) para catálogos; los built-in no se pueden
  eliminar, solo desactivar.
- Configuración: formulario generado desde `plugin.json` (`config.mode=form`) o
  editor externo (`external`). Tipos: texto, secreto, número, toggle, select,
  ruta, lista de rutas.
- Errores `CONFIG_REQUIRED` con acción directa.
- Icono de la sección: `plug`/`puzzle` monoline; nunca Steam.

### 4.7 Extras
- Lista de widgets; añadir; configurar; preview; on/off global; por monitor.
- Proveedores: ping, RTSS, HWiNFO, CPU/RAM/GPU, now playing, reloj, clima,
  HTTP JSON.
- Widgets: texto, barra, sparkline, icono.
- Posición por anclas o drag en una preview del wallpaper.
- Se pintan en la capa del wallpaper (debajo de iconos; no clicables).

### 4.8 Herramientas
- Tarjetas: “Adaptar a dispositivo” (device-export), “Generador”, futuras.
- Exportaciones recientes: tamaño, fecha, QR, abrir carpeta, borrar.
- Registro `tools.json`: añadir/editar/quitar herramientas; lanzar con contexto.

### 4.9 Ajustes
- General: idioma, tema, acento, autostart, bandeja, iniciar minimizado.
- Rendimiento: fps, calidad, HDR, pausas (fullscreen, maximizado, bloqueo,
  batería), GPU.
- Reproducción: ajuste por defecto, volumen, velocidad (cuando exista).
- Red/Móvil: puerto, token, firewall, dispositivos.
- Almacenamiento: caché, descargas, exportaciones, limpiar.
- Acerca de: versión, licencias, logs.

---

## 5. Modelo de plugins (resumen)

| Tipo | Implementación | Ejemplos | Config |
|---|---|---|---|
| `catalog` | CLI + JSON (contrato v1) | Local (built-in), Telegram, Pixabay, Wallhaven… | `form` / `external` |
| `wallpaper` | Nativo o helper con textura | Vídeo/imagen (built-in), Web (CEF helper), Aplicación (WGC) | `form` |
| `extras` | Proveedor de datos + widgets | Ping, RTSS, reloj, clima | `form` + preview |

Un CLI no puede entregar texturas por frame con la latencia de un wallpaper;
por eso los renderers son nativos/helper, aunque la UI los presente como
plugins. Los catálogos sí son CLI.

El **plugin local** sustituye a “Installed”: built-in, configura carpetas,
subcarpetas, extensiones, descargas y auto-refresco. La Biblioteca es su
catálogo.

---

## 6. Estrategia técnica

- **Reescritura**, no reskin. La UI vieja queda en `/legacy` solo durante la
  transición.
- **Svelte + Vite + TypeScript**; salida estática servida por `wpx.internal`.
  CSS con variables (tokens DNX). Sin framework pesado.
- **Lucide** para iconos (SVG inline).
- **Mica** vía DWM (`DWMWA_SYSTEMBACKDROP_TYPE`) en Win11; fallback a color
  sólido/Acrylic en Win10; CEF con fondo transparente.
- Titlebar propio; arrastre en C++; botones por bridge.
- **Bridge tipado** `window.dnx` sobre el dispatcher actual; métodos nuevos para
  plugins/config, herramientas, extras, tema e i18n.
- i18n desde el día 1: claves + JSON por idioma; es/en primero.
- La UI nueva no reutiliza el bundle Angular; los locales originales no se
  usan.

---

## 7. Fases y estimación

| Fase | Contenido | Semanas |
|---|---|---|
| 0 | Tokens DNX, shell (titlebar/sidebar/router), Ajustes | 1–2 |
| 1 | Biblioteca, plugin local, Plugins + config, Inicio | 4–6 |
| 2 | Descubrir, Reproducción, Pantallas, Herramientas | 3–4 |
| 3 | Extras UI, limpieza UI vieja/Steam, marca v3 | 2–3 |

Total realista: **8–12 semanas** de una persona, más pulido.

---

## 8. Riesgos

- La grid (virtual scroll, hover preview, selección múltiple, drag&drop) es lo
  más caro; prototiparla primero.
- El bridge actual está pensado para la UI original; hay que mapear y simplificar.
- Mica/transparencia + CEF + DPI: probar 100/125/150 %.
- La eliminación de Steam requiere borrar también los métodos del bridge y los
  shims; si se deja la UI vieja, no se puede eliminar del todo.
- El i18n nuevo necesita su propio flujo de traducción; la idea del usuario está
  pendiente de analizar.

---

## 9. Decisiones abiertas

1. Stack: Svelte + Vite (recomendado) o vanilla TS/React.
2. ¿“Extras” top-level o dentro de Plugins?
3. ¿“Inicio” como dashboard o eliminarlo y arrancar en Biblioteca?
4. ¿Mini modo se recupera en la UI nueva o se descarta?
5. Flujo de traducción (idea del usuario).
