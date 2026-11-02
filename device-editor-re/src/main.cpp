// device-editor.exe — abre el editor «Adaptar a dispositivo» en una ventana de
// aplicación (Edge/Chrome con --app), arrancando DNXWallpaper si hace falta.
//
// Uso:
//   device-editor.exe                 → fondo actual (res/current.json)
//   device-editor.exe --file <ruta>   → fuerza un fichero concreto
//   device-editor.exe --no-wait       → no espera al servidor LAN
//
// No abre consola ni requiere CEF: reutiliza la página servida por
// DNXWallpaper.UI (http://127.0.0.1:<puerto>/editor/).
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winhttp.h>

#include <string>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr int kDefaultPort = 8477;

std::wstring ExeDir() {
    wchar_t exe[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    if (n == 0) return L".";
    std::wstring p(exe, n);
    size_t k = p.find_last_of(L"\\/");
    if (k == std::wstring::npos) return L".";
    return p.substr(0, k);
}

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
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr,
                                nullptr);
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

int ReadPort(const std::wstring& dir) {
    std::string cfg = ReadTextFile(dir + L"\\res\\config\\mobile.json");
    std::string ps = JsonString(cfg, "port");
    if (!ps.empty()) {
        int p = atoi(ps.c_str());
        if (p > 1024 && p < 65535) return p;
    }
    return kDefaultPort;
}

std::wstring CurrentFile(const std::wstring& dir) {
    std::string cur = ReadTextFile(dir + L"\\res\\current.json");
    std::string f = JsonString(cur, "project");
    if (f.empty()) f = JsonString(cur, "file");
    return Utf8ToWide(f);
}

bool ServerAlive(int port, int timeoutMs = 1500) {
    HINTERNET h = WinHttpOpen(L"DNXWallpaper/1.0",
                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h) return false;
    WinHttpSetTimeouts(h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    bool ok = false;
    HINTERNET c = WinHttpConnect(h, L"127.0.0.1", (INTERNET_PORT)port, 0);
    if (c) {
        HINTERNET r = WinHttpOpenRequest(c, L"GET", L"/api/info", nullptr,
                                         WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (r) {
            if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(r, nullptr)) {
                DWORD code = 0;
                DWORD len = sizeof(code);
                WinHttpQueryHeaders(r,
                                    WINHTTP_QUERY_STATUS_CODE |
                                        WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                                    WINHTTP_NO_HEADER_INDEX);
                ok = (code == 200);
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(h);
    return ok;
}

bool StartApp(const std::wstring& dir) {
    std::wstring exe = dir + L"\\DNXWallpaper.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0,
                             nullptr, dir.c_str(), &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok != FALSE;
}

std::wstring RegAppPath(const wchar_t* exe) {
    wchar_t out[MAX_PATH * 2] = {};
    DWORD len = sizeof(out);
    std::wstring key = std::wstring(
                           L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App "
                           L"Paths\\") +
                       exe;
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        if (RegGetValueW(root, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, out,
                         &len) == ERROR_SUCCESS) {
            if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return out;
        }
    }
    return {};
}

std::wstring FindBrowser() {
    // 1) Rutas registradas.
    for (const wchar_t* name : {L"msedge.exe", L"chrome.exe", L"brave.exe"}) {
        std::wstring p = RegAppPath(name);
        if (!p.empty()) return p;
    }
    // 2) Ubicaciones habituales.
    const wchar_t* envs[] = {L"ProgramFiles(x86)", L"ProgramFiles",
                             L"LocalAppData"};
    const wchar_t* rels[] = {
        L"\\Microsoft\\Edge\\Application\\msedge.exe",
        L"\\Google\\Chrome\\Application\\chrome.exe",
        L"\\BraveSoftware\\Brave-Browser\\Application\\brave.exe",
    };
    wchar_t buf[MAX_PATH * 2] = {};
    for (const wchar_t* env : envs) {
        DWORD n = GetEnvironmentVariableW(env, buf, MAX_PATH * 2);
        if (n == 0 || n >= MAX_PATH * 2) continue;
        std::wstring base(buf, n);
        for (const wchar_t* rel : rels) {
            std::wstring p = base + rel;
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
                return p;
            }
        }
    }
    return {};
}

void Launch(const std::wstring& exe, const std::wstring& params,
            const std::wstring& dir) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + params;
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0,
                             nullptr, dir.c_str(), &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        ShellExecuteW(nullptr, L"open", exe.c_str(), params.c_str(), dir.c_str(),
                      SW_SHOWNORMAL);
    }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring forced;
    bool wait = true;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--no-wait") {
            wait = false;
        } else if (a == L"--file" && i + 1 < argc) {
            forced = argv[++i];
        } else if (a.rfind(L"--file=", 0) == 0) {
            forced = a.substr(7);
        } else if (forced.empty() && a.rfind(L"--", 0) != 0) {
            forced = a;
        }
    }
    if (argv) LocalFree(argv);

    const std::wstring dir = ExeDir();
    const int port = ReadPort(dir);

    if (!ServerAlive(port) && wait) {
        StartApp(dir);
        // El servidor LAN tarda en levantar: se reintenta hasta ~25 s.
        for (int i = 0; i < 50; ++i) {
            Sleep(500);
            if (ServerAlive(port, 800)) break;
        }
    }

    std::wstring file = forced.empty() ? CurrentFile(dir) : forced;
    std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) +
                       L"/editor/";
    if (!file.empty()) url += L"?file=" + UrlEncode(file);

    const std::wstring browser = FindBrowser();
    if (!browser.empty()) {
        // Ventana de aplicación sin barra de direcciones.
        std::wstring params = L"--app=\"" + url +
                              L"\" --window-size=1280,880 "
                              L"--window-position=120,60";
        Launch(browser, params, dir);
        return 0;
    }

    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}
