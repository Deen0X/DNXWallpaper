// tray.cpp — Icono de bandeja con menú completo.
#include "tray.h"

#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

namespace {
constexpr UINT kMenuOpen = 100;
constexpr UINT kMenuChoose = 101;
constexpr UINT kMenuFolder = 102;
constexpr UINT kMenuSettings = 103;
constexpr UINT kMenuQuit = 104;
constexpr UINT kMenuScreensaver = 105;
constexpr UINT kMenuWorkshop = 106;
constexpr UINT kMenuEditor = 107;
constexpr UINT kMenuFaq = 108;
constexpr UINT kMenuShot = 109;
constexpr UINT kMenuMute = 110;
constexpr UINT kMenuPause = 111;
constexpr UINT kMenuNewUi = 112;
constexpr UINT kMenuRecentBase = 1000; // + índice
} // namespace

UINT TrayIcon::TrayMessage() {
    static UINT msg = 0;
    if (!msg) {
        msg = RegisterWindowMessageW(L"DNXWallpaperTray");
    }
    return msg;
}

bool TrayIcon::Create(HWND owner, const std::wstring& iconPath,
                      const std::wstring& tip, const TrayActions& actions) {
    owner_ = owner;
    actions_ = actions;
    HICON icon = (HICON)LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON,
                                   32, 32, LR_LOADFROMFILE);
    if (!icon) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    NOTIFYICONDATAW d{};
    d.cbSize = sizeof(d);
    d.hWnd = owner;
    d.uID = 1;
    d.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    d.uCallbackMessage = TrayMessage();
    d.hIcon = icon;
    wcsncpy_s(d.szTip, tip.c_str(), _TRUNCATE);
    created_ = Shell_NotifyIconW(NIM_ADD, &d) == TRUE;
    if (created_) {
        d.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &d);
    }
    return created_;
}

void TrayIcon::Remove() {
    if (created_ && owner_) {
        NOTIFYICONDATAW d{};
        d.cbSize = sizeof(d);
        d.hWnd = owner_;
        d.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &d);
        created_ = false;
    }
}

void TrayIcon::ShowMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Abrir DNXWallpaper v3");
    AppendMenuW(menu, MF_STRING, kMenuChoose, L"Cambiar fondo");

    // Fondos recientes (submenú).
    HMENU recent = CreatePopupMenu();
    std::vector<std::wstring> items;
    if (actions_.getRecent) items = actions_.getRecent();
    int n = 0;
    for (const auto& it : items) {
        std::wstring label = it;
        size_t slash = label.find_last_of(L"\\/");
        if (slash != std::wstring::npos) label = label.substr(slash + 1);
        AppendMenuW(recent, MF_STRING, kMenuRecentBase + n, label.c_str());
        if (++n >= 10) break;
    }
    if (n == 0) {
        AppendMenuW(recent, MF_STRING | MF_GRAYED, 0, L"(sin fondos recientes)");
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)recent, L"Fondos recientes");

    AppendMenuW(menu, MF_STRING, kMenuScreensaver, L"Cambiar salvapantallas");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuWorkshop, L"Buscar en plugins");
    AppendMenuW(menu, MF_STRING, kMenuEditor, L"Adaptar a dispositivo…");
    AppendMenuW(menu, MF_STRING, kMenuNewUi, L"Interfaz nueva (beta)");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Configuración");
    AppendMenuW(menu, MF_STRING, kMenuFaq, L"FAQ y soporte");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuShot, L"Hacer captura de pantalla");
    AppendMenuW(menu, MF_STRING | ((actions_.isMuted && actions_.isMuted())
                                       ? MF_CHECKED
                                       : 0),
                kMenuMute, L"Silenciar");
    AppendMenuW(menu, MF_STRING | ((actions_.isPaused && actions_.isPaused())
                                       ? MF_CHECKED
                                       : 0),
                kMenuPause, L"Pausar");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuFolder, L"Abrir carpeta de fondos");
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Salir");

    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(owner);
    PostMessageW(owner, WM_NULL, 0, 0);
    UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y,
                                    0, owner, nullptr);
    if (cmd >= kMenuRecentBase) {
        int idx = (int)cmd - (int)kMenuRecentBase;
        if (idx >= 0 && idx < (int)items.size() && actions_.onRecent) {
            actions_.onRecent(items[idx]);
        }
    } else {
        switch (cmd) {
        case kMenuOpen:
        case kMenuChoose:
            if (actions_.onChooseWallpaper) actions_.onChooseWallpaper();
            else if (actions_.onOpenUi) actions_.onOpenUi();
            break;
        case kMenuFolder:
            if (actions_.onOpenFolder) actions_.onOpenFolder();
            break;
        case kMenuScreensaver:
            if (actions_.onScreensaver) actions_.onScreensaver();
            break;
        case kMenuWorkshop:
            if (actions_.onWorkshop) actions_.onWorkshop();
            break;
        case kMenuNewUi:
            if (actions_.onNewUi) actions_.onNewUi();
            break;
        case kMenuEditor:
            if (actions_.onEditor) actions_.onEditor();
            break;
        case kMenuSettings:
            if (actions_.onSettings) actions_.onSettings();
            break;
        case kMenuFaq:
            if (actions_.onFaq) actions_.onFaq();
            break;
        case kMenuShot:
            if (actions_.onScreenshot) actions_.onScreenshot();
            break;
        case kMenuMute:
            if (actions_.onToggleMute) actions_.onToggleMute();
            break;
        case kMenuPause:
            if (actions_.onTogglePause) actions_.onTogglePause();
            break;
        case kMenuQuit:
            if (actions_.onQuit) actions_.onQuit();
            break;
        default:
            break;
        }
    }
    DestroyMenu(menu);
}

bool TrayIcon::HandleMessage(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg != TrayMessage()) {
        return false;
    }
    UINT id = HIWORD(l) ? HIWORD(l) : (UINT)w;
    if (id != 1) {
        return false;
    }
    WORD evt = LOWORD(l);
    // Doble clic: abrir la UI.
    if (evt == WM_LBUTTONDBLCLK) {
        if (actions_.onOpenUi) actions_.onOpenUi();
        return true;
    }
    // Clic izquierdo (NIN_SELECT/keys) y derecho: menú. NIN_POPUPOPEN se
    // ignora a propósito: el shell lo envía al pasar el ratón por encima.
    if (evt == WM_LBUTTONUP || evt == NIN_SELECT || evt == NIN_KEYSELECT ||
        evt == WM_RBUTTONUP || evt == WM_CONTEXTMENU || evt == WM_RBUTTONDBLCLK) {
        ShowMenu(hwnd);
        return true;
    }
    return false;
}
