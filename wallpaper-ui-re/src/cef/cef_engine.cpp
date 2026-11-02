// cef_engine.cpp — Arranque CEF real.
#include <windows.h>
#include "cef_engine.h"

#include <filesystem>
#include <cstdio>
#include "../core/method_dispatch.h"

static void CefLog(const char* m) {
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) { fprintf(f, "cef: %s\n", m); fclose(f); }
}

std::wstring BuildStartUrl(const std::wstring& docRoot, const UiOptions& opts) {
    // skins.json externo: skin por defecto si no se pasa -skin.
    std::wstring skin = opts.skin.empty() ? L"styles/main.css" : opts.skin;
    // Sin /index.html: el router AngularJS (hashbang) no mapea ese path
    // y cae en la página 404 de la propia UI ("Nothing to see here").
    // La ruta "/" es INTENCIONALMENTE vacía (EmptyCtrl): el host navega
    // directo al hash de la vista (browse/editor/settings).
    std::wstring url = L"http://wpx.internal/?skinStyle=" + skin;
    url += L"&lang=" + opts.language;
    url += L"&uiprofile=local";
    url += L"#/browsewallpapers"; // vista principal (la "/" es vacía a propósito)
    (void)docRoot;
    return url;
}

#ifdef RE_HAVE_CEF
#include "cef_app_impl.h"
#include "include/cef_command_line.h"

bool CefEngine::Init(const UiOptions& opts, const std::wstring& appDir) {
    opts_ = opts;
    appDir_ = appDir;

    // Instancia única SOLO en el proceso "browser". Los subprocesos de CEF
    // (renderer/gpu/utility) llevan --type= en la línea de comandos y deben
    // continuar para llamar a CefExecuteProcess.
    bool isCefSubprocess = wcsstr(GetCommandLineW(), L"--type=") != nullptr;
    HANDLE uiMutex = nullptr;
    if (!isCefSubprocess) {
        uiMutex = CreateMutexW(nullptr, TRUE, L"dnxWallpaperV3UIMutex");
    }
    bool staleInstance = false;
    if (!isCefSubprocess && uiMutex &&
        GetLastError() == ERROR_ALREADY_EXISTS) {
        // Puede haber una UI viva (se enfoca) o un proceso "fantasma" que ya
        // cerró su ventana (p. ej. versión anterior). Reintentar unos segundos.
        HWND existing = nullptr;
        for (int i = 0; i < 10 && !existing; ++i) {
            existing = FindWindowW(L"CefBrowserWindow", L"DNXWallpaper v3");
            if (!existing) existing = FindWindowW(nullptr, L"DNXWallpaper v3");
            if (!existing) Sleep(200);
        }
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            CefLog("already running: focused existing UI");
            CloseHandle(uiMutex);
            exit(0);
        }
        // Mutex sin ventana: continuar con una caché única para no chocar.
        staleInstance = true;
        CefLog("stale UI mutex without window: starting new instance");
    }

    CefMainArgs args(::GetModuleHandleW(nullptr));
    std::string lang;
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, opts.language.c_str(), -1,
                                    nullptr, 0, nullptr, nullptr);
        lang.assign((size_t)(n > 0 ? n - 1 : 0), '\0');
        if (n > 0) {
            WideCharToMultiByte(CP_UTF8, 0, opts.language.c_str(), -1,
                                lang.data(), n, nullptr, nullptr);
        }
        if (lang.empty()) {
            lang = "en";
        }
    }
    CefRefPtr<WallpaperCefApp> app(
        new WallpaperCefApp(appDir + L"\\ui\\dist",
                            BuildStartUrl(appDir, opts), lang));
    int exit_code = CefExecuteProcess(args, app, nullptr);
    if (exit_code >= 0) {
        exit(exit_code); // subproceso CEF
    }

    CefSettings settings;
    settings.no_sandbox = true;
    settings.log_severity = LOGSEVERITY_VERBOSE;
    CefString(&settings.log_file) = (appDir + L"\\cef-debug.log").c_str();
    CefString(&settings.resources_dir_path) =
        (appDir + L"\\cef").c_str();
    CefString(&settings.locales_dir_path) =
        (appDir + L"\\cef\\locales").c_str();
    std::wstring cacheDir = appDir + L"\\cef-cache";
    if (staleInstance) {
        cacheDir += L"-" + std::to_wstring(GetCurrentProcessId());
    }
    std::wstring cachePath = cacheDir;
    CefString(&settings.root_cache_path) = cachePath.c_str();
    settings.log_severity = LOGSEVERITY_WARNING;
    if (opts_.remoteDebugging) {
        settings.remote_debugging_port = opts_.remoteDebuggingPort > 0
                                             ? opts_.remoteDebuggingPort
                                             : 9222;
    }
    if (!CefInitialize(args, settings, app, nullptr)) {
        CefLog("CefInitialize FAILED");
        return false;
    }
    CefLog("init OK (RE_HAVE_CEF)");
    initialized_ = true;
    return true;
}

int CefEngine::RunMessageLoop() {
    if (!initialized_) {
        return 1;
    }
    CefRunMessageLoop();
    return 0;
}

void CefEngine::Shutdown() {
    if (initialized_) {
        CefShutdown();
        initialized_ = false;
    }
}

void CefEngine::RegisterBindings() {}
#else
bool CefEngine::Init(const UiOptions&, const std::wstring&) {
    CefLog("STUB BUILD (sin RE_HAVE_CEF) - reconstruir con build_wui.cmd");
    return false; // sin CEF_ROOT: main muestra el error
}
int CefEngine::RunMessageLoop() {
    return 1;
}
void CefEngine::Shutdown() {}
void CefEngine::RegisterBindings() {}
#endif
