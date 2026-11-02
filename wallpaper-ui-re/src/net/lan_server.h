// lan_server.h — Servidor HTTP para la app móvil (ver docs/companion-plan.md).
#pragma once

#include <string>

// Arranca el servidor (idempotente). Lee res/config/mobile.json:
//   { "enabled": true, "port": 8477, "token": "" }
// El token vacío desactiva la autenticación (LAN doméstica).
void StartLanServer(const std::wstring& baseDir);

// Datos para el QR de la UI: {"ip":"192.168.1.10","port":8477,"enabled":true}
std::string LanInfoJson();

// Puerto real del servidor (0 si aún no se ha arrancado).
int LanPort();
