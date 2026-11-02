// distribution.h — Instalación/actualización de la distribución local.
//
// RECONSTRUIDO con REA + Rizin desde launcher.exe.
// Función original: fcn.14000b6f0 (referencia "InstallDistribution: Failed
// opening existing process handles for shutdown."). Usa Toolhelp32 para
// cerrar procesos, std::filesystem para copiar/borrar. Todos los mensajes
// "InstallDistribution: ..." y "core_updater_status_*" se preservan.
//
// SIN Steam: no hay tickets, ni Workshop, ni steam_api. Solo archivos locales.
#pragma once

#include <string>

// Cierra los procesos indicados (por nombre de exe) esperando su salida.
// Mensajes originales: core_updater_status_closing_processes*,
// "InstallDistribution: TerminateProcess failed with %i (%S)", etc.
bool CloseProcessesForUpdate(const wchar_t* const* exeNames, size_t count);

// Copia recursiva distroDir -> installDir con std::filesystem.
// Mensajes originales: core_updater_status_distro_*,
// "InstallDistribution: %S -> %S fs::copy error %s", etc.
bool CopyDistribution(const std::wstring& distroDir, const std::wstring& installDir);

// Entrada principal. Retorna true si la distribución quedó instalada.
bool InstallDistribution(const std::wstring& distroDir, const std::wstring& installDir);
