// main.cpp — Punto de entrada del motor (SIN Steam).
//
// wWinMain: parsea flags (lista observada en .rdata de DNXWallpaper.Motor.exe),
// garantiza instancia única (dnxWallpaperV3GlobalMutex),
// WallpaperManager::Init, loop de mensajes.
//
// Flags soportados (observados):
//   -file -location -monitor -width -height -preset -playlist -profile
//   -properties -safe -disablefx -disablegifs -borderless -activate
//   -host -control -window -tex -nowallpapers -weuimain -updateuicmd
//   -process -silent -buildv -skin -language -steamlanguage -monitordetection
//   -monitormap -x64 -restartapplocal
//
// ELIMINADO (Steam): "-op steamscreenshot", lectura de
// Software\Valve\Steam\Users y [steamusers], WriteSteamMiniDump.
// "-steamlanguage X" se acepta como alias de "-language X" sin tocar Steam.
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <shlobj.h>

#include "wallpaper_manager.h"
#include "tray.h"
#include "wizard.h"
#include "ipc/reload_watcher.h"
#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

// --- Optimizaciones (equivalentes a las del original: playbackfocus,
// playbackfullscreen, playbackmaximized, playbackonbattery, playbacksleep) ---
struct OptSettings {
    bool pauseWhenHidden = true;
    bool pauseOnFullscreen = true;
    bool pauseOnMaximized = false;
    bool pauseOnBattery = false;
    bool pauseOnSessionLock = true;
    int fps = 30;
};
OptSettings g_opt;
bool g_sessionLocked = false;
bool g_suspended = false;
bool g_paused = false;
bool g_muted = false;

void LoadOptSettings() {
    FILE* f = nullptr;
    _wfopen_s(&f, L"res\\config\\optimizations.json", L"rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string j((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) fread(j.data(), 1, (size_t)n, f);
    fclose(f);
    auto getBool = [&](const char* key, bool def) {
        std::string k = std::string("\"") + key + "\"";
        size_t p = j.find(k);
        if (p == std::string::npos) return def;
        p = j.find(':', p);
        if (p == std::string::npos) return def;
        return j.compare(p + 1, 4, "true") == 0;
    };
    g_opt.pauseWhenHidden = getBool("pauseWhenHidden", g_opt.pauseWhenHidden);
    g_opt.pauseOnFullscreen = getBool("pauseOnFullscreen", g_opt.pauseOnFullscreen);
    g_opt.pauseOnMaximized = getBool("pauseOnMaximized", g_opt.pauseOnMaximized);
    g_opt.pauseOnBattery = getBool("pauseOnBattery", g_opt.pauseOnBattery);
    g_opt.pauseOnSessionLock =
        getBool("pauseOnSessionLock", g_opt.pauseOnSessionLock);
    size_t fp = j.find("\"fps\"");
    if (fp != std::string::npos) {
        fp = j.find(':', fp);
        if (fp != std::string::npos) {
            int v = atoi(j.c_str() + fp + 1);
            if (v >= 1 && v <= 144) g_opt.fps = v;
        }
    }
}

// Ventana en primer plano que cubre todo el monitor (juego/vídeo).
bool ForegroundCoversMonitor() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
        !wcscmp(cls, L"SHELLDLL_DefView") || !wcscmp(cls, L"Shell_TrayWnd")) {
        return false;
    }
    RECT r{};
    if (!GetWindowRect(fg, &r)) return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) {
        return false;
    }
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

bool OnBattery() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return false;
    return ps.BatteryFlag != 128 && ps.ACLineStatus == 0;
}

// ¿Debe pausarse el render/decodificación?
bool ShouldPause(HWND parent) {
    if (g_opt.pauseWhenHidden && parent && !IsWindowVisible(parent)) return true;
    if (g_opt.pauseOnSessionLock && (g_sessionLocked || g_suspended)) return true;
    if (g_opt.pauseOnFullscreen && ForegroundCoversMonitor()) return true;
    if (g_opt.pauseOnBattery && OnBattery()) return true;
    return false;
}

void LogOpt(const char* s) {
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) { fprintf(f, "opt: %s\n", s); fclose(f); }
}

bool HasFlag(const std::wstring& cmd, const wchar_t* flag) {
    return cmd.find(flag) != std::wstring::npos;
}

std::wstring FlagValue(const std::wstring& cmd, const wchar_t* flag) {
    size_t pos = cmd.find(flag);
    if (pos == std::wstring::npos) {
        return L"";
    }
    size_t start = cmd.find_first_not_of(L" \t", pos + wcslen(flag));
    if (start == std::wstring::npos) {
        return L"";
    }
    size_t end = cmd.find_first_of(L" \t\"", start);
    return cmd.substr(start, end - start);
}

// Lanza la UI (DNXWallpaper.UI.exe -weuimain) sin esperar: es la ventana
// visible (navegador CEF). No bloquea al motor.

std::string ReadTextFile(const std::wstring& path) {
    std::string out;
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.assign((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) fread(out.data(), 1, (size_t)n, f);
    fclose(f);
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::string JsonString(const std::string& j, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return {};
    p = j.find('"', j.find(':', p));
    if (p == std::string::npos) return {};
    std::string out;
    for (size_t i = p + 1; i < j.size(); ++i) {
        char c = j[i];
        if (c == '"') break;
        if (c == '\\' && i + 1 < j.size()) {
            char e = j[++i];
            if (e == 'n') out.push_back('\n');
            else if (e == 'r') out.push_back('\r');
            else if (e == 't') out.push_back('\t');
            else out.push_back(e);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::wstring UrlEncode(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0,
                                nullptr, nullptr);
    std::string u((size_t)(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u.data(), n, nullptr,
                            nullptr);
    }
    static const char* hex = "0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : u) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~' || c == '/' || c == ':') {
            out.push_back((wchar_t)c);
        } else {
            out.push_back(L'%');
            out.push_back((wchar_t)hex[c >> 4]);
            out.push_back((wchar_t)hex[c & 15]);
        }
    }
    return out;
}

void LaunchUi(const wchar_t* tab);


void OpenNewUi() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t k = dir.find_last_of(L"\\/");
    if (k != std::wstring::npos) dir = dir.substr(0, k);

    int port = 8477;
    std::string mobile = ReadTextFile(dir + L"\\res\\config\\mobile.json");
    std::string ps = JsonString(mobile, "port");
    if (!ps.empty()) {
        int p = atoi(ps.c_str());
        if (p > 1024 && p < 65535) port = p;
    }
    LaunchUi(nullptr);
    Sleep(1200);
    std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/ui2/";
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenDeviceEditor() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t k = dir.find_last_of(L"\\/");
    if (k != std::wstring::npos) dir = dir.substr(0, k);

    // Herramienta independiente (Fase 1): abre el editor en ventana propia.
    std::wstring tool = dir + L"\\device-editor.exe";
    if (GetFileAttributesW(tool.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ShellExecuteW(nullptr, L"open", tool.c_str(), nullptr, dir.c_str(),
                      SW_SHOWNORMAL);
        return;
    }

    std::string current = ReadTextFile(dir + L"\\res\\current.json");
    std::string file = JsonString(current, "project");
    if (file.empty()) file = JsonString(current, "file");

    int port = 8477;
    std::string mobile = ReadTextFile(dir + L"\\res\\config\\mobile.json");
    std::string ps = JsonString(mobile, "port");
    if (!ps.empty()) {
        int p = atoi(ps.c_str());
        if (p > 1024 && p < 65535) port = p;
    }

    // Si la UI no está arrancada, el servidor LAN no existe: se abre.
    LaunchUi(nullptr);
    Sleep(1200);

    std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) +
                       L"/editor/";
    if (!file.empty()) {
        url += L"?file=" + UrlEncode(Utf8ToWide(file));
    }
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

void LaunchUi(const wchar_t* tab = nullptr) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring ui = dir + L"\\DNXWallpaper.UI.exe";
    if (GetFileAttributesW(ui.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return; // UI no desplegada: el motor sigue funcionando sin ella
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring args = L"\"" + ui + L"\" -weuimain";
    if (tab && *tab) {
        args += L" -tab ";
        args += tab;
    }
    if (CreateProcessW(ui.c_str(), args.data(), nullptr, nullptr, FALSE,
                       0, nullptr, dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess); // sin esperar: UI independiente
    }
}


std::wstring ExeDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring d = exe;
    return d.substr(0, d.find_last_of(L"\\/"));
}

std::vector<std::wstring> ReadRecent() {
    std::vector<std::wstring> out;
    FILE* f = nullptr;
    _wfopen_s(&f, (ExeDir() + L"\\res\\config\\recent.json").c_str(), L"rb");
    if (!f) return out;
    std::string j;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    j.assign((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) fread(j.data(), 1, (size_t)n, f);
    fclose(f);
    size_t p = 0;
    while ((p = j.find('"', p)) != std::string::npos) {
        size_t e = j.find('"', p + 1);
        if (e == std::string::npos) break;
        std::string v = j.substr(p + 1, e - p - 1);
        std::wstring w;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '\\' && i + 1 < v.size()) w.push_back((wchar_t)v[++i]);
            else w.push_back((wchar_t)v[i]);
        }
        if (w.find(L':') != std::wstring::npos) out.push_back(w);
        p = e + 1;
    }
    return out;
}

void ApplyPathToMotor(const std::wstring& path) {
    std::wstring esc;
    for (wchar_t c : path) {
        if (c == L'\\' || c == L'"') esc.push_back(L'\\');
        esc.push_back(c);
    }
    FILE* f = nullptr;
    _wfopen_s(&f, (ExeDir() + L"\\res\\current.json").c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"{\"project\": \"%s\"}", esc.c_str());
    fclose(f);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, L"dnxWallpaperV3Reload");
    if (ev) {
        SetEvent(ev);
        CloseHandle(ev);
    }
}

void TakeScreenshot() {
    wchar_t pics[MAX_PATH] = {};
    if (!SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pics))) {
        return;
    }
    std::wstring dir = std::wstring(pics) + L"\\DNXWallpaper v3";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[128];
    swprintf_s(name, L"\\dnxwallpaper_%04d%02d%02d_%02d%02d%02d.png", st.wYear,
               st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    WallpaperManager::Instance().CaptureScreenshot(dir + name);
}

} // namespace


static void InstallScreensaver() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring scr = dir + L"\\DNXWallpaper.scr";
    CopyFileW((dir + L"\\DNXWallpaper.Motor.exe").c_str(), scr.c_str(), FALSE);
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, nullptr,
                        0, KEY_SET_VALUE, nullptr, &k, nullptr) ==
        ERROR_SUCCESS) {
        RegSetValueExW(k, L"SCRNSAVE.EXE", 0, REG_SZ, (const BYTE*)scr.c_str(),
                       (DWORD)((scr.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(k, L"ScreenSaveActive", 0, REG_SZ, (const BYTE*)L"1", 4);
        RegSetValueExW(k, L"ScreenSaveTimeOut", 0, REG_SZ, (const BYTE*)L"300", 8);
        RegCloseKey(k);
    }
    SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, TRUE, nullptr, 0);
    MessageBoxW(nullptr,
                L"Salvapantallas instalado. Windows lo usará con el tiempo de "
                L"espera configurado en Configuración de pantalla.",
                L"DNXWallpaper v3", MB_OK | MB_ICONINFORMATION);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    // Windows lanza el salvapantallas desde System32: sin esto, "res\..." se
    // resuelve en otra carpeta y no encuentra ni el log ni la configuración.
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            dir = dir.substr(0, slash);
            SetCurrentDirectoryW(dir.c_str());
        }
    }
    std::wstring cmd = lpCmdLine ? lpCmdLine : L"";
    {
        std::wstring lowCmd = cmd;
        for (auto& ch : lowCmd) ch = (wchar_t)towlower(ch);
        if (lowCmd.find(L"/s") == 0 || lowCmd.find(L"-screensaver") != std::wstring::npos) {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            int rc = WallpaperManager::Instance().RunScreensaverMode();
            CoUninitialize();
            return rc;
        }
        if (lowCmd.find(L"/c") == 0) {
            InstallScreensaver();
            return 0;
        }
    }


    // Si ya hay un motor en ejecución, no duplicar: asegurar la UI y salir.
    {
        HANDLE probe = CreateMutexW(nullptr, FALSE, L"dnxWallpaperV3Mutex");
        bool already = probe && GetLastError() == ERROR_ALREADY_EXISTS;
        if (probe) CloseHandle(probe);
        if (already) {
            LaunchUi();
            return 0;
        }
    }

    // COM para WIC (decodificación de imágenes) y Shell.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // 0. Asistente de bienvenida (solo la primera vez; guarda res/config/welcome.json).
    bool firstRun = !WelcomeDone(L"res");
    std::wstring wizLang = RunWelcomeWizard(L"res");
    (void)wizLang;

    EngineOptions opts;
    opts.file = FlagValue(cmd, L"-file");
    opts.location = FlagValue(cmd, L"-location");
    opts.preset = FlagValue(cmd, L"-preset");
    opts.playlist = FlagValue(cmd, L"-playlist");
    opts.profile = FlagValue(cmd, L"-profile");
    opts.properties = FlagValue(cmd, L"-properties");
    opts.safe = HasFlag(cmd, L"-safe");
    opts.disableFx = HasFlag(cmd, L"-disablefx");
    opts.disableGifs = HasFlag(cmd, L"-disablegifs");
    opts.noWallpapers = HasFlag(cmd, L"-nowallpapers");
    opts.silent = HasFlag(cmd, L"-silent");
    opts.borderless = HasFlag(cmd, L"-borderless");
    opts.activate = HasFlag(cmd, L"-activate");
    if (HasFlag(cmd, L"-monitor")) {
        opts.monitor = _wtoi(FlagValue(cmd, L"-monitor").c_str());
    }
    if (HasFlag(cmd, L"-language")) {
        opts.language = FlagValue(cmd, L"-language");
    } else if (HasFlag(cmd, L"-steamlanguage")) {
        // Alias local: antes se leía de Steam; ahora es un simple override.
        opts.language = FlagValue(cmd, L"-steamlanguage");
    }
    if (!opts.file.empty()) {
        std::wstring low = opts.file;
        for (auto& c : low) {
            c = (wchar_t)towlower(c);
        }
        if (low.size() >= 5 && low.compare(low.size() - 5, 5, L".json") == 0) {
            opts.type = WallpaperType::Scene;
        } else {
            opts.type = WallpaperType::Video;
        }
    }

    if (!WallpaperManager::Instance().Init(opts)) {
        return 1;
    }

    // Icono de bandeja con menú (Elegir fondo / Carpeta / Config / Salir).
    // Ventana owner oculta para recibir el callback del tray.
    static TrayIcon* g_tray = nullptr;
    struct Owner {
        static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
            if (g_tray && g_tray->HandleMessage(h, m, w, l)) {
                return 0;
            }
            switch (m) {
            case WM_WTSSESSION_CHANGE:
                if (w == WTS_SESSION_LOCK) {
                    g_sessionLocked = true;
                } else if (w == WTS_SESSION_UNLOCK) {
                    g_sessionLocked = false;
                }
                return 0;
            case WM_POWERBROADCAST:
                if (w == PBT_APMSUSPEND) {
                    g_suspended = true;
                } else if (w == PBT_APMRESUMEAUTOMATIC ||
                           w == PBT_APMRESUMESUSPEND) {
                    g_suspended = false;
                }
                return 0;
            case WM_TIMER: {
                // Optimizaciones (equivalentes al original): no renderizar ni
                // decodificar si el wallpaper está oculto, bloqueado, suspendido,
                // tapado por una ventana a pantalla completa o en batería.
                HWND pw = WallpaperManager::Instance().ParentHwnd();
                bool pause = ShouldPause(pw);
                if (pause != g_paused) {
                    g_paused = pause;
                    WallpaperManager::Instance().SetVideoPaused(pause);
                    UINT interval = pause
                                        ? 1000
                                        : (UINT)(1000 / (g_opt.fps > 0
                                                             ? g_opt.fps
                                                             : 30));
                    SetTimer(h, 1, interval, nullptr);
                    LogOpt(pause ? "paused" : "resumed");
                }
                // Recarga de propiedades de reproducción si cambia el archivo.
                static FILETIME lastPlayback{};
                static DWORD lastCheck = 0;
                DWORD nowMs = GetTickCount();
                if (nowMs - lastCheck > 1500) {
                    lastCheck = nowMs;
                    WIN32_FILE_ATTRIBUTE_DATA fad{};
                    if (GetFileAttributesExW(
                            L"res\\config\\optimizations.json",
                            GetFileExInfoStandard, &fad)) {
                        static FILETIME lastOpt{};
                        if (CompareFileTime(&fad.ftLastWriteTime, &lastOpt) != 0) {
                            lastOpt = fad.ftLastWriteTime;
                            WallpaperManager::Instance().ReloadDisplaySettings();
                        }
                    }
                    if (GetFileAttributesExW(L"res\\config\\playback.json",
                                             GetFileExInfoStandard, &fad)) {
                        if (CompareFileTime(&fad.ftLastWriteTime,
                                            &lastPlayback) != 0) {
                            lastPlayback = fad.ftLastWriteTime;
                            WallpaperManager::Instance().LoadPlaybackSettings();
                        }
                    }
                }
                if (!g_paused) {
                    WallpaperManager::Instance().RenderFrame();
                }
                return 0;
            }
            default:
                break;
            }
            return DefWindowProcW(h, m, w, l);
        }
    };
    WNDCLASSW wc{};
    wc.lpfnWndProc = Owner::Proc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WETrayOwner";
    RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"", 0,
                                 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
    LoadOptSettings();
    WTSRegisterSessionNotification(owner, NOTIFY_FOR_THIS_SESSION);
    TrayIcon tray;
    g_tray = &tray;
    TrayActions acts;
    acts.onOpenUi = [] { LaunchUi(); };
    acts.onChooseWallpaper = [] { LaunchUi(); };
    acts.onOpenFolder = [] {
        ShellExecuteW(nullptr, L"open", L"res\\scenes", nullptr, nullptr, SW_SHOW);
    };
    acts.onWorkshop = [] { LaunchUi(L"plugins"); };
    acts.onEditor = [] { OpenDeviceEditor(); };
    acts.onNewUi = [] { OpenNewUi(); };
    acts.onSettings = [] { LaunchUi(L"settings"); };
    acts.onFaq = [] {
        ShellExecuteW(nullptr, L"open", L"README.md", nullptr, nullptr, SW_SHOW);
    };
    acts.getRecent = [] { return ReadRecent(); };
    acts.onRecent = [](const std::wstring& path) { ApplyPathToMotor(path); };
    acts.onScreensaver = [] { InstallScreensaver(); };
    acts.onScreenshot = [] { TakeScreenshot(); };
    acts.onToggleMute = [] {
        g_muted = !g_muted;
        WallpaperManager::Instance().SetVideoMuted(g_muted);
    };
    acts.isMuted = [] { return g_muted; };
    acts.onTogglePause = [] {
        g_paused = !g_paused;
        WallpaperManager::Instance().SetVideoPaused(g_paused);
    };
    acts.isPaused = [] { return g_paused; };
    acts.onQuit = [] { PostQuitMessage(0); };
    tray.Create(owner, L"res\\app.ico", L"DNXWallpaper v3", acts);

    // La UI siempre se abre al arrancar (ventana visible principal).
    // (Antes: solo en primera ejecución; confuso porque el motor es invisible.)
    if (!opts.silent && !opts.noWallpapers) {
        LaunchUi();
    }

    // Con escena (-file): mostrar ventana y arrancar el game loop.
    // Sin escena: la ventana queda invisible (sin velo) y solo hay tray.
    // Además se aplica res/current.json (persistencia de la UI).
    bool hasScene = !opts.file.empty() && !opts.noWallpapers;
    if (!hasScene) {
        hasScene = WallpaperManager::Instance().ApplySelection();
    }
    if (hasScene) {
        WallpaperManager::Instance().ShowContent();
        UINT interval = (UINT)(1000 / (g_opt.fps > 0 ? g_opt.fps : 30));
        SetTimer(owner, 1, interval, nullptr);
    }

    // Watcher: la UI escribe res/current.json y señaliza; el motor aplica.
    // (SetTimer es idempotente con el mismo ID: seguro re-llamarlo.)
    static HWND g_ownerTimer = owner;
    ReloadWatcher watcher([] {
        if (WallpaperManager::Instance().ApplySelection()) {
            WallpaperManager::Instance().ShowContent();
            SetTimer(g_ownerTimer, 1, 33, nullptr);
        }
    });
    watcher.Start();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_tray = nullptr;
    tray.Remove();
    WallpaperManager::Instance().Shutdown();
    CoUninitialize();
    return 0;
}
