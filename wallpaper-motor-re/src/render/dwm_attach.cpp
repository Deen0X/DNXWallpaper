// dwm_attach.cpp — Fondo animado detrás de los iconos (secuencia original).
//
// RECONSTRUIDO de DNXWallpaper.Motor.exe:
//   fcn.14003c840 (localizar ancla) + callback 0x14003c760 +
//   fcn.14005aaf0 / WPEUIUnderlayWindow.
//
// Secuencia verificada empíricamente en este entorno:
//   1. ProgMan = FindWindowW("ProgMan")
//   2. SendMessageTimeoutW(ProgMan, 0x052C, 0x0D, 1, SMTO_NORMAL, 2500)
//   3. DefView = FindWindowExW(ProgMan, 0, "SHELLDLL_DefView")
//   4. Anchor   = FindWindowExW(ProgMan, DefView, "WorkerW")  <-- fondo real
//   5. Ventana HIJA (WS_CHILD) del Anchor con EXSTYLE NOACTIVATE|TRANSPARENT.
//      NO usar WS_EX_LAYERED (incompatible con swapchain DXGI) ni
//      WS_EX_NOREDIRECTIONBITMAP (CreateWindowExW falla en hijos: 126/183).
//   6. SetWindowPos(HWND_BOTTOM, NOACTIVATE|NOMOVE|NOSIZE) + SW_SHOWNOACTIVATE.
#include "dwm_attach.h"

#include <cstdio>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace {

bool g_attached = false;
HWND g_anchor = nullptr;

void LogStage(const char* s) {
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) {
        fprintf(f, "%s\n", s);
        fclose(f);
    }
}

void LogStageCode(const char* s, unsigned long code) {
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) {
        fprintf(f, "%s (0x%lX)\n", s, code);
        fclose(f);
    }
}

// Ancla del fondo: WorkerW hermano de SHELLDLL_DefView dentro de ProgMan.
// Si no existe, se fuerza con 0x052C y se reintenta.
HWND FindWallpaperAnchor() {
    HWND progman = FindWindowW(L"ProgMan", nullptr);
    if (!progman) {
        LogStage("attach: ERROR no ProgMan");
        return nullptr;
    }
    HWND def = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    HWND worker = def ? FindWindowExW(progman, def, L"WorkerW", nullptr) : nullptr;
    if (!worker) {
        SendMessageTimeoutW(progman, 0x052C, 0x0D, 1, SMTO_NORMAL, 2500, nullptr);
        for (int i = 0; i < 10 && !worker; ++i) {
            Sleep(100);
            def = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
            worker = def ? FindWindowExW(progman, def, L"WorkerW", nullptr)
                         : nullptr;
            if (!worker) {
                // Variante: WorkerW top-level que contiene DefView.
                struct C {
                    static BOOL CALLBACK Cb(HWND h, LPARAM l) {
                        if (!FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr)) {
                            return TRUE;
                        }
                        *reinterpret_cast<HWND*>(l) = h;
                        return FALSE;
                    }
                };
                HWND holder = nullptr;
                EnumWindows(C::Cb, reinterpret_cast<LPARAM>(&holder));
                if (holder) {
                    worker = holder;
                }
            }
        }
    }
    if (!worker) {
        LogStage("attach: ERROR no background WorkerW");
        return nullptr;
    }
    return worker;
}

LRESULT CALLBACK ParentProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

} // namespace

bool WallpaperParentReady() {
    return g_attached;
}

HWND AttachWallpaperParent(HINSTANCE hInst, int x, int y, int w, int h) {
    g_attached = false;
    g_anchor = FindWallpaperAnchor();
    if (!g_anchor) {
        return nullptr;
    }
    {
        char b[160];
        sprintf_s(b, "attach: anchor WorkerW %p", (void*)g_anchor);
        LogStage(b);
    }
    const wchar_t* kClass = L"DNXWallpaperV3Parent";
    WNDCLASSW wc{};
    wc.lpfnWndProc = ParentProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    // EXSTYLE del original WPEDesktopDX11Window: 0x8280000 =
    // WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED.
    // Se intentan variantes por si el sistema rechaza alguna combinación.
    const DWORD kStyles[] = {
        0x08280000u, // NOACTIVATE|NOREDIRECTIONBITMAP|LAYERED (original)
        0x08200000u, // NOACTIVATE|NOREDIRECTIONBITMAP
        0x08000000u, // NOACTIVATE
    };
    HWND parent = nullptr;
    DWORD usedEx = 0;
    for (DWORD ex : kStyles) {
        SetLastError(0);
        parent = CreateWindowExW(ex, kClass, L"", WS_CHILD | WS_VISIBLE,
                                 x, y, w, h, g_anchor, nullptr, hInst, nullptr);
        if (parent) {
            usedEx = ex;
            break;
        }
        char b[128];
        sprintf_s(b, "attach: CreateWindowExW ex=%08lX failed (0x%lX)",
                  (unsigned long)ex, (unsigned long)GetLastError());
        LogStage(b);
    }
    if (!parent) {
        LogStage("attach: ERROR no window style accepted");
        return nullptr;
    }
    {
        char b[128];
        sprintf_s(b, "attach: child created ex=%08lX", (unsigned long)usedEx);
        LogStage(b);
    }
    if (usedEx & WS_EX_LAYERED) {
        SetLayeredWindowAttributes(parent, 0, 255, LWA_ALPHA);
    }
    SetWindowPos(parent, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOSENDCHANGING);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    g_attached = true;
    LogStage("attach: child of WorkerW OK (behind icons)");
    return parent;
}

void ShowWallpaperParent(HWND parent, BYTE alpha) {
    (void)alpha; // sin LAYERED: opacidad gestionada por el contenido
    if (!parent || !g_attached) {
        return;
    }
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    SetWindowPos(parent, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS);
}

HWND CreateWallpaperWindow(HINSTANCE hInst, HWND parent, int w, int h) {
    const wchar_t* kClass = L"DNXWallpaperV3Wallpaper";
    WNDCLASSW wc{};
    wc.lpfnWndProc = ParentProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    return CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE,
                           0, 0, w, h, parent, nullptr, hInst, nullptr);
}
