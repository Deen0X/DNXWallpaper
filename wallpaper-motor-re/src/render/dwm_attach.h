// dwm_attach.h — Inserción del wallpaper detrás de los iconos del escritorio.
//
// Mecanismo observado (clases de ventana en .rdata):
//   "DNXWallpaperV3Parent" / "DNXWallpaperV3Wallpaper"
// Técnica estándar: localizar Progman -> WorkerW (0xCDCD...) y reparentar
// la ventana de render como hija del WorkerW que está detrás de los iconos.
#pragma once

#include <windows.h>

// Crea la ventana contenedora y la inserta tras los iconos.
// INVISIBLE y totalmente transparente hasta ShowWallpaperParent (evita el
// velo oscuro sobre el escritorio cuando no hay escena).
// Retorna el HWND padre ("DNXWallpaperV3Parent") o nullptr.
HWND AttachWallpaperParent(HINSTANCE hInst, int x, int y, int w, int h);

// Muestra el padre con la opacidad dada cuando hay contenido.
// Si el WorkerW no se encontró, NO se muestra (evita tapar el escritorio).
void ShowWallpaperParent(HWND parent, BYTE alpha);

// true si el padre quedó anclado tras los iconos (WorkerW válido).
bool WallpaperParentReady();

// true si el padre quedó anclado tras los iconos (WorkerW válido).
bool WallpaperParentReady();

// Ventana hija de render ("DNXWallpaperV3Wallpaper") dentro del padre.
HWND CreateWallpaperWindow(HINSTANCE hInst, HWND parent, int w, int h);
