// ipc.h — Control entre procesos (pipes con nombre).
//
// Original: InterProcessMessageHandler::PeekNextRecvMessageCommand.
// Transporte observado: pipes \\.\pipe\WPXCMD_<id> (PIPES, no TCP).
// El launcher/manager lanza hijos con "-messagehandler <id> -parentprocess
// <pid>" (el hijo abre \\.\pipe\WPXCMD_<id>). Otros endpoints observados:
// __WPXProviderAPIMediaControlPID / __WPXProviderAPIMediaControlSSPID.
// SIN Steam: el canal "__steam_trck_hrs" NO se implementa.
#pragma once

#include <windows.h>
#include <string>
#include <functional>

// Nombre del pipe para un message-handler dado.
std::wstring PipeNameFor(const std::wstring& handlerId); // \\.\pipe\WPXCMD_<id>

// Lado servidor: atiende un pipe y despacha comandos entrantes.
// cmd: línea de comando recibida (ej. "load <ruta>", "stop", "pause").
class InterProcessMessageHandler {
public:
    using CommandCallback = std::function<void(const std::wstring& cmd)>;

    explicit InterProcessMessageHandler(const std::wstring& handlerId);
    ~InterProcessMessageHandler();

    // Original: PeekNextRecvMessageCommand — no bloqueante, retorna
    // true si había un comando pendiente (ya despachado a cb_).
    bool PeekNextRecvMessageCommand();

    void SetCallback(CommandCallback cb) { cb_ = cb; }

private:
    std::wstring pipeName_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    CommandCallback cb_;
};
