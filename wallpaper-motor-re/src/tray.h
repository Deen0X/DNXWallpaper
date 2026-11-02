// tray.h — Icono de bandeja (tray) con menú nativo completo.
//
// Comportamiento (equivalente al original):
//   - clic izquierdo: abre el menú
//   - doble clic: abre la UI (elegir fondo)
//   - clic derecho: abre el menú (compatibilidad)
#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <vector>

struct TrayActions {
    std::function<void()> onOpenUi;          // "Abrir" / doble clic
    std::function<void()> onChooseWallpaper; // "Cambiar fondo"
    std::function<void(const std::wstring&)> onRecent; // "Fondos recientes"
    std::function<std::vector<std::wstring>()> getRecent;
    std::function<void()> onScreensaver;     // "Cambiar salvapantallas"
    std::function<void()> onWorkshop;        // "Buscar en Workshop" (Plugins)
    std::function<void()> onEditor;
    std::function<void()> onNewUi;     // interfaz nueva (beta)          // "Crear fondo" (editor)
    std::function<void()> onSettings;        // "Configuración"
    std::function<void()> onFaq;             // "FAQ y soporte"
    std::function<void()> onScreenshot;      // "Hacer captura de pantalla"
    std::function<void()> onToggleMute;      // "Silenciar"
    std::function<void()> onTogglePause;     // "Pausar"
    std::function<bool()> isMuted;
    std::function<bool()> isPaused;
    std::function<void()> onOpenFolder;      // "Abrir carpeta de fondos"
    std::function<void()> onQuit;            // "Salir"
};

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon() { Remove(); }

    bool Create(HWND owner, const std::wstring& iconPath,
                const std::wstring& tip, const TrayActions& actions);
    void Remove();
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

    static UINT TrayMessage();

private:
    void ShowMenu(HWND owner);
    HWND owner_ = nullptr;
    TrayActions actions_;
    bool created_ = false;
};
