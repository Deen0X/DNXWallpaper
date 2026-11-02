// main.cpp — Host CEF real (SIN Steam).
//
// Arranque: cef_execute_process (subprocesos) -> WallpaperCefApp ->
// CefInitialize -> esquemas wpx.internal/wpx.app -> navegador CEF con la UI.
// Flags: -weuimain -language -skin (-steamlanguage = alias local).
// Sin RE_HAVE_CEF el exe informa que falta CEF_ROOT (ver README).
#include <windows.h>
#include <string>
#include <filesystem>

#include "cef/cef_engine.h"

namespace {
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
std::wstring ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    wchar_t* s = wcsrchr(p, L'\\');
    if (s) {
        *s = 0;
    }
    return p;
}
} // namespace

namespace {
// Diagnóstico de estabilidad: registra el punto de fallo y la pila del
// proceso que falla (sirve igual para el browser y para el renderer).
LONG CALLBACK WpxCrashHandler(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Sólo fallos reales: las notificaciones de depuración y las excepciones
    // gestionadas de C++/CLR son ruido y llenaban el log.
    if (code != 0xC0000005 && code != 0xC0000374 && code != 0xC0000409 &&
        code != 0xC000001D && code != 0xC0000094 && code != 0xC00000FD &&
        code != 0xC0000417 && (code & 0x80000000) == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    HMODULE base = GetModuleHandleW(nullptr);
    // Identifica el rol del proceso (browser / renderer / gpu / utility).
    const wchar_t* cmd = GetCommandLineW();
    const wchar_t* type = wcsstr(cmd, L"--type=");
    char role[24] = "browser";
    if (type) {
        int i = 0;
        for (const wchar_t* c = type + 7; *c && *c != L' ' && i < 23; ++c) {
            role[i++] = (char)*c;
        }
        role[i] = 0;
    }
    void* frames[48] = {};
    USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
    FILE* f = nullptr;
    fopen_s(&f, "res\\crash.log", "a");
    if (f) {
        fprintf(f, "CRASH pid=%lu role=%s code=0x%08lX addr=%p base=%p\n",
                (unsigned long)GetCurrentProcessId(), role,
                (unsigned long)code, ep->ExceptionRecord->ExceptionAddress,
                (void*)base);
        for (unsigned short i = 0; i < n; ++i) {
            char* a = (char*)frames[i];
            fprintf(f, "  #%02d +0x%llx\n", i,
                    (unsigned long long)(a - (char*)base));
        }
        fflush(f);
        fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    std::wstring cmd = lpCmdLine ? lpCmdLine : L"";
    // Diagnóstico opcional: WALLPX_CRASHLOG=1 escribe res\crash.log con
    // la pila si el proceso falla. Desactivado por defecto.
    if (const char* cl = getenv("WALLPX_CRASHLOG")) {
        if (cl && cl[0] == '1') AddVectoredExceptionHandler(1, WpxCrashHandler);
    }
    if (FAILED(OleInitialize(nullptr))) {
        return 1;
    }
    UiOptions opts;
    opts.weUiMain = HasFlag(cmd, L"-weuimain");
    if (HasFlag(cmd, L"-language")) {
        opts.language = FlagValue(cmd, L"-language");
    } else if (HasFlag(cmd, L"-steamlanguage")) {
        opts.language = FlagValue(cmd, L"-steamlanguage"); // alias local
    }
    if (HasFlag(cmd, L"-skin")) {
        opts.skin = FlagValue(cmd, L"-skin");
    }

    CefEngine engine;
    if (!engine.Init(opts, ExeDir())) {
        MessageBoxW(nullptr,
            IsCefBuild()
                ? L"No se pudo inicializar CEF (revisa res\\dnxwallpaper.log). "
                  L"Puede haber una instancia previa abierta o falta un recurso."
                : L"Este binario se compiló sin CEF. Recompila con "
                  L"build_wui.cmd (RE_HAVE_CEF).",
            L"DNXWallpaper UI", MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }
    int rc = engine.RunMessageLoop();
    engine.Shutdown();
    OleUninitialize();
    return rc;
}
