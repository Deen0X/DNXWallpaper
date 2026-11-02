// wizard.h — Asistente de bienvenida (réplica funcional del original).
//
// Original (capturas): ventana "Te damos la bienvenida a DNXWallpaper v3"
// con cabecera naranja, selector de idioma (Default (Español)...), texto,
// logo, puntos de progreso (5 páginas) y botón Adelante; última página:
// "¡Ya está todo listo!", checkbox "Buscar fondos ahora", botón Todo listo.
// Réplica local en 2 páginas Win32 (sin recursos embebidos). Guarda
// res/config/welcome.json {welcomed:true, language} para no repetir.
// SIN Steam: sin Workshop, "Buscar fondos ahora" abre la carpeta local.
#pragma once

#include <string>

// Muestra el wizard si no existe welcome.json. Retorna el idioma elegido
// ("es","en",...) o "" si el usuario canceló.
std::wstring RunWelcomeWizard(const std::wstring& resDir);

// Rutas/estado del wizard (externos).
bool WelcomeDone(const std::wstring& resDir);
bool SaveWelcome(const std::wstring& resDir, const std::wstring& language,
                 bool searchNow);
