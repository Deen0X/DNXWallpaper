// plugin_host.cpp — Implementación del host de plugins (contrato v1).
#include "plugin_host.h"

#include <windows.h>
#include <winhttp.h>
#include <cstdarg>

void DlLog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) {
        fprintf(f, "[dl] %s\n", buf);
        fclose(f);
    }
}
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <ctime>
#include <atomic>
#include <deque>
#include <memory>
#include <condition_variable>
#include <chrono>

static bool IsHttpUrl(const std::string& u) {
    return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0;
}

// Un fichero descargado no siempre es un medio: los CDN protegidos devuelven
// HTML (403/anti-bot) que se guardaba como .mp4 y luego rompía ffmpeg.
// Definido más abajo, junto a FileSize.
bool MediaFileLooksValid(const std::wstring& path);

static std::atomic<int> g_remoteVersion{1};
static void BumpRemoteVersion() { ++g_remoteVersion; }

namespace {

std::mutex g_registryMutex;
std::map<std::string, RemoteItem> g_registry;
// Marca que el catálogo cambió y hay que reescribir el snapshot.
std::atomic<bool> g_snapDirty{false};

// ---------- utilidades de archivo ----------
std::string ReadAll(const std::wstring& path) {
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
    // Quitar BOM UTF-8 si existe.
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr,
                                nullptr);
    std::string out((size_t)(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n,
                                   nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

// ---------- mini parser JSON ----------
struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;

    const JVal* Get(const char* k) const {
        for (auto& kv : obj)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    std::string AsString(const char* k, const std::string& def = "") const {
        const JVal* v = Get(k);
        return (v && v->type == Str) ? v->str : def;
    }
    bool AsBool(const char* k, bool def = false) const {
        const JVal* v = Get(k);
        return (v && v->type == Bool) ? v->b : def;
    }
    double AsNum(const char* k, double def = 0) const {
        const JVal* v = Get(k);
        return (v && v->type == Num) ? v->num : def;
    }
};

struct JParser {
    const char* p;
    const char* end;
    bool ok = true;

    void SkipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            ++p;
    }
    bool Eat(char c) {
        SkipWs();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }
    std::string ParseString() {
        std::string out;
        if (!Eat('"')) { ok = false; return out; }
        while (p < end) {
            char c = *p++;
            if (c == '"') return out;
            if (c == '\\' && p < end) {
                char e = *p++;
                switch (e) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': {
                    // \uXXXX -> UTF-8 (BMP)
                    if (end - p < 4) { ok = false; return out; }
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = *p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                    }
                    if (cp < 0x80) out.push_back((char)cp);
                    else if (cp < 0x800) {
                        out.push_back((char)(0xC0 | (cp >> 6)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back((char)(0xE0 | (cp >> 12)));
                        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: out.push_back(e);
                }
            } else {
                out.push_back(c);
            }
        }
        ok = false;
        return out;
    }
    JVal ParseValue() {
        SkipWs();
        JVal v;
        if (p >= end) { ok = false; return v; }
        char c = *p;
        if (c == '{') {
            ++p;
            v.type = JVal::Obj;
            SkipWs();
            if (Eat('}')) return v;
            while (ok && p < end) {
                std::string k = ParseString();
                if (!ok) break;
                if (!Eat(':')) { ok = false; break; }
                JVal val = ParseValue();
                v.obj.emplace_back(k, std::move(val));
                if (Eat(',')) continue;
                Eat('}');
                break;
            }
        } else if (c == '[') {
            ++p;
            v.type = JVal::Arr;
            SkipWs();
            if (Eat(']')) return v;
            while (ok && p < end) {
                v.arr.push_back(ParseValue());
                if (Eat(',')) continue;
                Eat(']');
                break;
            }
        } else if (c == '"') {
            v.type = JVal::Str;
            v.str = ParseString();
        } else if (c == 't' && end - p >= 4 && !strncmp(p, "true", 4)) {
            v.type = JVal::Bool; v.b = true; p += 4;
        } else if (c == 'f' && end - p >= 5 && !strncmp(p, "false", 5)) {
            v.type = JVal::Bool; v.b = false; p += 5;
        } else if (c == 'n' && end - p >= 4 && !strncmp(p, "null", 4)) {
            v.type = JVal::Null; p += 4;
        } else {
            char* e = nullptr;
            v.num = strtod(p, &e);
            if (e == p) { ok = false; return v; }
            v.type = JVal::Num;
            p = e;
        }
        return v;
    }
};

JVal ParseJson(const std::string& text) {
    JParser jp{text.c_str(), text.c_str() + text.size()};
    return jp.ParseValue();
}

} // namespace


// Une "tags" + "categories" en una lista separada por comas.
std::string CollectTags(const JVal& it) {
    std::string out;
    auto add = [&](const char* key) {
        const JVal* arr = it.Get(key);
        if (!arr || arr->type != JVal::Arr) return;
        for (const auto& v : arr->arr) {
            if (v.type != JVal::Str || v.str.empty()) continue;
            bool dup = false;
            size_t p = 0;
            while ((p = out.find(v.str, p)) != std::string::npos) {
                bool leftOk = p == 0 || out[p - 1] == ',';
                bool rightOk = p + v.str.size() == out.size() ||
                               out[p + v.str.size()] == ',';
                if (leftOk && rightOk) { dup = true; break; }
                p += v.str.size();
            }
            if (dup) continue;
            if (!out.empty()) out.push_back(',');
            out += v.str;
        }
    };
    add("categories");
    add("tags");
    return out;
}

std::vector<PluginInfo> DiscoverPlugins(const std::wstring& baseDir) {
    std::vector<PluginInfo> out;
    // Plugins deshabilitados: res\config\plugins.json {"disabled":["id",...]}
    std::vector<std::string> disabled;
    {
        std::string cfg = ReadAll(baseDir + L"\\res\\config\\plugins.json");
        size_t p = cfg.find("\"disabled\"");
        if (p != std::string::npos) {
            size_t a = cfg.find('[', p);
            size_t b = cfg.find(']', a);
            if (a != std::string::npos && b != std::string::npos) {
                std::string arr = cfg.substr(a + 1, b - a - 1);
                size_t i = 0;
                while ((i = arr.find('"', i)) != std::string::npos) {
                    size_t j = arr.find('"', i + 1);
                    if (j == std::string::npos) break;
                    disabled.push_back(arr.substr(i + 1, j - i - 1));
                    i = j + 1;
                }
            }
        }
    }
    auto isDisabled = [&](const std::string& id) {
        for (const auto& d : disabled)
            if (d == id) return true;
        return false;
    };
    std::wstring root = baseDir + L"\\plugins\\";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((root + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring dir = root + fd.cFileName;
        std::string manifest = ReadAll(dir + L"\\plugin.json");
        if (manifest.empty()) continue;
        JVal j = ParseJson(manifest);
        PluginInfo pi;
        pi.id = j.AsString("id");
        pi.name = j.AsString("name");
        pi.attribution = j.AsString("attribution");
        std::string entry = j.AsString("entry", "plugin.exe");
        std::string runtime = j.AsString("runtime", "");
        if (pi.id.empty()) pi.id = ToUtf8(fd.cFileName);
        pi.python = (runtime == "python");
        // Contrato v3: `protocol: 3` habilita el worker persistente (`serve`).
        pi.protocol = (int)j.AsNum("protocol", 2);
        pi.exePath = dir + L"\\" + ToWide(entry);
        if (GetFileAttributesW(pi.exePath.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        if (isDisabled(pi.id)) continue;

        // Si la version del plugin cambia, su cache de paginas ya no vale:
        // los ids o el formato pueden haber cambiado (paso de v1 a v2).
        {
            const std::string version = j.AsString("version", "");
            const std::wstring cacheDir =
                baseDir + L"\\res\\cache\\plugins\\" + ToWide(pi.id);
            const std::wstring marker = cacheDir + L"\\.version";
            const std::string seen = ReadAll(marker);
            if (!version.empty() && seen != version) {
                WIN32_FIND_DATAW cfd{};
                HANDLE ch = FindFirstFileW((cacheDir + L"\\*").c_str(), &cfd);
                if (ch != INVALID_HANDLE_VALUE) {
                    do {
                        if (wcscmp(cfd.cFileName, L".") == 0 ||
                            wcscmp(cfd.cFileName, L"..") == 0) {
                            continue;
                        }
                        DeleteFileW((cacheDir + L"\\" + cfd.cFileName).c_str());
                    } while (FindNextFileW(ch, &cfd));
                    FindClose(ch);
                }
                CreateDirectoryW((baseDir + L"\\res\\cache").c_str(), nullptr);
                CreateDirectoryW(
                    (baseDir + L"\\res\\cache\\plugins").c_str(), nullptr);
                CreateDirectoryW(cacheDir.c_str(), nullptr);
                FILE* mf = nullptr;
                _wfopen_s(&mf, marker.c_str(), L"wb");
                if (mf) {
                    fwrite(version.data(), 1, version.size(), mf);
                    fclose(mf);
                }
            }
        }

        out.push_back(pi);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// Job con KILL_ON_JOB_CLOSE: todos los plugin.exe lanzados mueren cuando
// termina el proceso que los creo. Evita procesos huerfanos bloqueados
// escribiendo en una tuberia sin lector.
HANDLE PluginJob() {
    static HANDLE job = []() -> HANDLE {
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        if (!h) return nullptr;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info,
                                sizeof(info));
        return h;
    }();
    return job;
}

// Intérprete de Python para los plugins v2. Orden:
//   1. variable DNX_PYTHON
//   2. runtime embebido junto al ejecutable (runtime\python\python.exe)
//   3. lanzador py.exe -3
//   4. python.exe del PATH
// Devuelve el prefijo de comando ya entrecomillado (vacío si no hay).
// OJO: se llama desde el escaneo en paralelo (un hilo por plugin). La caché se
// rellena bajo cerrojo: antes se marcaba `done` antes de tener el valor y otro
// hilo podía leerlo vacío (plugins que fallaban de forma intermitente).
std::wstring PythonPrefix() {
    static std::mutex m;
    static std::wstring cached;
    static bool done = false;
    std::lock_guard<std::mutex> lk(m);
    if (done) return cached;

    auto finish = [&]() {
        done = true;
        return cached;
    };

    const wchar_t* env = _wgetenv(L"DNX_PYTHON");
    if (env && *env && GetFileAttributesW(env) != INVALID_FILE_ATTRIBUTES) {
        cached = std::wstring(L"\"") + env + L"\"";
        return finish();
    }

    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        std::wstring dir = exe;
        size_t k = dir.find_last_of(L"\\/");
        if (k != std::wstring::npos) dir = dir.substr(0, k);
        std::wstring emb = dir + L"\\runtime\\python\\python.exe";
        if (GetFileAttributesW(emb.c_str()) != INVALID_FILE_ATTRIBUTES) {
            cached = L"\"" + emb + L"\"";
            return finish();
        }
    }

    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"py.exe", nullptr, MAX_PATH, found, nullptr)) {
        cached = L"\"" + std::wstring(found) + L"\" -3";
        return finish();
    }
    if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, found, nullptr)) {
        cached = L"\"" + std::wstring(found) + L"\"";
        return finish();
    }
    return finish();
}

// Ejecución de un plugin: línea de comandos ya armada + directorio de trabajo.
struct PluginInvocation {
    std::wstring cmd;
    std::wstring cwd;
    std::wstring cacheDir;
    bool v2 = false;  // trae dnxlib.py (acepta --cache-dir)
};

bool BuildPluginInvocation(const PluginInfo& plugin, const std::wstring& args,
                           PluginInvocation& out) {
    std::wstring exe = plugin.exePath;
    std::wstring low = exe;
    for (auto& c : low) c = (wchar_t)towlower(c);
    bool isScript = low.size() > 4 &&
                    (low.rfind(L".cmd") == low.size() - 4 ||
                     low.rfind(L".bat") == low.size() - 4);
    // Ruta de configuración del plugin (contrato v1 §2.0): se pasa siempre
    // con --config; el plugin puede ignorarla si no la necesita.
    std::wstring pluginDir = plugin.exePath;
    size_t sep = pluginDir.find_last_of(L"\\/");
    if (sep != std::wstring::npos) pluginDir = pluginDir.substr(0, sep);
    std::wstring cfg = pluginDir + L"\\config.json";
    std::wstring tail = args + L" --config \"" + cfg + L"\"";
    out.cwd = pluginDir;
    if (plugin.python) {
        std::wstring py = PythonPrefix();
        if (py.empty()) return false;  // sin intérprete no hay plugin
        // Caché de datos del plugin bajo res\cache\plugin-data\<id>.
        // Sólo los plugins del contrato v2 (traen dnxlib.py) aceptan
        // --cache-dir; los antiguos fallaban con exit=2 al recibirlo.
        std::wstring base = pluginDir;
        size_t s1 = base.find_last_of(L"\\/");          // ...\plugins\<id>
        if (s1 != std::wstring::npos) base = base.substr(0, s1);
        size_t s2 = base.find_last_of(L"\\/");          // ...\plugins
        if (s2 != std::wstring::npos) base = base.substr(0, s2);
        out.cacheDir = base + L"\\res\\cache\\plugin-data\\" + ToWide(plugin.id);
        out.v2 = GetFileAttributesW((pluginDir + L"\\dnxlib.py").c_str()) !=
                 INVALID_FILE_ATTRIBUTES;
        out.cmd = py + L" \"" + exe + L"\" " + tail;
        if (out.v2) out.cmd += L" --cache-dir \"" + out.cacheDir + L"\"";
        return true;
    }
    if (isScript) {
        // cmd /s /c ""script.cmd" args..." : comillas externas para conservar
        // las internas de las rutas con espacios.
        out.cmd = L"cmd.exe /d /s /c \"\"" + exe + L"\" " + tail + L"\"";
    } else {
        out.cmd = L"\"" + exe + L"\" " + tail;
    }
    return true;
}

// Proceso de un solo uso (contrato v1/v2): se lanza, se lee la salida y muere.
std::string RunPluginCommandOneShot(const PluginInfo& plugin,
                                    const std::wstring& args, int timeoutMs,
                                    int* exitCode) {
    if (exitCode) *exitCode = -1;
    PluginInvocation inv;
    if (!BuildPluginInvocation(plugin, args, inv)) {
        if (exitCode) *exitCode = plugin.python ? -2 : -1;
        return {};
    }
    std::wstring full = inv.cmd;
    std::wstring cwd = inv.cwd;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return {};
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    // stderr del plugin: ahí van los mensajes de error reales (dnxlib escribe
    // sus logs en stderr). Se manda a un fichero temporal para no mezclarlo con
    // el JSON de stdout, y sólo se lee si el comando falla.
    wchar_t tmpDir[MAX_PATH] = {0};
    wchar_t errPath[MAX_PATH] = {0};
    HANDLE errh = INVALID_HANDLE_VALUE;
    if (GetTempPathW(MAX_PATH, tmpDir) &&
        GetTempFileNameW(tmpDir, L"dnx", 0, errPath)) {
        errh = CreateFileW(errPath, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    }
    si.hStdError = (errh == INVALID_HANDLE_VALUE) ? nullptr : errh;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(full.begin(), full.end());
    buf.push_back(0);
    BOOL okc = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (errh != INVALID_HANDLE_VALUE) CloseHandle(errh);
    if (!okc) {
        CloseHandle(rd);
        if (errPath[0]) DeleteFileW(errPath);
        return {};
    }
    if (HANDLE job = PluginJob()) {
        AssignProcessToJobObject(job, pi.hProcess);
    }
    std::string out;
    char chunk[4096];
    DWORD read = 0;
    DWORD start = GetTickCount();
    while (true) {
        DWORD avail = 0;
        if (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            if (!ReadFile(rd, chunk, sizeof(chunk) - 1, &read, nullptr) ||
                read == 0)
                break;
            chunk[read] = 0;
            out += chunk;
            if (out.size() > 8 * 1024 * 1024) break; // límite de seguridad
        } else {
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
                // vaciar lo que quede
                while (ReadFile(rd, chunk, sizeof(chunk) - 1, &read, nullptr) &&
                       read > 0) {
                    chunk[read] = 0;
                    out += chunk;
                }
                break;
            }
            if (GetTickCount() - start > (DWORD)timeoutMs) {
                TerminateProcess(pi.hProcess, 1);
                break;
            }
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    if (exitCode) *exitCode = (int)code;
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    // stderr: sólo interesa cuando el comando falla (o no devuelve nada).
    std::string errTail;
    if (errPath[0]) {
        FILE* ef = nullptr;
        if (_wfopen_s(&ef, errPath, L"rb") == 0 && ef) {
            fseek(ef, 0, SEEK_END);
            long n = ftell(ef);
            if (n > 0) {
                const long take = n > 1200 ? 1200 : n;
                fseek(ef, n - take, SEEK_SET);
                errTail.assign((size_t)take, '\0');
                fread(errTail.data(), 1, (size_t)take, ef);
            }
            fclose(ef);
        }
        DeleteFileW(errPath);
    }
    // Diagnóstico: qué devolvió cada plugin (útil cuando el catálogo no cambia).
    DlLog("plugin %s exit=%lu bytes=%zu", plugin.id.c_str(),
          (unsigned long)code, out.size());
    if (!errTail.empty() && (code != 0 || out.empty())) {
        std::string flat;
        for (char c : errTail) flat.push_back((c == '\r' || c == '\n') ? ' ' : c);
        DlLog("plugin %s stderr: %s", plugin.id.c_str(), flat.c_str());
    }
    return out;
}


// --- Worker persistente (contrato v3, `serve`) -----------------------------
// Un proceso Python por plugin que recibe peticiones JSON por stdin y responde
// por stdout. Elimina el arranque de Python en cada comando (~250-360 ms).
// Si el plugin no declara `protocol: 3`, falla o no contesta, se usa el modo de
// un solo uso de siempre: la mejora entra plugin a plugin sin romper nada.
namespace {

struct PluginWorker {
    HANDLE proc = nullptr;
    HANDLE in = nullptr;    // stdin del plugin (escribimos)
    HANDLE out = nullptr;   // stdout del plugin (leemos)
    std::string inbuf;      // bytes pendientes de stdout
    std::mutex m;           // una petición a la vez
    int nextId = 1;
    bool broken = false;
};

std::map<std::string, std::shared_ptr<PluginWorker>> g_workers;
std::mutex g_workersMutex;

// ¿Está activado el worker? res\config\plugins.json -> "worker": false
bool WorkerEnabled() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    cached = 1;
    FILE* f = nullptr;
    if (_wfopen_s(&f, L"res\\config\\plugins.json", L"rb") == 0 && f) {
        char buf[4096] = {};
        size_t got = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (got > 0) {
            const char* k = strstr(buf, "\"worker\"");
            if (k) {
                const char* c = strchr(k, ':');
                if (c) {
                    const char* v = c + 1;
                    while (*v == ' ' || *v == '\t') ++v;
                    if (strncmp(v, "false", 5) == 0) cached = 0;
                }
            }
        }
    }
    return cached != 0;
}

void CloseWorker(PluginWorker& w) {
    if (w.in) { CloseHandle(w.in); w.in = nullptr; }
    if (w.out) { CloseHandle(w.out); w.out = nullptr; }
    if (w.proc) {
        TerminateProcess(w.proc, 0);
        CloseHandle(w.proc);
        w.proc = nullptr;
    }
}

// Lee una línea de stdout con plazo. Devuelve false si expira o muere.
bool WorkerReadLine(PluginWorker& w, std::string& line, DWORD timeoutMs) {
    line.clear();
    const DWORD start = GetTickCount();
    char buf[8192];
    while (true) {
        size_t nl = w.inbuf.find('\n');
        if (nl != std::string::npos) {
            line = w.inbuf.substr(0, nl);
            w.inbuf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        DWORD avail = 0;
        if (PeekNamedPipe(w.out, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD got = 0;
            if (!ReadFile(w.out, buf, sizeof(buf), &got, nullptr) || got == 0) {
                return false;
            }
            w.inbuf.append(buf, (size_t)got);
            continue;
        }
        if (GetTickCount() - start > timeoutMs) return false;
        Sleep(10);
    }
}

std::string JsonEscape(const std::string& in) {
    std::string o;
    for (char c : in) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

// Convierte "--limit 100 --page 1 --animated" en {"limit":"100",...}.
std::string ArgsToJson(const std::string& args) {
    std::vector<std::string> tok;
    std::string cur;
    bool q = false;
    for (char c : args) {
        if (c == '"') { q = !q; continue; }
        if (!q && (c == ' ' || c == '\t')) {
            if (!cur.empty()) { tok.push_back(cur); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) tok.push_back(cur);

    std::string j = "{";
    bool first = true;
    for (size_t i = 0; i < tok.size(); ++i) {
        const std::string& t = tok[i];
        if (t.rfind("--", 0) != 0) continue;  // el comando va aparte
        std::string key = t.substr(2);
        bool hasVal = (i + 1 < tok.size()) && tok[i + 1].rfind("--", 0) != 0;
        if (!first) j += ",";
        first = false;
        j += "\"" + JsonEscape(key) + "\":";
        if (hasVal) {
            j += "\"" + JsonEscape(tok[i + 1]) + "\"";
            ++i;
        } else {
            j += "true";
        }
    }
    j += "}";
    return j;
}

// Arranca `serve` y espera la línea de ready. nullptr si no es viable.
std::shared_ptr<PluginWorker> StartWorker(const PluginInfo& plugin) {
    PluginInvocation inv;
    if (!BuildPluginInvocation(plugin, L"serve", inv)) {
        DlLog("worker %s: no se pudo armar el comando (serve)",
              plugin.id.c_str());
        return nullptr;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE inR = nullptr, inW = nullptr;   // stdin del hijo
    HANDLE outR = nullptr, outW = nullptr; // stdout del hijo
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) {
        DlLog("worker %s: CreatePipe fallo (%lu)", plugin.id.c_str(),
              GetLastError());
        return nullptr;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    // stderr a un fichero aparte: no se mezcla con el protocolo.
    std::wstring errLog = inv.cacheDir + L"\\worker-stderr.log";
    CreateDirectoryW(inv.cacheDir.c_str(), nullptr);
    HANDLE errh = CreateFileW(errLog.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = (errh == INVALID_HANDLE_VALUE) ? nullptr : errh;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(inv.cmd.begin(), inv.cmd.end());
    buf.push_back(0);
    BOOL okc = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, inv.cwd.c_str(), &si,
                              &pi);
    CloseHandle(inR);
    CloseHandle(outW);
    if (errh != INVALID_HANDLE_VALUE) CloseHandle(errh);
    if (!okc) {
        DlLog("worker %s: CreateProcess fallo (%lu)", plugin.id.c_str(),
              GetLastError());
        CloseHandle(inW);
        CloseHandle(outR);
        return nullptr;
    }
    if (HANDLE job = PluginJob()) AssignProcessToJobObject(job, pi.hProcess);
    CloseHandle(pi.hThread);

    auto w = std::make_shared<PluginWorker>();
    w->proc = pi.hProcess;
    w->in = inW;
    w->out = outR;

    std::string ready;
    if (!WorkerReadLine(*w, ready, 5000) ||
        ready.find("\"ready\"") == std::string::npos) {
        DlLog("worker %s no arranco (serve)", plugin.id.c_str());
        CloseWorker(*w);
        return nullptr;
    }
    DlLog("worker %s listo", plugin.id.c_str());
    return w;
}

// Quita el `id` de la respuesta: {"id":N,"ok":...} -> {"ok":...}
std::string StripResponseId(const std::string& line) {
    size_t p = line.find("\"id\"");
    if (p == std::string::npos) return line;
    size_t colon = line.find(':', p);
    if (colon == std::string::npos) return line;
    size_t comma = line.find(',', colon);
    if (comma == std::string::npos) return line;  // sólo id: no hay payload
    size_t brace = line.find('{');
    if (brace == std::string::npos || brace > p) return line;
    return "{" + line.substr(comma + 1);
}

// Petición al worker. false = no se pudo (se cae al modo un-uso).
bool RunViaWorker(const PluginInfo& plugin, const std::wstring& args,
                  int timeoutMs, std::string& out) {
    // Argumentos a UTF-8 (las rutas pueden tener acentos).
    std::string u8;
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, args.c_str(), -1, nullptr, 0,
                                    nullptr, nullptr);
        if (n > 0) {
            u8.resize((size_t)n);
            WideCharToMultiByte(CP_UTF8, 0, args.c_str(), -1, u8.data(), n,
                                nullptr, nullptr);
            while (!u8.empty() && u8.back() == '\0') u8.pop_back();
        }
    }
    // Primera palabra = comando; el resto, argumentos.
    size_t sp = u8.find(' ');
    const std::string cmd = (sp == std::string::npos) ? u8 : u8.substr(0, sp);
    const std::string rest = (sp == std::string::npos) ? std::string() : u8.substr(sp + 1);
    if (cmd.empty()) return false;

    std::shared_ptr<PluginWorker> w;
    bool needStart = false;
    {
        std::lock_guard<std::mutex> g(g_workersMutex);
        auto it = g_workers.find(plugin.id);
        if (it != g_workers.end()) w = it->second;
        if (w && w->broken) return false;
        if (!w) needStart = true;
    }
    // El arranque (hasta 5 s esperando el ready) va fuera del cerrojo global:
    // si un plugin tarda en fallar, no bloquea a los demás.
    if (needStart) {
        auto fresh = StartWorker(plugin);
        std::lock_guard<std::mutex> g(g_workersMutex);
        auto it = g_workers.find(plugin.id);
        if (it != g_workers.end()) {
            // Otro hilo lo creó mientras tanto: se descarta el duplicado.
            if (fresh) CloseWorker(*fresh);
            w = it->second;
        } else if (fresh) {
            g_workers[plugin.id] = fresh;
            w = fresh;
        } else {
            auto dead = std::make_shared<PluginWorker>();
            dead->broken = true;
            g_workers[plugin.id] = dead;
            return false;
        }
        if (w && w->broken) return false;
    }

    std::lock_guard<std::mutex> lock(w->m);
    if (w->broken || !w->in) return false;
    const int id = w->nextId++;
    std::string req = "{\"id\":" + std::to_string(id) + ",\"cmd\":\"" +
                      JsonEscape(cmd) + "\",\"args\":" + ArgsToJson(rest) +
                      "}\n";
    DWORD wrote = 0;
    if (!WriteFile(w->in, req.data(), (DWORD)req.size(), &wrote, nullptr) ||
        wrote != req.size()) {
        w->broken = true;
        CloseWorker(*w);
        return false;
    }

    const DWORD deadline = GetTickCount() + (DWORD)(timeoutMs > 0 ? timeoutMs : 30000);
    std::string line;
    const std::string want = "\"id\":" + std::to_string(id);
    while (GetTickCount() < deadline) {
        if (!WorkerReadLine(*w, line, 800)) {
            // Puede ser un hueco entre líneas: seguir hasta el plazo global.
            continue;
        }
        if (line.find(want) == std::string::npos &&
            line.find("\"id\": " + std::to_string(id)) == std::string::npos) {
            continue;  // respuesta de otra petición o ruido
        }
        out = StripResponseId(line);
        DlLog("worker %s %s ok (%zu bytes)", plugin.id.c_str(), cmd.c_str(),
              out.size());
        return true;
    }
    DlLog("worker %s %s TIMEOUT", plugin.id.c_str(), cmd.c_str());
    w->broken = true;
    CloseWorker(*w);
    return false;
}

}  // namespace

// Punto de entrada: usa el worker si el plugin lo soporta y si funciona;
// en cualquier otro caso, proceso de un solo uso (contrato v1/v2).
std::string RunPluginCommand(const PluginInfo& plugin,
                             const std::wstring& args, int timeoutMs,
                             int* exitCode) {
    if (plugin.protocol >= 3 && !plugin.exePath.empty() && WorkerEnabled()) {
        std::string out;
        if (RunViaWorker(plugin, args, timeoutMs, out)) {
            if (exitCode) *exitCode = 0;
            return out;
        }
        DlLog("worker %s no disponible: se usa un solo uso", plugin.id.c_str());
    }
    return RunPluginCommandOneShot(plugin, args, timeoutMs, exitCode);
}

// Cierra todos los workers (al salir). El job object también los mata.
void StopPluginWorkers() {
    std::lock_guard<std::mutex> g(g_workersMutex);
    for (auto& kv : g_workers) {
        if (kv.second && kv.second->proc && kv.second->in) {
            const char* bye = "{\"id\":0,\"cmd\":\"shutdown\",\"args\":{}}\n";
            DWORD wrote = 0;
            WriteFile(kv.second->in, bye, (DWORD)strlen(bye), &wrote, nullptr);
        }
        CloseWorker(*kv.second);
    }
    g_workers.clear();
}


// --- Caché en disco por plugin y página (arranque instantáneo) ------------
// res/cache/plugins/<id>/pN.json; TTL configurable (cacheTtlHours).
int g_cacheTtlHours = 24;

std::wstring CacheExeDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring d = exe;
    size_t k = d.find_last_of(L"\\/");
    return k == std::wstring::npos ? std::wstring(L".") : d.substr(0, k);
}

std::wstring PluginPageCachePath(const std::string& pluginId, int page) {
    std::wstring root = CacheExeDir() + L"\\res\\cache";
    CreateDirectoryW(root.c_str(), nullptr);
    std::wstring plugins = root + L"\\plugins";
    CreateDirectoryW(plugins.c_str(), nullptr);
    std::wstring dir = plugins + L"\\" + ToWide(pluginId);
    CreateDirectoryW(dir.c_str(), nullptr);
    wchar_t name[32];
    swprintf_s(name, L"\\p%d.json", page);
    return dir + name;
}

long long FileUnixTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa))
        return -1;
    ULARGE_INTEGER u{};
    u.LowPart = fa.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fa.ftLastWriteTime.dwHighDateTime;
    return (long long)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

bool ReadTextUtf8(const std::wstring& path, std::string& out) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.assign((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) fread(out.data(), 1, (size_t)n, f);
    fclose(f);
    return !out.empty();
}

void WriteTextUtf8(const std::wstring& path, const std::string& text) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"wb");
    if (!f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

std::vector<RemoteItem> ListPluginItems(const PluginInfo& plugin, int limit) {
    std::vector<RemoteItem> out;
    wchar_t args[128];
    swprintf_s(args, L"list --limit %d --sort newest", limit);
    std::string json = RunPluginCommand(plugin, args, 20000);
    if (json.empty()) return out;
    JVal root = ParseJson(json);
    const JVal* items = root.Get("items");
    if (!items || items->type != JVal::Arr) return out;
    for (const auto& it : items->arr) {
        if (it.type != JVal::Obj) continue;
        RemoteItem r;
        r.provider = plugin.id;
        r.id = it.AsString("id");
        if (r.id.empty()) continue;
        r.key = plugin.id + ":" + r.id;
        r.title = it.AsString("title", r.id);
        r.mediaType = it.AsString("mediaType", "video");
        r.animated = it.AsBool("animated", r.mediaType == "video");
        r.tags = CollectTags(it);
        {
            // Contrato v2: width/height en el nivel del item. "resolution"
            // se mantiene como respaldo por compatibilidad.
            r.width = (int)it.AsNum("width", 0);
            r.height = (int)it.AsNum("height", 0);
            const JVal* res = it.Get("resolution");
            if (res && res->type == JVal::Obj) {
                if (r.width <= 0) r.width = (int)res->AsNum("width", 0);
                if (r.height <= 0) r.height = (int)res->AsNum("height", 0);
            }
        }
        r.publishedAt = it.AsString("publishedAt");
        r.downloadable = it.AsBool("downloadable", true);
        const JVal* media = it.Get("media");
        if (media && media->type == JVal::Obj) r.mediaUrl = media->AsString("url");
        const JVal* thumbs = it.Get("thumbs");
        if (thumbs && thumbs->type == JVal::Obj) {
            r.animatedThumb = thumbs->AsString("animatedUrl");
            r.staticThumb = thumbs->AsString("staticUrl");
            r.staticCropped = thumbs->AsBool("staticCropped", false);
            r.animatedCropped = thumbs->AsBool("animatedCropped", false);
            if (r.staticThumb.empty()) r.staticThumb = r.animatedThumb;
        }
        const JVal* rating = it.Get("rating");
        if (rating && rating->type == JVal::Obj) {
            r.rating = rating->AsNum("average", -1.0);
        }
        if (r.mediaUrl.empty() && r.staticThumb.empty()) continue;
        out.push_back(std::move(r));
    }
    return out;
}

// http://wpx.internal/... ya apunta a un archivo local servido por el host:
// no hay nada que descargar (antes se intentaba por WinHTTP -> error 12007).
bool IsRemoteHttp(const std::string& u) {
    return IsHttpUrl(u) && u.rfind("http://wpx.internal/", 0) != 0;
}

std::vector<RemoteItem> ListPluginItemsPaged(
    const PluginInfo& plugin, int maxItems,
    const std::function<void(const std::vector<RemoteItem>&)>& onPage) {
    std::vector<RemoteItem> all;
    const int kPage = 100;
    for (int page = 1; page <= 20 && (int)all.size() < maxItems; ++page) {
        wchar_t args[160];
        swprintf_s(args, L"list --limit %d --page %d --sort newest", kPage,
                   page);
        // Página 1 siempre fresca (novedades); el resto desde caché si es
        // reciente, para que el arranque sea inmediato.
        const std::wstring cacheFile = PluginPageCachePath(plugin.id, page);
        std::string json;
        if (page > 1 && g_cacheTtlHours > 0) {
            const long long t = FileUnixTime(cacheFile);
            const long long now = (long long)time(nullptr);
            if (t > 0 && now - t < (long long)g_cacheTtlHours * 3600) {
                ReadTextUtf8(cacheFile, json);
            }
        }
        if (json.empty()) {
            json = RunPluginCommand(plugin, args, 25000);
            if (!json.empty()) WriteTextUtf8(cacheFile, json);
        }
        if (json.empty()) break;
        JVal root = ParseJson(json);
        const JVal* items = root.Get("items");
        if (!items || items->type != JVal::Arr || items->arr.empty()) break;
        for (const auto& it : items->arr) {
            if (it.type != JVal::Obj) continue;
            RemoteItem r;
            r.provider = plugin.id;
            r.id = it.AsString("id");
            if (r.id.empty()) continue;
            r.key = plugin.id + ":" + r.id;
            r.title = it.AsString("title", r.id);
            r.mediaType = it.AsString("mediaType", "video");
            r.animated = it.AsBool("animated", r.mediaType == "video");
        r.tags = CollectTags(it);
        {
            // Contrato v2: width/height en el nivel del item. "resolution"
            // se mantiene como respaldo por compatibilidad.
            r.width = (int)it.AsNum("width", 0);
            r.height = (int)it.AsNum("height", 0);
            const JVal* res = it.Get("resolution");
            if (res && res->type == JVal::Obj) {
                if (r.width <= 0) r.width = (int)res->AsNum("width", 0);
                if (r.height <= 0) r.height = (int)res->AsNum("height", 0);
            }
        }
            r.publishedAt = it.AsString("publishedAt");
            r.downloadable = it.AsBool("downloadable", true);
            const JVal* media = it.Get("media");
            if (media && media->type == JVal::Obj) {
                r.mediaUrl = media->AsString("url");
                r.sizeBytes = (long long)media->AsNum("sizeBytes", 0);
            }
            if (!r.sizeBytes) r.sizeBytes = (long long)it.AsNum("sizeBytes", 0);
            const JVal* thumbs = it.Get("thumbs");
            if (thumbs && thumbs->type == JVal::Obj) {
                r.animatedThumb = thumbs->AsString("animatedUrl");
                r.staticThumb = thumbs->AsString("staticUrl");
                r.staticCropped = thumbs->AsBool("staticCropped", false);
                r.animatedCropped = thumbs->AsBool("animatedCropped", false);
                if (r.staticThumb.empty())
                    r.staticThumb = r.animatedThumb;
            }
            const JVal* rating = it.Get("rating");
            if (rating && rating->type == JVal::Obj)
                r.rating = rating->AsNum("average", -1.0);
            if (r.mediaUrl.empty() && r.staticThumb.empty()) continue;
            all.push_back(std::move(r));
            if ((int)all.size() >= maxItems) break;
        }
        if (onPage) onPage(all);
        bool hasMore = root.AsBool("hasMore", false);
        if (!hasMore) break;
    }
    return all;
}

// ---- recarga profunda del catálogo ----------------------------------------
// El arranque sólo trae la primera página de cada plugin. Esto pide varias
// páginas por plugin (en serie, para no machacar los registros) y guarda el
// snapshot. Se usa desde la UI ("Recargar catálogo").
static std::atomic<bool> g_deepRunning{false};
static std::atomic<int> g_deepDone{0};
static std::atomic<int> g_deepTotal{0};

bool DeepScanRunning() { return g_deepRunning.load(); }

std::string DeepScanStatusJson() {
    char b[192];
    sprintf_s(b, "{\"running\":%s,\"done\":%d,\"total\":%d,\"items\":%d}",
              g_deepRunning.load() ? "true" : "false", g_deepDone.load(),
              g_deepTotal.load(), (int)GetRemoteRegistryCopy().size());
    return b;
}

int RefreshCatalogDeep(const std::wstring& baseDir, int pages) {
    if (g_deepRunning.exchange(true)) return 0;
    if (pages < 1) pages = 1;
    if (pages > 10) pages = 10;
    auto plugins = DiscoverPlugins(baseDir);
    g_deepTotal = (int)plugins.size();
    g_deepDone = 0;
    for (auto& p : plugins) {
        ListPluginItemsPaged(p, pages * 100,
                             [](const std::vector<RemoteItem>& page) {
                                 MergeRemoteItems(page);
                             });
        ++g_deepDone;
    }
    SaveCatalogSnapshot(baseDir, GetRemoteRegistryCopy());
    BumpRemoteVersion();
    g_deepRunning = false;
    return (int)plugins.size();
}

// Declaraciones anticipadas (se definen más abajo, junto a la descarga).

static std::wstring PluginBaseDir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    cached = exe;
    size_t k = cached.find_last_of(L"\\/");
    if (k != std::wstring::npos) cached = cached.substr(0, k);
    return cached;
}

// Igual que MediaPathFor pero SIN crear carpetas: se usa para comprobar si
// un ítem ya está descargado al construir el catálogo.
std::wstring MediaPathForCheck(const std::wstring& baseDir,
                               const RemoteItem& item) {
    // El plugin local sirve los ficheros en su sitio: la URL trae la ruta
    // absoluta codificada (http://wpx.internal/__file/D%3A%2F...).
    const std::string prefix = "http://wpx.internal/__file/";
    if (item.mediaUrl.rfind(prefix, 0) == 0) {
        std::string enc = item.mediaUrl.substr(prefix.size());
        size_t q = enc.find_first_of("?#");
        if (q != std::string::npos) enc = enc.substr(0, q);
        std::string dec;
        for (size_t i = 0; i < enc.size(); ++i) {
            if (enc[i] == '%' && i + 2 < enc.size()) {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                const int hi = hex(enc[i + 1]);
                const int lo = hex(enc[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    dec.push_back((char)((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            dec.push_back(enc[i] == '+' ? ' ' : enc[i]);
        }
        if (!dec.empty()) return ToWide(dec);
    }
    std::wstring ext = L".mp4";
    std::wstring low = ToWide(item.mediaUrl);
    for (auto& c : low) c = (wchar_t)towlower(c);
    size_t q = low.find_first_of(L"?#");
    if (q != std::wstring::npos) low = low.substr(0, q);
    if (low.size() > 5 && low.rfind(L".webm") == low.size() - 5) ext = L".webm";
    else if (low.size() > 4 && low.rfind(L".mkv") == low.size() - 4) ext = L".mkv";
    else if (low.size() > 4 && low.rfind(L".mov") == low.size() - 4) ext = L".mov";
    else if (low.size() > 5 && low.rfind(L".webp") == low.size() - 5) ext = L".webp";
    else if (low.size() > 4 && low.rfind(L".png") == low.size() - 4) ext = L".png";
    else if (low.size() > 4 && low.rfind(L".jpg") == low.size() - 4) ext = L".jpg";
    return baseDir + L"\\res\\providers\\" + ToWide(item.provider) + L"\\media\\" +
           ToWide(item.id) + ext;
}

std::string BuildRemoteItemsJson(const std::vector<RemoteItem>& items) {
    const std::wstring baseDir = PluginBaseDir();
    auto esc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o.push_back('\\');
            if (c == '\n') { o += "\\n"; continue; }
            o.push_back(c);
        }
        return o;
    };
    std::string json = "[";
    bool first = true;
    for (const auto& it : items) {
        if (!first) json.push_back(',');
        first = false;
        std::string title = it.title;
        if (it.animated) title += "  [Animado]";
        json += "{\"file\":\"wpxr:" + esc(it.key) + "\"";
        json += ",\"workshopid\":\"" + esc(it.key) + "\"";
        json += ",\"title\":\"" + esc(title) + "\"";
        json += ",\"type\":\"";
        json += (it.mediaType == "image") ? "Image" : "Video";
        json += "\"";
        if (!it.tags.empty()) json += ",\"tags\":\"" + esc(it.tags) + "\"";
        if (it.width > 0 && it.height > 0) {
            char rbuf[64];
            sprintf_s(rbuf, ",\"width\":%d,\"height\":%d", it.width, it.height);
            json += rbuf;
            sprintf_s(rbuf, ",\"resolution\":{\"width\":%d,\"height\":%d}",
                      it.width, it.height);
            json += rbuf;
        }
        json += ",\"official\":false,\"local\":true";
        // Descarga ya presente en disco (de una sesión anterior): se anuncia
        // como instalada y con su ruta local, para que "Add wallpaper" y el
        // preview funcionen sin volver a descargar.
        std::wstring localFile = MediaPathForCheck(baseDir, it);
        long long localSize = 0;
        {
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExW(localFile.c_str(), GetFileExInfoStandard,
                                     &fad)) {
                localSize = ((long long)fad.nFileSizeHigh << 32) |
                            fad.nFileSizeLow;
            }
        }
        if (localSize > 0) {
            json += ",\"status\":\"installed\"";
            json += ",\"localpath\":\"" + esc(ToUtf8(localFile)) + "\"";
            json += ",\"sizeBytes\":" + std::to_string(localSize);
        } else {
            json += ",\"status\":\"downloadable\"";
        }
        json += ",\"provider\":\"" + esc(it.provider) + "\"";
        json += ",\"publishedAt\":\"" + esc(it.publishedAt) + "\"";
        if (it.rating >= 0) {
            char r[32];
            sprintf_s(r, "%.1f", it.rating);
            json += ",\"rating\":";
            json += r;
        }
        std::string tprev = it.staticThumb;
        if (it.animatedThumb.rfind("http://wpx.internal/", 0) == 0) {
            tprev = it.animatedThumb;  // preview animado también en el panel
        }
        json += ",\"preview\":\"" + esc(tprev) + "\"";
        std::string tsmall = it.staticThumb;
        // Sólo usar la miniatura animada si ya está cacheada en disco
        // (wpx.internal); un mp4/webm remoto no se puede pintar en un <img>.
        if (it.animatedThumb.rfind("http://wpx.internal/", 0) == 0) {
            tsmall = it.animatedThumb;
        }
        json += ",\"previewsmall\":\"" + esc(tsmall) + "\"";
        json += ",\"animated\":";
        json += it.animated ? "true" : "false";
        json += "}";
    }
    json.push_back(']');
    return json;
}

void SetRemoteRegistry(const std::vector<RemoteItem>& items) {
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_registry.clear();
        for (const auto& it : items) g_registry[it.key] = it;
    }
    BumpRemoteVersion();
}

const RemoteItem* FindRemoteItem(const std::string& key) {
    // El registro es pequeño; se devuelve una copia estática protegida por
    // mutex en el llamador mediante Fetch/Apply.
    static RemoteItem copy;
    std::lock_guard<std::mutex> lock(g_registryMutex);
    auto it = g_registry.find(key);
    if (it == g_registry.end()) return nullptr;
    copy = it->second;
    return &copy;
}

std::wstring LocalPathFromWpxUrl(const std::string& url) {
    const char* prefix = "http://wpx.internal/__file/";
    if (url.rfind(prefix, 0) != 0) return {};
    std::string enc = url.substr(strlen(prefix));
    std::string dec;
    for (size_t i = 0; i < enc.size(); ++i) {
        if (enc[i] == '%' && i + 2 < enc.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int a = hex(enc[i + 1]), b = hex(enc[i + 2]);
            if (a >= 0 && b >= 0) {
                dec.push_back((char)((a << 4) | b));
                i += 2;
                continue;
            }
        }
        dec.push_back(enc[i]);
    }
    return ToWide(dec);
}

// Nombre de fichero seguro a partir de una clave (los ids llevan @, :, /...).
static std::wstring SafeStem(const std::string& s) {
    std::wstring w = ToWide(s);
    for (auto& c : w) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|')
            c = L'_';
    }
    if (w.size() > 120) w = w.substr(0, 120);
    return w;
}

// Mensaje de error que devuelve el plugin en su JSON ({"code","message"}).
static std::string PluginJsonMessage(const std::string& json) {
    size_t p = json.find("\"message\"");
    if (p == std::string::npos) return {};
    size_t a = json.find('"', json.find(':', p));
    if (a == std::string::npos) return {};
    std::string out;
    for (size_t i = a + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) { out.push_back(json[++i]); continue; }
        if (json[i] == '"') break;
        out.push_back(json[i]);
    }
    return out;
}

std::wstring FetchRemoteItemKind(const std::wstring& baseDir,
                                 const RemoteItem& item,
                                 const std::string& kind) {
    // Si el medio ya es local (mock o plugin que lo sirve), usarlo tal cual.
    std::wstring local = LocalPathFromWpxUrl(item.mediaUrl);
    if (!local.empty() &&
        GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return local;
    }
    auto plugins = DiscoverPlugins(baseDir);
    const PluginInfo* target = nullptr;
    for (const auto& p : plugins)
        if (p.id == item.provider) target = &p;
    if (!target) return {};

    std::wstring out;
    if (kind == "preview") {
        // Clip ligero para la vista previa animada (se cachea aparte).
        std::wstring dir = baseDir + L"\\res\\cache\\preview\\plugin";
        CreateDirectoryW((baseDir + L"\\res\\cache").c_str(), nullptr);
        CreateDirectoryW((baseDir + L"\\res\\cache\\preview").c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
        out = dir + L"\\" + SafeStem(item.key) + L"-clip.webm";
    } else {
        // Destino: res\providers\<provider>\media\<id>.<ext>
        std::wstring pdir = baseDir + L"\\res\\providers\\" + ToWide(item.provider);
        CreateDirectoryW((baseDir + L"\\res\\providers").c_str(), nullptr);
        CreateDirectoryW(pdir.c_str(), nullptr);
        std::wstring mdir = pdir + L"\\media";
        CreateDirectoryW(mdir.c_str(), nullptr);
        std::wstring ext = L".mp4";
        std::wstring low = ToWide(item.mediaUrl);
        for (auto& c : low) c = (wchar_t)towlower(c);
        size_t q = low.find_first_of(L"?#");
        if (q != std::wstring::npos) low = low.substr(0, q);
        if (low.size() > 5 && low.rfind(L".webm") == low.size() - 5) ext = L".webm";
        else if (low.size() > 4 && low.rfind(L".mkv") == low.size() - 4) ext = L".mkv";
        else if (low.size() > 4 && low.rfind(L".mov") == low.size() - 4) ext = L".mov";
        else if (low.size() > 4 && low.rfind(L".webp") == low.size() - 4) ext = L".webp";
        else if (low.size() > 4 && low.rfind(L".png") == low.size() - 4) ext = L".png";
        else if (low.size() > 4 && low.rfind(L".jpg") == low.size() - 4) ext = L".jpg";
        else if (low.size() > 5 && low.rfind(L".jpeg") == low.size() - 5) ext = L".jpg";
        else if (low.size() > 4 && low.rfind(L".gif") == low.size() - 4) ext = L".gif";
        else if (low.size() > 4 && low.rfind(L".avi") == low.size() - 4) ext = L".avi";
        out = mdir + L"\\" + ToWide(item.id) + ext;
    }

    // Descarga limpia: un resto anterior (cortado) no debe darse por bueno.
    DeleteFileW(out.c_str());
    std::wstring args = L"fetch --id " + ToWide(item.id) + L" --out \"" + out + L"\"";
    if (kind != "media") args += L" --kind " + ToWide(kind);
    int code = -1;
    std::string json = RunPluginCommand(*target, args, 180000, &code);
    if (!MediaFileLooksValid(out)) {
        DeleteFileW(out.c_str());
        const std::string msg = PluginJsonMessage(json);
        DlLog("lan: fetch plugin %s FALLO (%s exit=%d)%s%s", item.key.c_str(),
              item.provider.c_str(), code, msg.empty() ? "" : " ", msg.c_str());
        return {};
    }
    return out;
}

std::wstring FetchRemoteItem(const std::wstring& baseDir, const RemoteItem& item) {
    return FetchRemoteItemKind(baseDir, item, "media");
}


namespace {
std::vector<PluginInfo> g_knownPlugins;
std::mutex g_knownMutex;
}

void SetKnownPlugins(const std::vector<PluginInfo>& plugins) {
    std::lock_guard<std::mutex> lock(g_knownMutex);
    g_knownPlugins = plugins;
}

bool IsPluginEnabled(const std::string& id) {
    FILE* f = nullptr;
    _wfopen_s(&f, L"res\\config\\plugins.json", L"rb");
    if (!f) return true;
    std::string j;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    j.assign((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) fread(j.data(), 1, (size_t)n, f);
    fclose(f);
    size_t p = j.find("\"disabled\"");
    if (p == std::string::npos) return true;
    size_t a = j.find('[', p);
    size_t b = j.find(']', a);
    if (a == std::string::npos || b == std::string::npos) return true;
    return j.substr(a, b - a).find("\"" + id + "\"") == std::string::npos;
}

std::string BuildPluginListJson() {
    std::lock_guard<std::mutex> lock(g_knownMutex);
    auto esc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o.push_back('\\');
            if (c == '\n') { o += "\\n"; continue; }
            o.push_back(c);
        }
        return o;
    };
    std::string json = "[";
    bool first = true;
    for (const auto& p : g_knownPlugins) {
        if (!first) json.push_back(',');
        first = false;
        json += "{\"id\":\"" + esc(p.id) + "\",\"name\":\"" +
                esc(p.name) + "\",\"enabled\":" +
                (IsPluginEnabled(p.id) ? "true" : "false") +
                ",\"config\":" +
                (GetFileAttributesW((PluginBaseDir() + L"\\plugins\\" +
                                     ToWide(p.id) + L"\\config.json").c_str()) !=
                         INVALID_FILE_ATTRIBUTES
                     ? "true"
                     : "false") +
                "}";
    }
    json.push_back(']');
    return json;
}

std::string LoadPluginPageJson(const std::wstring& baseDir,
                               const std::string& pluginId, int page,
                               const std::string& query, int limit) {
    PluginInfo target;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_knownMutex);
        for (const auto& p : g_knownPlugins) {
            if (p.id == pluginId) {
                target = p;
                found = true;
                break;
            }
        }
    }
    std::vector<RemoteItem> fetched;
    if (found) {
        std::wstring args = L"list --limit " + std::to_wstring(limit) +
                            L" --page " + std::to_wstring(page > 0 ? page : 1);
        if (!query.empty()) {
            args += L" --query \"" + ToWide(query) + L"\"";
        }
        std::string json = RunPluginCommand(target, args, 20000);
        if (!json.empty()) {
            JVal root = ParseJson(json);
            const JVal* items = root.Get("items");
            if (items && items->type == JVal::Arr) {
                for (const auto& it : items->arr) {
                    if (it.type != JVal::Obj) continue;
                    RemoteItem r;
                    r.provider = target.id;
                    r.id = it.AsString("id");
                    if (r.id.empty()) continue;
                    r.key = r.provider + ":" + r.id;
                    r.title = it.AsString("title", r.id);
                    r.mediaType = it.AsString("mediaType", "video");
                    r.animated = it.AsBool("animated", r.mediaType == "video");
        r.tags = CollectTags(it);
        {
            // Contrato v2: width/height en el nivel del item. "resolution"
            // se mantiene como respaldo por compatibilidad.
            r.width = (int)it.AsNum("width", 0);
            r.height = (int)it.AsNum("height", 0);
            const JVal* res = it.Get("resolution");
            if (res && res->type == JVal::Obj) {
                if (r.width <= 0) r.width = (int)res->AsNum("width", 0);
                if (r.height <= 0) r.height = (int)res->AsNum("height", 0);
            }
        }
                    r.publishedAt = it.AsString("publishedAt");
                    r.downloadable = it.AsBool("downloadable", true);
                    const JVal* media = it.Get("media");
                    if (media && media->type == JVal::Obj) r.mediaUrl = media->AsString("url");
                    const JVal* thumbs = it.Get("thumb");
                    if (!thumbs) thumbs = it.Get("thumbs");
                    if (thumbs && thumbs->type == JVal::Obj) {
                        r.animatedThumb = thumbs->AsString("animatedUrl");
                        r.staticThumb = thumbs->AsString("staticUrl");
                        r.staticCropped = thumbs->AsBool("staticCropped", false);
                        r.animatedCropped = thumbs->AsBool("animatedCropped", false);
                        if (r.staticThumb.empty()) r.staticThumb = r.animatedThumb;
                    }
                    const JVal* rating = it.Get("rating");
                    if (rating && rating->type == JVal::Obj) r.rating = rating->AsNum("average", -1.0);
                    if (r.mediaUrl.empty() && r.staticThumb.empty()) continue;
                    fetched.push_back(std::move(r));
                }
            }
        }
    }
    // Conservar miniaturas ya cacheadas al recargar páginas del plugin.
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        for (auto& it : fetched) {
            auto o = g_registry.find(it.key);
            if (o == g_registry.end()) continue;
            if (!IsHttpUrl(o->second.staticThumb) &&
                !o->second.staticThumb.empty()) {
                it.staticThumb = o->second.staticThumb;
            }
            if (!IsHttpUrl(o->second.animatedThumb) &&
                !o->second.animatedThumb.empty()) {
                it.animatedThumb = o->second.animatedThumb;
            }
        }
    }
    // Reemplazar ítems previos de este plugin por la nueva página acumulada.
    std::vector<RemoteItem> merged;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        for (const auto& kv : g_registry) {
            if (kv.second.provider != pluginId) merged.push_back(kv.second);
        }
    }
    for (auto& it : fetched) {
        bool dup = false;
        for (const auto& m : merged)
            if (m.key == it.key) { dup = true; break; }
        if (!dup) merged.push_back(it);
    }
    SetRemoteRegistry(merged);
    // Las miniaturas se piden bajo demanda (/api/thumb) y se cachean: calentar
    // aqui bloqueaba cada pagina varios segundos.
    return BuildRemoteItemsJson(merged);
}

// Búsqueda en vivo en un plugin (`list --query ...`). Devuelve los items sin
// tocar el registro: quien llama decide si los incorpora.
std::vector<RemoteItem> SearchPluginItems(const std::wstring& baseDir,
                                          const std::string& pluginId,
                                          const std::string& query, int limit) {
    std::vector<RemoteItem> out;
    if (query.empty() || limit <= 0) return out;
    // Plugins deshabilitados: no se consultan.
    {
        std::string cfg = ReadAll(baseDir + L"\\res\\config\\plugins.json");
        size_t p = cfg.find("\"disabled\"");
        if (p != std::string::npos) {
            size_t a = cfg.find('[', p);
            size_t b = cfg.find(']', a);
            if (a != std::string::npos && b != std::string::npos &&
                cfg.substr(a, b - a).find("\"" + pluginId + "\"") !=
                    std::string::npos) {
                return out;
            }
        }
    }
    PluginInfo target;
    {
        std::lock_guard<std::mutex> lock(g_knownMutex);
        for (const auto& p : g_knownPlugins) {
            if (p.id == pluginId) { target = p; break; }
        }
    }
    if (target.id.empty()) return out;

    std::wstring args = L"list --limit " + std::to_wstring(limit) +
                        L" --page 1 --query \"" + ToWide(query) +
                        L"\" --sort relevance";
    std::string json = RunPluginCommand(target, args, 20000);
    if (json.empty()) return out;
    JVal root = ParseJson(json);
    const JVal* items = root.Get("items");
    if (!items || items->type != JVal::Arr) return out;
    for (const auto& it : items->arr) {
        if (it.type != JVal::Obj) continue;
        RemoteItem r;
        r.provider = target.id;
        r.id = it.AsString("id");
        if (r.id.empty()) continue;
        r.key = r.provider + ":" + r.id;
        r.title = it.AsString("title", r.id);
        r.mediaType = it.AsString("mediaType", "video");
        r.animated = it.AsBool("animated", r.mediaType == "video");
        r.tags = CollectTags(it);
        r.width = (int)it.AsNum("width", 0);
        r.height = (int)it.AsNum("height", 0);
        const JVal* res = it.Get("resolution");
        if (res && res->type == JVal::Obj) {
            if (r.width <= 0) r.width = (int)res->AsNum("width", 0);
            if (r.height <= 0) r.height = (int)res->AsNum("height", 0);
        }
        r.publishedAt = it.AsString("publishedAt");
        r.downloadable = it.AsBool("downloadable", true);
        const JVal* media = it.Get("media");
        if (media && media->type == JVal::Obj) r.mediaUrl = media->AsString("url");
        const JVal* thumbs = it.Get("thumb");
        if (!thumbs) thumbs = it.Get("thumbs");
        if (thumbs && thumbs->type == JVal::Obj) {
            r.animatedThumb = thumbs->AsString("animatedUrl");
            r.staticThumb = thumbs->AsString("staticUrl");
            r.staticCropped = thumbs->AsBool("staticCropped", false);
            r.animatedCropped = thumbs->AsBool("animatedCropped", false);
            if (r.staticThumb.empty()) r.staticThumb = r.animatedThumb;
        }
        const JVal* rating = it.Get("rating");
        if (rating && rating->type == JVal::Obj) {
            r.rating = rating->AsNum("average", -1.0);
        }
        if (r.mediaUrl.empty() && r.staticThumb.empty()) continue;
        out.push_back(std::move(r));
    }
    return out;
}


namespace {
struct DownloadState {
    bool active = false;
    bool done = false;
    bool error = false;
    std::string message;
    long long bytes = 0;
    long long total = 0;
    std::wstring path;
    unsigned long long finishedAt = 0;
};
std::mutex g_dlMutex;
std::map<std::string, DownloadState> g_dls;
constexpr int kMaxConcurrentDownloads = 3;

// Estado del escaneo inicial de plugins.
std::atomic<bool> g_pluginsLoading{false};
std::atomic<int> g_pluginsDone{0};
std::atomic<int> g_pluginsTotal{0};
std::atomic<int> g_pluginsItems{0};

DownloadState* FindDl(const std::string& key) {
    auto it = g_dls.find(key);
    return it == g_dls.end() ? nullptr : &it->second;
}

void PruneDls() {
    unsigned long long now = GetTickCount64();
    for (auto it = g_dls.begin(); it != g_dls.end();) {
        if (!it->second.active && it->second.finishedAt &&
            now - it->second.finishedAt > 10 * 60 * 1000) {
            it = g_dls.erase(it);
        } else {
            ++it;
        }
    }
    if (g_dls.size() > 60) {
        for (auto it = g_dls.begin(); it != g_dls.end() && g_dls.size() > 40;) {
            if (!it->second.active) it = g_dls.erase(it);
            else ++it;
        }
    }
}


std::wstring MediaPathFor(const std::wstring& baseDir, const RemoteItem& item) {
    std::wstring pdir = baseDir + L"\\res\\providers\\" + ToWide(item.provider);
    CreateDirectoryW((baseDir + L"\\res\\providers").c_str(), nullptr);
    CreateDirectoryW(pdir.c_str(), nullptr);
    std::wstring mdir = pdir + L"\\media";
    CreateDirectoryW(mdir.c_str(), nullptr);
    std::wstring ext = L".mp4";
    std::wstring low = ToWide(item.mediaUrl);
    for (auto& c : low) c = (wchar_t)towlower(c);
    // Quitar query/fragmento: ".../foto.JPG?utm_source=..." es .jpg, no .mp4.
    size_t q = low.find_first_of(L"?#");
    if (q != std::wstring::npos) low = low.substr(0, q);
    if (low.size() > 5 && low.rfind(L".webm") == low.size() - 5) ext = L".webm";
    else if (low.size() > 4 && low.rfind(L".mkv") == low.size() - 4) ext = L".mkv";
    else if (low.size() > 4 && low.rfind(L".mov") == low.size() - 4) ext = L".mov";
    else if (low.size() > 5 && low.rfind(L".webp") == low.size() - 5) ext = L".webp";
    else if (low.size() > 4 && low.rfind(L".png") == low.size() - 4) ext = L".png";
    else if (low.size() > 4 && low.rfind(L".jpg") == low.size() - 4) ext = L".jpg";
    return mdir + L"\\" + ToWide(item.id) + ext;
}


long long FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

// ---------- miniaturas: caché local ----------
std::atomic<bool> g_warmRunning{false};

std::string UrlEncodePath(const std::wstring& p) {
    std::string out;
    for (wchar_t c : p) {
        if (c == L'\\') {
            out.push_back('/');
        } else if (c == L' ') {
            out += "%20";
        } else if (c < 128) {
            out.push_back((char)c);
        } else {
            char buf[8];
            int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1, buf, sizeof(buf),
                                        nullptr, nullptr);
            for (int i = 0; i < n; ++i) {
                char hex[4];
                sprintf_s(hex, "%%%02X", (unsigned char)buf[i]);
                out += hex;
            }
        }
    }
    return out;
}

std::string FileUrl(const std::wstring& path) {
    return "http://wpx.internal/__file/" + UrlEncodePath(path);
}

std::wstring ExtFromUrl(const std::string& url) {
    std::string path = url;
    size_t q = path.find_first_of("?#");
    if (q != std::string::npos) path = path.substr(0, q);
    size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    std::wstring ext;
    if (dot != std::string::npos) {
        for (size_t i = dot; i < name.size(); ++i) {
            ext.push_back((wchar_t)tolower((unsigned char)name[i]));
        }
    }
    if (ext == L".jpeg") ext = L".jpg";
    if (ext != L".jpg" && ext != L".png" && ext != L".webp" &&
        ext != L".gif" && ext != L".bmp") {
        ext = L".jpg";
    }
    return ext;
}

std::wstring SanitizeId(const std::string& id) {
    std::wstring out;
    for (char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out.push_back((wchar_t)c);
        } else {
            out.push_back(L'_');
        }
    }
    if (out.size() > 120) out.resize(120);
    return out;
}

std::wstring FfmpegExe() {
    static std::wstring cached;
    static bool done = false;
    if (done) return cached;
    done = true;
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr)) {
        cached = buf;
        return cached;
    }
    if (GetFileAttributesW(L"C:\\Windows\\ffmpeg.exe") != INVALID_FILE_ATTRIBUTES) {
        cached = L"C:\\Windows\\ffmpeg.exe";
    }
    return cached;
}

// WinHTTP recibe la URL en UTF-16, pero los bytes no ASCII de una URL deben
// ir percent-encoded (si no, el servidor devuelve 404).
std::string UrlEscapeNonAscii(const std::string& u) {
    std::string out;
    out.reserve(u.size());
    for (unsigned char c : u) {
        if (c < 0x80) {
            out.push_back((char)c);
        } else {
            char hex[4];
            sprintf_s(hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

bool HttpDownloadToFile(const std::string& url, const std::wstring& outPath,
                        int timeoutMs = 25000,
                        const std::string& referer = std::string(),
                        const std::wstring& progressPath = std::wstring()) {
    std::wstring wurl = ToWide(UrlEscapeNonAscii(url));
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[8192] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 8191;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        DlLog("http: URL invalida %s", url.c_str());
        return false;
    }

    HINTERNET ses = WinHttpOpen(L"DNXWallpaperV3/1.0",
                                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) {
        DlLog("http: WinHttpOpen fallo");
        return false;
    }
    WinHttpSetTimeouts(ses, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(ses, WINHTTP_OPTION_DECOMPRESSION, &decompress,
                     sizeof(decompress));
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) {
        WinHttpCloseHandle(ses);
        DlLog("http: connect fallo (%ls)", host);
        return false;
    }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        DlLog("http: OpenRequest fallo");
        return false;
    }

    // Cabeceras de navegador: sin User-Agent muchos CDN (Wikimedia,
    // Wallhaven, Cloudflare) devuelven 403 aunque la URL sea correcta.
    std::wstring hdr =
        L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        L"AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 "
        L"Safari/537.36\r\n"
        L"Accept: image/avif,image/webp,image/apng,image/*,video/*,*/*;q=0.8\r\n"
        L"Accept-Language: en-US,en;q=0.9\r\n";
    if (!referer.empty()) {
        hdr += L"Referer: " + ToWide(UrlEscapeNonAscii(referer)) + L"\r\n";
    }
    WinHttpAddRequestHeaders(req, hdr.c_str(), (DWORD)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    // Se descarga a ".part" y se renombra al final: nunca dejamos un medio
    // truncado con la extensión definitiva.
    std::wstring partPath = outPath + L".part";
    DeleteFileW(partPath.c_str());

    bool ok = false;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                            WINHTTP_NO_HEADER_INDEX);
        if (status >= 200 && status < 300) {
            FILE* f = nullptr;
            _wfopen_s(&f, partPath.c_str(), L"wb");
            if (f) {
                DWORD avail = 0, got = 0;
                char buf[64 * 1024];
                ok = true;
                do {
                    avail = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail)) {
                        ok = false;
                        break;
                    }
                    if (!avail) break;
                    DWORD want = avail > sizeof(buf) ? sizeof(buf) : avail;
                    if (WinHttpReadData(req, buf, want, &got) && got > 0) {
                        fwrite(buf, 1, got, f);
                    } else {
                        ok = false;
                        break;
                    }
                } while (got > 0);
                fclose(f);
                if (!ok || FileSize(partPath) <= 0) {
                    ok = false;
                } else if (!MoveFileExW(partPath.c_str(), outPath.c_str(),
                                        MOVEFILE_REPLACE_EXISTING)) {
                    ok = false;
                }
            }
        } else {
            DlLog("http: status %lu en %s", (unsigned long)status, url.c_str());
        }
    } else {
        DlLog("http: sin respuesta (err %lu) %s", (unsigned long)GetLastError(),
              url.c_str());
    }
    if (!ok) DeleteFileW(partPath.c_str());
    (void)progressPath;
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

bool ConvertToAnimatedWebp(const std::wstring& src, const std::wstring& dst) {
    std::wstring ff = FfmpegExe();
    if (ff.empty()) return false;
    std::wstring cmd = L"\"" + ff + L"\" -y -v error -i \"" + src +
                       L"\" -t 6 -vf \"fps=10,scale=320:-2:flags=lanczos\" "
                       L"-c:v libwebp_anim -lossless 0 -q:v 55 -loop 0 -an \"" +
                       dst + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    BOOL launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                                   FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                                   &si, &pi);
    if (!launched) return false;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 && FileSize(dst) > 0;
}

void SetRegistryThumb(const std::string& key, bool animated,
                      const std::string& localUrl) {
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto it = g_registry.find(key);
        if (it == g_registry.end()) return;
        if (animated) it->second.animatedThumb = localUrl;
        else it->second.staticThumb = localUrl;
    }
    BumpRemoteVersion();
}

void EnsureMotorRunning(const std::wstring& baseDir) {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"dnxWallpaperV3Mutex");
    if (m) { CloseHandle(m); return; }
    std::wstring exe = baseDir + L"\\DNXWallpaper.Motor.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, baseDir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// Genera miniatura a partir del medio ya descargado:
//  - vídeo  -> WebP animado (320 px, 6 s)
//  - imagen -> WebP estático (640 px)
void GenerateThumbFromMedia(const std::wstring& baseDir, const RemoteItem& item,
                            const std::wstring& mediaPath) {
    std::wstring ff = FfmpegExe();
    if (ff.empty()) return;
    std::wstring dir = baseDir + L"\\res\\cache\\thumbs\\remote2\\" +
                       ToWide(item.provider);
    CreateDirectoryW((baseDir + L"\\res\\cache\\thumbs").c_str(), nullptr);
    CreateDirectoryW((baseDir + L"\\res\\cache\\thumbs\\remote2").c_str(),
                     nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);

    std::wstring low = mediaPath;
    for (auto& c : low) c = (wchar_t)towlower(c);
    auto ends = [&low](const wchar_t* ext) {
        size_t n = wcslen(ext);
        return low.size() > n && low.rfind(ext) == low.size() - n;
    };
    bool isVideo = ends(L".mp4") || ends(L".webm") || ends(L".mkv") ||
                   ends(L".mov") || ends(L".avi") || ends(L".wmv");

    std::wstring base = dir + L"\\" + SanitizeId(item.id);
    std::wstring out = base + L".thumb.webp";
    // Restos de versiones anteriores con tamaño 0: no deben servirse.
    for (const wchar_t* legacy : {L".webp", L".thumb.webp"}) {
        std::wstring p2 = base + legacy;
        if (GetFileAttributesW(p2.c_str()) != INVALID_FILE_ATTRIBUTES &&
            FileSize(p2) <= 0) {
            DeleteFileW(p2.c_str());
        }
    }
    if (FileSize(out) > 0) {
        SetRegistryThumb(item.key, isVideo, FileUrl(out));
        return;
    }

    std::wstring cmd;
    if (isVideo) {
        cmd = L"\"" + ff + L"\" -y -v error -i \"" + mediaPath +
              L"\" -t 6 -vf \"fps=10,scale=320:-2:flags=lanczos\" "
              L"-c:v libwebp_anim -lossless 0 -q:v 55 -loop 0 -an \"" +
              out + L"\"";
    } else {
        cmd = L"\"" + ff + L"\" -y -v error -i \"" + mediaPath +
              L"\" -frames:v 1 -vf \"scale=640:-2:flags=lanczos\" "
              L"-c:v libwebp -quality 82 -an \"" + out + L"\"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mcmd = cmd;
    if (CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 120000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (code == 0 && FileSize(out) > 0) {
            SetRegistryThumb(item.key, isVideo, FileUrl(out));
            DlLog("thumb %s generada (%s, %lld bytes)", item.id.c_str(),
                  isVideo ? "video" : "imagen", FileSize(out));
        } else {
            DeleteFileW(out.c_str());
            DlLog("thumb %s FALLO (ffmpeg %lu)", item.id.c_str(),
                  (unsigned long)code);
        }
    }
}


void ApplyLocalWallpaper(const std::wstring& baseDir, const std::wstring& path) {
    std::string pathUtf8;
    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0,
                                nullptr, nullptr);
    pathUtf8.resize((size_t)(n > 0 ? n - 1 : 0));
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, pathUtf8.data(), n,
                            nullptr, nullptr);
    }
    std::string esc;
    for (char c : pathUtf8) {
        if (c == '\\' || c == '"') esc.push_back('\\');
        esc.push_back(c);
    }
    FILE* f = nullptr;
    _wfopen_s(&f, (baseDir + L"\\res\\current.json").c_str(), L"w, ccs=UTF-8");
    if (f) {
        fwprintf(f, L"{\"project\": \"%hs\"}", esc.c_str());
        fclose(f);
        DlLog("apply escrito current.json -> %s", pathUtf8.c_str());
    } else {
        DlLog("apply ERROR: no se pudo escribir current.json");
    }
    EnsureMotorRunning(baseDir);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, L"dnxWallpaperV3Reload");
    if (ev) {
        SetEvent(ev);
        CloseHandle(ev);
    }
}


std::string ToUtf8Ext(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    std::string out;
    if (dot == std::wstring::npos) return out;
    for (size_t i = dot; i < path.size(); ++i)
        out.push_back((char)path[i]);
    return out;
}

// Detecta el tipo real por cabecera. Devuelve extensión (.mp4/.webm/.jpg/...)
// o "" si no es un medio reconocible (p. ej. una página HTML de error).
std::wstring SniffMediaExt(const std::wstring& path) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return {};
    unsigned char b[32]{};
    size_t n = fread(b, 1, sizeof(b), f);
    fclose(f);
    if (n < 12) return {};
    auto at = [&](size_t o, const char* sig, size_t len) {
        return n >= o + len && memcmp(b + o, sig, len) == 0;
    };
    if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return L".jpg";
    if (b[0] == 0x89 && at(1, "PNG\r\n", 4)) return L".png";
    if (memcmp(b, "GIF8", 4) == 0) return L".gif";
    if (memcmp(b, "RIFF", 4) == 0 && at(8, "WEBP", 4)) return L".webp";
    if (memcmp(b, "RIFF", 4) == 0 && at(8, "AVI ", 4)) return L".avi";
    if (memcmp(b, "PK", 2) == 0) return {};
    if (at(4, "ftyp", 4) && (at(8, "avif", 4) || at(8, "avis", 4))) return L".avif";
    if (at(4, "ftyp", 4)) {
        if (at(8, "qt  ", 4)) return L".mov";
        return L".mp4";
    }
    if (b[0] == 0x1A && b[1] == 0x45 && b[2] == 0xDF && b[3] == 0xA3) {
        // EBML: webm o mkv (el motor soporta webm/mkv por extensión).
        for (size_t i = 0; i + 4 < n; ++i) {
            if (memcmp(b + i, "webm", 4) == 0) return L".webm";
        }
        return L".mkv";
    }
    if ((b[0] == 0x30 && b[1] == 0x26 && b[2] == 0xB2) || at(0, "ASF", 3)) {
        return L".wmv";
    }
    if (memcmp(b, "FLV", 3) == 0) return L".flv";
    if (b[0] == 0x47) return L".ts";                   // MPEG-TS
    if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01) return L".mpg"; // MPEG-PS
    return {};
}

// Consulta `detail` para obtener la URL real del medio (algunos plugins solo
// la entregan ahí; en `list` viene vacía).
bool ResolveItemDetail(const PluginInfo& plugin, const std::string& id,
                       std::string& mediaUrl, std::string& animatedUrl,
                       std::string& pageUrl, long long& sizeBytes) {
    std::wstring args = L"detail --id " + ToWide(id);
    int code = -1;
    std::string out = RunPluginCommand(plugin, args, 30000, &code);
    if (out.empty()) return false;
    JVal root = ParseJson(out);
    const JVal* item = root.Get("item");
    if (!item || item->type != JVal::Obj) {
        if (root.type == JVal::Obj) item = &root;
        else return false;
    }
    const JVal* media = item->Get("media");
    if (media && media->type == JVal::Obj) {
        std::string u = media->AsString("url");
        if (!u.empty()) mediaUrl = u;
        double sz = media->AsNum("sizeBytes", 0);
        if (sz > 0) sizeBytes = (long long)sz;
    }
    const JVal* thumbs = item->Get("thumbs");
    if (thumbs && thumbs->type == JVal::Obj) {
        std::string a = thumbs->AsString("animatedUrl");
        if (!a.empty()) animatedUrl = a;
    }
    pageUrl = item->AsString("pageUrl");
    return !mediaUrl.empty();
}
} // namespace

// Un fichero descargado no siempre es un medio: los CDN protegidos devuelven
// HTML (403/anti-bot) y guardarlo como .mp4 rompía ffmpeg después. Se reconoce
// por cabecera y se exige un tamaño mínimo (un HTML de error son pocos bytes).
bool MediaFileLooksValid(const std::wstring& path) {
    if (FileSize(path) < 1024) return false;
    return !SniffMediaExt(path).empty();
}

bool StartRemoteDownload(const std::wstring& baseDir, const RemoteItem& item) {
    const std::string key = item.key;
    {
        std::lock_guard<std::mutex> lock(g_dlMutex);
        PruneDls();
        int activeCount = 0;
        for (auto& kv : g_dls) {
            if (kv.second.active) ++activeCount;
        }
        if (activeCount >= kMaxConcurrentDownloads) return false;
        DownloadState& st = g_dls[key];
        if (st.active) return false;
        st = DownloadState{};
        st.active = true;
        st.total = item.sizeBytes;
    }

    // Medio ya local (wpx.internal): aplicar directamente.
    std::wstring local = LocalPathFromWpxUrl(item.mediaUrl);
    if (!local.empty() &&
        GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
        {
            std::lock_guard<std::mutex> lock(g_dlMutex);
            DownloadState& st = g_dls[key];
            st.active = false;
            st.done = true;
            st.path = local;
            st.bytes = st.total = FileSize(local);
            st.finishedAt = GetTickCount64();
        }
        // Fuera del mutex: aplicar implica escribir disco y arrancar el motor.
        ApplyLocalWallpaper(baseDir, local);
        return true;
    }

    RemoteItem it = item;
    std::thread([baseDir, it, key]() mutable {
        auto fail = [key](const char* msg) {
            std::lock_guard<std::mutex> lock(g_dlMutex);
            DownloadState& st = g_dls[key];
            st.active = false;
            st.error = true;
            st.message = msg;
            st.finishedAt = GetTickCount64();
        };
        PluginInfo target;
        bool found = false;
        for (const auto& p : DiscoverPlugins(baseDir)) {
            if (p.id == it.provider) { target = p; found = true; break; }
        }
        DlLog("%s start (mediaUrl=%s)", key.c_str(), it.mediaUrl.c_str());
        if (!found) {
            fail("plugin no encontrado");
            DlLog("%s FAIL plugin no encontrado", key.c_str());
            return;
        }

        // Resolver detalle si falta la URL del medio (waifu, moewalls...).
        std::string pageUrl;
        if (it.mediaUrl.empty()) {
            std::string media, anim;
            long long sz = 0;
            if (ResolveItemDetail(target, it.id, media, anim, pageUrl, sz)) {
                it.mediaUrl = media;
                if (it.sizeBytes <= 0) it.sizeBytes = sz;
                std::lock_guard<std::mutex> lock(g_dlMutex);
                DownloadState& st = g_dls[key];
                if (st.total <= 0) st.total = it.sizeBytes;
            }
            (void)anim;
        }

        std::wstring out = MediaPathFor(baseDir, it);
        // Ya descargado y válido: aplicar sin volver a bajar.
        if (FileSize(out) > 0) {
            std::wstring realE = SniffMediaExt(out);
            if (realE.empty()) {
                DeleteFileW(out.c_str());
            } else if (realE != ExtFromUrl(ToUtf8Ext(out))) {
                std::wstring fixed = out.substr(0, out.find_last_of(L'.')) + realE;
                MoveFileExW(out.c_str(), fixed.c_str(), MOVEFILE_REPLACE_EXISTING);
                out = fixed;
            }
            if (FileSize(out) > 0) {
                {
                    std::lock_guard<std::mutex> lock(g_dlMutex);
                    DownloadState& st = g_dls[key];
                    st.active = false;
                    st.done = true;
                    st.path = out;
                    st.bytes = st.total = FileSize(out);
                    st.finishedAt = GetTickCount64();
                }
                ApplyLocalWallpaper(baseDir, out);
                EnsureRemoteThumbAsync(baseDir, it.key);
                return;
            }
        }

        long long sz = 0;
        DlLog("%s descargando %s -> %ls", key.c_str(), it.mediaUrl.c_str(), out.c_str());
        if (IsHttpUrl(it.mediaUrl)) {
            HANDLE evDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::wstring partial = out + L".part";
            std::thread monitor([evDone, partial, key]() {
                while (WaitForSingleObject(evDone, 250) == WAIT_TIMEOUT) {
                    std::lock_guard<std::mutex> lock(g_dlMutex);
                    DownloadState* st = FindDl(key);
                    if (!st || !st->active) break;
                    long long s2 = FileSize(partial);
                    if (s2 > st->bytes) st->bytes = s2;
                    if (st->total <= 0) st->total = 0;
                }
            });
            HttpDownloadToFile(it.mediaUrl, out, 180000, pageUrl, partial);
            SetEvent(evDone);
            monitor.join();
            CloseHandle(evDone);
            sz = FileSize(out);
            if (sz > 0 && SniffMediaExt(out).empty()) {
                DeleteFileW(out.c_str());
                sz = 0;
            }
        }
        DlLog("%s http -> sz=%lld", key.c_str(), sz);
        if (sz <= 0) {
            std::wstring args = L"fetch --id " + ToWide(it.id) + L" --out \"" +
                                out + L"\"";
            RunPluginCommand(target, args, 90000);
            sz = FileSize(out);
            DlLog("%s fetch -> sz=%lld", key.c_str(), sz);
        }

        {
            std::lock_guard<std::mutex> lock(g_dlMutex);
            DownloadState& st = g_dls[key];
            st.active = false;
            std::wstring realE = SniffMediaExt(out);
            if (sz > 0 && realE.empty()) {
                DeleteFileW(out.c_str());
                sz = 0;
            } else if (sz > 0 && realE != ExtFromUrl(ToUtf8Ext(out))) {
                std::wstring fixed = out.substr(0, out.find_last_of(L'.')) + realE;
                if (MoveFileExW(out.c_str(), fixed.c_str(),
                                MOVEFILE_REPLACE_EXISTING)) {
                    out = fixed;
                }
            }
            st.bytes = sz;
            st.finishedAt = GetTickCount64();
            if (sz > 0) {
                st.done = true;
                st.path = out;
                if (st.total <= 0) st.total = sz;
            } else {
                st.error = true;
                st.message = "descarga fallida (formato no válido)";
            }
        }
        // Aplicar y generar miniatura FUERA del mutex: si no, la UI se queda
        // bloqueada en cada getDownloadStatus (ffmpeg puede tardar minutos) y
        // Chromium mata el proceso render (pantalla blanca).
        if (sz > 0) {
            ApplyLocalWallpaper(baseDir, out);
            EnsureRemoteThumbAsync(baseDir, it.key);
            DlLog("%s OK %ls (%lld bytes)", key.c_str(), out.c_str(), sz);
        } else {
            DlLog("%s FAIL formato no valido", key.c_str());
        }
    }).detach();
    return true;
}

std::string GetDownloadStatusJson() {
    std::lock_guard<std::mutex> lock(g_dlMutex);
    auto esc = [](const std::string& in) {
        std::string o;
        for (char c : in) {
            if (c == '"' || c == '\\') o.push_back('\\');
            o.push_back(c);
        }
        return o;
    };
    std::string json = "[";
    bool first = true;
    for (const auto& kv : g_dls) {
        const DownloadState& st = kv.second;
        if (!first) json += ",";
        first = false;
        // Sin buffer fijo: la ruta del medio puede superar cualquier tamaño
        // razonable y sprintf_s aborta el proceso (__fastfail) al truncar.
        json += "{\"key\":\"" + esc(kv.first) + "\",\"active\":";
        json += st.active ? "true" : "false";
        json += ",\"done\":";
        json += st.done ? "true" : "false";
        json += ",\"error\":";
        json += st.error ? "true" : "false";
        json += ",\"bytes\":" + std::to_string(st.bytes);
        json += ",\"total\":" + std::to_string(st.total);
        json += ",\"path\":\"" + esc(ToUtf8(st.path)) + "\"";
        json += ",\"message\":\"" + esc(st.message) + "\"}";
    }
    json += "]";
    return json;
}

void SetPluginProgress(bool loading, int done, int total, int items) {
    g_pluginsLoading = loading;
    if (done >= 0) g_pluginsDone = done;
    if (total >= 0) g_pluginsTotal = total;
    if (items >= 0) g_pluginsItems = items;
}

std::string GetPluginStatusJson() {
    char buf[160];
    sprintf_s(buf, "{\"loading\":%s,\"done\":%d,\"total\":%d,\"items\":%d}",
              g_pluginsLoading.load() ? "true" : "false",
              g_pluginsDone.load(), g_pluginsTotal.load(),
              g_pluginsItems.load());
    return std::string(buf);
}


void MergeRemoteItems(const std::vector<RemoteItem>& items) {
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        for (const auto& it : items) {
            RemoteItem entry = it;
            // No perder dimensiones ya sondeadas: el plugin puede devolverlas
            // vacías al recargar la página.
            auto old = g_registry.find(it.key);
            if (old != g_registry.end()) {
                if ((entry.width <= 0 || entry.height <= 0) &&
                    old->second.width > 0 && old->second.height > 0) {
                    entry.width = old->second.width;
                    entry.height = old->second.height;
                }
                // No perder miniaturas ya generadas al recargar una página:
                // el plugin vuelve a mandar su URL remota y el merge la
                // sustituiría por la de red, provocando regeneraciones.
                const std::string local = "http://wpx.internal/";
                if (entry.staticThumb.rfind(local, 0) != 0 &&
                    old->second.staticThumb.rfind(local, 0) == 0) {
                    entry.staticThumb = old->second.staticThumb;
                }
                if (entry.animatedThumb.rfind(local, 0) != 0 &&
                    old->second.animatedThumb.rfind(local, 0) == 0) {
                    entry.animatedThumb = old->second.animatedThumb;
                }
                // Una vez marcado como recortado, no perder el aviso al
                // recargar una página que no traiga width/height.
                if (old->second.staticCropped) entry.staticCropped = true;
                if (old->second.animatedCropped) entry.animatedCropped = true;
            }
            g_registry[it.key] = entry;
        }
    }
    g_snapDirty = true;
    BumpRemoteVersion();
}

void UpdateRemoteItemDims(const std::string& key, int width, int height) {
    if (width <= 0 || height <= 0) return;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto it = g_registry.find(key);
        if (it == g_registry.end()) return;
        if (it->second.width <= 0 || it->second.height <= 0) {
            it->second.width = width;
            it->second.height = height;
            changed = true;
        }
    }
    if (changed) {
        g_snapDirty = true;
        BumpRemoteVersion();
    }
}

void StartCatalogSaver(const std::wstring& baseDir) {
    static std::once_flag once;
    std::call_once(once, [baseDir]() {
        const std::wstring base = baseDir;
        std::thread([base]() {
            for (;;) {
                Sleep(30000);
                if (!g_snapDirty.exchange(false)) continue;
                SaveCatalogSnapshot(base, GetRemoteRegistryCopy());
            }
        }).detach();
    });
}

std::vector<RemoteItem> GetRemoteRegistryCopy() {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    std::vector<RemoteItem> out;
    out.reserve(g_registry.size());
    for (const auto& kv : g_registry) out.push_back(kv.second);
    return out;
}

// ---------------------------------------------------------------------------
// Cola de miniaturas (servidor LAN)
// ---------------------------------------------------------------------------
//
// Una cola FIFO por proveedor, reparto round-robin, máximo 2 workers por
// proveedor y 12 en total. Las claves visibles se empujan al principio de su
// cola desde POST /api/thumb/prefetch. Un fallo se reintenta con espera
// creciente (máximo 3 intentos) y después queda en cuarentena hasta que el
// cliente vuelva a pedirlo tras el cooldown.
static bool ConvertImageToWebp(const std::wstring& src,
                               const std::wstring& dst);
static bool GenerateRemoteThumbJob(const std::wstring& baseDir,
                                   const std::string& key);

namespace {

constexpr int kThumbWorkers = 12;
constexpr int kThumbPerProvider = 2;
constexpr int kThumbMaxAttempts = 3;
constexpr int kThumbRetryMs = 1500;
constexpr int kThumbCooldownMs = 60000;

struct ThumbQueue {
    std::mutex mu;
    std::condition_variable cv;
    std::map<std::string, std::deque<std::string>> pending;  // provider -> keys
    std::vector<std::string> providers;
    size_t rr = 0;
    std::set<std::string> known;    // en cola o ejecutándose
    std::set<std::string> running;
    std::map<std::string, int> runningByProvider;
    std::map<std::string, int> attempts;
    std::map<std::string, std::chrono::steady_clock::time_point> readyAt;
    std::map<std::string, std::chrono::steady_clock::time_point> failedAt;
    std::wstring baseDir;
    bool started = false;
};

ThumbQueue& ThumbQueueInstance() {
    static ThumbQueue q;
    return q;
}

void ThumbWorkerLoop() {
    ThumbQueue& q = ThumbQueueInstance();
    for (;;) {
        std::string key;
        std::string provider;
        std::wstring baseDir;
        {
            std::unique_lock<std::mutex> lk(q.mu);
            for (;;) {
                const auto now = std::chrono::steady_clock::now();
                bool found = false;
                const size_t n = q.providers.size();
                for (size_t step = 0; step < n && !found; ++step) {
                    const size_t idx = (q.rr + step) % n;
                    const std::string& p = q.providers[idx];
                    if (q.runningByProvider[p] >= kThumbPerProvider) continue;
                    auto it = q.pending.find(p);
                    if (it == q.pending.end() || it->second.empty()) continue;

                    // Rota las claves no listas (backoff) al final de la cola.
                    const size_t count = it->second.size();
                    for (size_t i = 0; i < count; ++i) {
                        std::string candidate = it->second.front();
                        it->second.pop_front();
                        auto ready = q.readyAt.find(candidate);
                        if (ready == q.readyAt.end() || ready->second <= now) {
                            key = candidate;
                            provider = p;
                            baseDir = q.baseDir;
                            q.rr = (idx + 1) % n;
                            q.running.insert(key);
                            q.runningByProvider[p] += 1;
                            found = true;
                            break;
                        }
                        it->second.push_back(candidate);
                    }
                }
                if (found) break;

                auto waitUntil = now + std::chrono::seconds(30);
                for (const auto& kv : q.readyAt) {
                    if (kv.second > now && kv.second < waitUntil) {
                        waitUntil = kv.second;
                    }
                }
                q.cv.wait_until(lk, waitUntil);
            }
        }

        const bool ok = GenerateRemoteThumbJob(baseDir, key);
        {
            std::lock_guard<std::mutex> lk(q.mu);
            q.running.erase(key);
            auto rp = q.runningByProvider.find(provider);
            if (rp != q.runningByProvider.end() && rp->second > 0) --rp->second;
            q.known.erase(key);

            const auto now = std::chrono::steady_clock::now();
            if (ok) {
                q.attempts.erase(key);
                q.readyAt.erase(key);
                q.failedAt.erase(key);
            } else {
                const int attempt = ++q.attempts[key];
                q.failedAt[key] = now;
                if (attempt < kThumbMaxAttempts) {
                    q.known.insert(key);
                    q.pending[provider].push_back(key);
                    q.readyAt[key] =
                        now + std::chrono::milliseconds(kThumbRetryMs * attempt);
                } else {
                    q.readyAt.erase(key);
                }
            }
            q.cv.notify_all();
        }
    }
}

void StartThumbWorkersLocked(ThumbQueue& q, const std::wstring& baseDir) {
    if (q.started) return;
    q.started = true;
    if (q.baseDir.empty()) q.baseDir = baseDir;
    for (int i = 0; i < kThumbWorkers; ++i) {
        std::thread(ThumbWorkerLoop).detach();
    }
}

bool EnqueueThumb(const std::wstring& baseDir, const std::string& key,
                  bool front) {
    if (key.empty()) return false;

    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto found = g_registry.find(key);
        if (found == g_registry.end()) return false;
        it = found->second;
    }
    // Si ya está en disco, no hay nada que encolar.
    if (!RemoteThumbPath(baseDir, key).empty()) return false;

    const std::string provider = it.provider.empty() ? "unknown" : it.provider;
    ThumbQueue& q = ThumbQueueInstance();
    std::lock_guard<std::mutex> lk(q.mu);
    const auto now = std::chrono::steady_clock::now();

    // Cuarentena tras agotar los intentos: solo se reintenta si el cliente
    // vuelve a pedirlo después del cooldown.
    auto attempt = q.attempts.find(key);
    if (attempt != q.attempts.end() && attempt->second >= kThumbMaxAttempts) {
        auto failed = q.failedAt.find(key);
        if (failed != q.failedAt.end() &&
            now - failed->second <
                std::chrono::milliseconds(kThumbCooldownMs)) {
            return false;
        }
        q.attempts.erase(attempt);
        q.failedAt.erase(key);
    }

    if (q.running.count(key)) return false;
    if (q.known.count(key)) {
        if (front) {
            auto& queue = q.pending[provider];
            auto pos = std::find(queue.begin(), queue.end(), key);
            if (pos != queue.end()) {
                queue.erase(pos);
                queue.push_front(key);
            }
        }
        return true;
    }

    q.known.insert(key);
    auto& queue = q.pending[provider];
    if (front) {
        queue.push_front(key);
    } else {
        queue.push_back(key);
    }
    if (std::find(q.providers.begin(), q.providers.end(), provider) ==
        q.providers.end()) {
        q.providers.push_back(provider);
    }
    q.readyAt[key] = now;
    StartThumbWorkersLocked(q, baseDir);
    q.cv.notify_all();
    return true;
}

}  // namespace

// Genera una miniatura estática desde un póster remoto (sin recorte).
bool ConvertImageToWebp(const std::wstring& src, const std::wstring& dst) {
    std::wstring ff = FfmpegExe();
    if (ff.empty()) return false;
    std::wstring cmd =
        L"\"" + ff + L"\" -y -v error -i \"" + src +
        L"\" -frames:v 1 -vf \"scale=640:640:force_original_aspect_ratio=decrease:flags=lanczos\" "
        L"-c:v libwebp -quality 82 -an \"" + dst + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 && FileSize(dst) > 0;
}

// Pipeline de generación. Devuelve true si al final hay una miniatura local.
bool GenerateRemoteThumbJob(const std::wstring& baseDir,
                            const std::string& key) {
    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto found = g_registry.find(key);
        if (found == g_registry.end()) return true;  // item eliminado
        it = found->second;
    }
    if (!RemoteThumbPath(baseDir, key).empty()) return true;

    std::wstring dir = baseDir + L"\\res\\cache\\thumbs\\remote2\\" +
                       ToWide(it.provider);
    CreateDirectoryW((baseDir + L"\\res\\cache\\thumbs").c_str(), nullptr);
    CreateDirectoryW((baseDir + L"\\res\\cache\\thumbs\\remote2").c_str(),
                     nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring base = dir + L"\\" + SanitizeId(it.id);

    // 1) Medio ya descargado: fotograma/imagen del propio fichero.
    std::wstring local = MediaPathForCheck(baseDir, it);
    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
        GenerateThumbFromMedia(baseDir, it, local);
        if (!RemoteThumbPath(baseDir, key).empty()) return true;
    }

    const std::wstring jpg = base + L".jpg";
    auto tryPluginThumb = [&]() -> bool {
        if (FileSize(jpg) == 0) {
            static std::mutex knownMu;
            static std::vector<PluginInfo> known;
            static bool loaded = false;
            std::vector<PluginInfo> plugins;
            {
                std::lock_guard<std::mutex> lk(knownMu);
                if (!loaded) {
                    known = DiscoverPlugins(baseDir);
                    loaded = true;
                }
                plugins = known;
            }
            for (const auto& pi : plugins) {
                if (pi.id != it.provider) continue;
                std::wstring args = L"thumb --id " + ToWide(it.id) +
                                    L" --out \"" + jpg + L"\"";
                RunPluginCommand(pi, args, 120000);
                break;
            }
        }
        if (FileSize(jpg) > 0) {
            SetRegistryThumb(it.key, false, FileUrl(jpg));
            DlLog("thumb plugin %s (%lld bytes)", it.key.c_str(),
                  FileSize(jpg));
            if (!RemoteThumbPath(baseDir, key).empty()) return true;
        }
        return false;
    };

    // Si el plugin avisó de que su póster/preview está recortado, su comando
    // `thumb` genera la miniatura con la proporción real del medio.
    const bool preferPlugin = it.staticCropped || it.animatedCropped;
    bool pluginTried = false;
    if (preferPlugin) {
        pluginTried = true;
        if (tryPluginThumb()) return true;
    }

    // 2) Clip o póster publicado por el plugin, convertido a WebP.
    const std::wstring webp = base + L".thumb.webp";
    if (FileSize(webp) > 0) {
        // Si ya existe, se registra sin volver a descargar.
        SetRegistryThumb(it.key, IsRemoteHttp(it.animatedThumb), FileUrl(webp));
        if (!RemoteThumbPath(baseDir, key).empty()) return true;
    }
    if (IsRemoteHttp(it.animatedThumb)) {
        std::wstring tmp = base + L".tmp.mp4";
        if (HttpDownloadToFile(it.animatedThumb, tmp)) {
            if (ConvertToAnimatedWebp(tmp, webp)) {
                SetRegistryThumb(it.key, true, FileUrl(webp));
                DlLog("thumb clip %s (%lld bytes)", it.key.c_str(),
                      FileSize(webp));
                DeleteFileW(tmp.c_str());
                if (!RemoteThumbPath(baseDir, key).empty()) return true;
            }
        }
        DeleteFileW(tmp.c_str());
    }
    if (IsRemoteHttp(it.staticThumb) && it.staticThumb != it.animatedThumb) {
        std::wstring tmp = base + L".tmp" + ExtFromUrl(it.staticThumb);
        if (HttpDownloadToFile(it.staticThumb, tmp)) {
            if (ConvertImageToWebp(tmp, webp)) {
                SetRegistryThumb(it.key, false, FileUrl(webp));
                DlLog("thumb poster %s (%lld bytes)", it.key.c_str(),
                      FileSize(webp));
                DeleteFileW(tmp.c_str());
                if (!RemoteThumbPath(baseDir, key).empty()) return true;
            }
        }
        DeleteFileW(tmp.c_str());
    }

    // 3) Respaldo: el propio plugin (`thumb --id --out`).
    if (!pluginTried) return tryPluginThumb();
    return false;
}

// Genera (en segundo plano) la miniatura de un ítem concreto. La petición
// siempre se encola; nunca se descarta por estar lleno el pool.
void EnsureRemoteThumbAsync(const std::wstring& baseDir,
                            const std::string& key) {
    EnqueueThumb(baseDir, key, false);
}

// Empuja claves visibles al principio de su cola. No bloquea.
int PrefetchRemoteThumbsAsync(const std::wstring& baseDir,
                              const std::vector<std::string>& keys) {
    int queued = 0;
    size_t seen = 0;
    for (const auto& key : keys) {
        if (seen++ >= 200) break;
        if (EnqueueThumb(baseDir, key, true)) ++queued;
    }
    return queued;
}

// ---------- API pública para el servidor LAN (app móvil) ----------

std::wstring RemoteMediaPath(const std::wstring& baseDir,
                             const std::string& key) {
    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto o = g_registry.find(key);
        if (o == g_registry.end()) {
            // Los escritorios compuestos y las optimizaciones viven en disco
            // (res\providers\optimized\media). El registro se reconstruye en
            // cada arranque a partir de los plugins, así que estos ficheros
            // desaparecen del catálogo; se resuelven por convención para que
            // el fondo y el editor sigan funcionando tras reiniciar.
            const std::string prefix = "optimized:";
            if (key.rfind(prefix, 0) == 0) {
                std::string id = key.substr(prefix.size());
                if (!id.empty() && id.find("..") == std::string::npos &&
                    id.find('\\') == std::string::npos &&
                    id.find('/') == std::string::npos) {
                    std::wstring f = baseDir +
                                     L"\\res\\providers\\optimized\\media\\" +
                                     ToWide(id) + L".mp4";
                    if (GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        return f;
                    }
                }
            }
            return {};
        }
        it = o->second;
    }
    std::wstring p = MediaPathForCheck(baseDir, it);
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    // Un resto inválido (HTML del CDN guardado como .mp4) no es un medio: se
    // trata como "no descargado" para que el llamante lo vuelva a pedir.
    if (!MediaFileLooksValid(p)) return {};
    return p;
}

std::wstring RemoteThumbPath(const std::wstring& baseDir,
                             const std::string& key) {
    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto o = g_registry.find(key);
        if (o == g_registry.end()) return {};
        it = o->second;
    }
    // Preferir la animada local; si no, la estática local.
    const std::string& url = it.animatedThumb.rfind("http://wpx.internal/", 0) == 0
                                 ? it.animatedThumb
                                 : it.staticThumb;
    if (url.rfind("http://wpx.internal/", 0) == 0) {
        std::wstring p = LocalPathFromWpxUrl(url);
        if (!p.empty() &&
            GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return p;
        }
    }
    // Respaldo: el fichero puede existir en la ruta convencional aunque el
    // registro haya perdido la URL local al recargar una página del catálogo.
    std::wstring dir = baseDir + L"\\res\\cache\\thumbs\\remote2\\" +
                       ToWide(it.provider);
    std::wstring base = dir + L"\\" + SanitizeId(it.id);
    const wchar_t* exts[] = {L".thumb.webp", L".webp", L".jpg", L".jpeg",
                             L".png", L".avif", L".gif"};
    for (const wchar_t* ext : exts) {
        std::wstring p = base + ext;
        if (FileSize(p) > 0) return p;
    }
    return {};
}

// Descarga el medio de un ítem si aún no está en disco. Se usa desde el
// servidor LAN cuando el móvil escanea un QR de algo no descargado y desde
// "Aplicar" en el editor.
// Devuelve true si al terminar hay un fichero realmente decodificable.
bool FetchRemoteMediaSync(const std::wstring& baseDir, const std::string& key,
                          int timeoutMs) {
    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto o = g_registry.find(key);
        if (o == g_registry.end()) return false;
        it = o->second;
    }
    std::wstring out = MediaPathFor(baseDir, it);
    if (FileSize(out) > 0) {
        if (MediaFileLooksValid(out)) return true;
        // Resto de una descarga mala (HTML del CDN guardado como .mp4).
        DlLog("lan: fichero no valido, se descarta (%s)", key.c_str());
        DeleteFileW(out.c_str());
    }

    // 1) El plugin es el que sabe bajar de su proveedor: cabeceras, tokens,
    //    referer, cookies, o el preview alternativo cuando el MP4 está
    //    protegido. No se trata al plugin como una lista de enlaces.
    std::wstring viaPlugin = FetchRemoteItemKind(baseDir, it, "media");
    if (!viaPlugin.empty()) {
        if (viaPlugin != out && !CopyFileW(viaPlugin.c_str(), out.c_str(), FALSE))
            out = viaPlugin;
        if (MediaFileLooksValid(out)) {
            DlLog("lan: descargado por plugin %s (%lld bytes)", key.c_str(),
                  FileSize(out));
            return true;
        }
        DeleteFileW(viaPlugin.c_str());
    }

    // 2) Respaldo: descarga directa (proveedores sin plugin o cuyo fetch falla).
    if (IsHttpUrl(it.mediaUrl) &&
        it.mediaUrl.rfind("http://wpx.internal/", 0) != 0) {
        DlLog("lan: fetch del plugin fallido, probando descarga directa (%s)",
              key.c_str());
        if (HttpDownloadToFile(it.mediaUrl, out, timeoutMs, std::string()) &&
            MediaFileLooksValid(out)) {
            DlLog("lan: descargado %s (%lld bytes)", key.c_str(), FileSize(out));
            return true;
        }
        DeleteFileW(out.c_str());
    }
    return false;
}

// Clip de preview para la vista previa animada: el plugin lo resuelve con su
// `fetch --kind preview`; si no, se intenta el preview del catálogo por HTTP.
std::wstring FetchRemotePreviewSync(const std::wstring& baseDir,
                                    const std::string& key, int timeoutMs) {
    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto o = g_registry.find(key);
        if (o == g_registry.end()) return {};
        it = o->second;
    }
    std::wstring p = FetchRemoteItemKind(baseDir, it, "preview");
    if (!p.empty()) return p;

    const std::string& u = it.animatedThumb;
    if (!IsHttpUrl(u) || u.rfind("http://wpx.internal/", 0) == 0) return {};
    std::wstring dir = baseDir + L"\\res\\cache\\preview\\plugin";
    CreateDirectoryW((baseDir + L"\\res\\cache").c_str(), nullptr);
    CreateDirectoryW((baseDir + L"\\res\\cache\\preview").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring out = dir + L"\\" + SafeStem(it.key) + L"-clip-http.webm";
    DeleteFileW(out.c_str());
    if (HttpDownloadToFile(u, out, timeoutMs, std::string()) &&
        MediaFileLooksValid(out)) {
        return out;
    }
    DeleteFileW(out.c_str());
    return {};
}

// Metadatos de un ítem para la app móvil (SIN descargar nada).
std::string RemoteItemJson(const std::string& key) {
    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto o = g_registry.find(key);
        if (o == g_registry.end()) return {};
        it = o->second;
    }
    auto esc = [](const std::string& in) {
        std::string o;
        for (char c : in) {
            if (c == '"' || c == '\\') o.push_back('\\');
            if (c == '\n') { o += "\\n"; continue; }
            o.push_back(c);
        }
        return o;
    };
    long long size = it.sizeBytes;
    std::wstring local = MediaPathForCheck(PluginBaseDir(), it);
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    bool installed = GetFileAttributesExW(local.c_str(), GetFileExInfoStandard,
                                           &fad) != 0;
    if (installed) {
        size = ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    }
    std::string j = "{";
    j += "\"key\":\"" + esc(it.key) + "\"";
    j += ",\"title\":\"" + esc(it.title) + "\"";
    j += ",\"type\":\"" + esc(it.mediaType) + "\"";
    j += ",\"width\":" + std::to_string(it.width);
    j += ",\"height\":" + std::to_string(it.height);
    j += ",\"size\":" + std::to_string(size);
    j += ",\"installed\":";
    j += installed ? "true" : "false";
    j += ",\"thumb\":\"/api/thumb/" + UrlEncodePath(ToWide(it.key)) + "\"";
    j += ",\"url\":\"/dl/" + UrlEncodePath(ToWide(it.key)) + "\"";
    j += "}";
    return j;
}

// ------------------------------------------------------------------
// Proxy móvil 1080p: job asíncrono con progreso y cancelación
// ------------------------------------------------------------------
namespace {

struct ProxyJob {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
    bool cancel = false;
    int percent = 0;
    std::wstring path;
    std::wstring src;
    std::wstring out;
    HANDLE process = nullptr;
};

std::mutex g_proxyJobsMutex;
std::map<std::string, std::shared_ptr<ProxyJob>> g_proxyJobs;

std::wstring FfprobeExe() {
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"ffprobe.exe", nullptr, MAX_PATH, buf, nullptr)) {
        return buf;
    }
    if (GetFileAttributesW(L"C:\\Windows\\ffprobe.exe") !=
        INVALID_FILE_ATTRIBUTES) {
        return L"C:\\Windows\\ffprobe.exe";
    }
    return {};
}

// Ejecuta un proceso con stdout/stderr redirigidos a un pipe y devuelve la
// salida (para ffprobe). No muestra ventana.
std::string RunCapture(const std::wstring& cmd) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return {};
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring mcmd = cmd;
    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return {};
    }
    CloseHandle(wr);
    std::string out;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        out.append(buf, got);
    }
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return out;
}

double ProbeDurationSeconds(const std::wstring& file) {
    std::wstring ff = FfprobeExe();
    if (ff.empty()) return 0;
    std::wstring cmd = L"\"" + ff +
                       L"\" -v error -show_entries format=duration "
                       L"-of default=nw=1:nk=1 \"" + file + L"\"";
    std::string out = RunCapture(cmd);
    return atof(out.c_str());
}

void RunProxyJob(std::shared_ptr<ProxyJob> job, std::wstring ff) {
    std::wstring tmp = job->out + L".part.mp4";
    DeleteFileW(tmp.c_str());

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        std::lock_guard<std::mutex> lk(job->m);
        job->done = true;
        job->path = job->src;
        job->cv.notify_all();
        return;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + ff + L"\" -y -v error -nostats -progress pipe:1 "
                       L"-i \"" + job->src +
                       L"\" -vf \"scale=-2:1080:flags=lanczos\" "
                       L"-c:v libx264 -preset veryfast -crf 23 -pix_fmt yuv420p "
                       L"-movflags +faststart -an \"" + tmp + L"\"";
    std::wstring mcmd = cmd;
    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        std::lock_guard<std::mutex> lk(job->m);
        job->done = true;
        job->path = job->src;
        job->cv.notify_all();
        return;
    }
    CloseHandle(wr);
    {
        std::lock_guard<std::mutex> lk(job->m);
        job->process = pi.hProcess;
    }

    const double duration = ProbeDurationSeconds(job->src);
    std::string line;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        for (DWORD i = 0; i < got; ++i) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                size_t p = line.find("out_time_us=");
                if (p == std::string::npos) p = line.find("out_time_ms=");
                if (p != std::string::npos && duration > 0.0) {
                    double us = atof(line.c_str() + p + 12);
                    int pc = (int)(us / (duration * 1000000.0) * 100.0);
                    if (pc < 0) pc = 0;
                    if (pc > 99) pc = 99;
                    std::lock_guard<std::mutex> lk(job->m);
                    job->percent = pc;
                }
                line.clear();
            } else if (line.size() < 512) {
                line.push_back(c);
            }
        }
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);

    std::lock_guard<std::mutex> lk(job->m);
    job->process = nullptr;
    bool cancelled = job->cancel;
    if (!cancelled && code == 0 && FileSize(tmp) > 0) {
        DeleteFileW(job->out.c_str());
        if (MoveFileExW(tmp.c_str(), job->out.c_str(),
                        MOVEFILE_REPLACE_EXISTING)) {
            job->path = job->out;
            job->ok = true;
            job->percent = 100;
            DlLog("mobile proxy %s (%lld bytes)", ToUtf8(job->out).c_str(),
                  FileSize(job->out));
        } else {
            job->path = job->src;
            DeleteFileW(tmp.c_str());
        }
    } else {
        DeleteFileW(tmp.c_str());
        job->path = job->src;
    }
    job->done = true;
    job->cv.notify_all();
}

bool IsVideoPath(const std::wstring& path) {
    std::wstring low = path;
    for (auto& c : low) c = (wchar_t)towlower(c);
    return (low.size() > 4 && (low.rfind(L".mp4") == low.size() - 4 ||
                               low.rfind(L".mkv") == low.size() - 4 ||
                               low.rfind(L".mov") == low.size() - 4)) ||
           (low.size() > 5 && low.rfind(L".webm") == low.size() - 5);
}

std::shared_ptr<ProxyJob> FindProxyJob(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_proxyJobsMutex);
    auto it = g_proxyJobs.find(key);
    return it == g_proxyJobs.end() ? nullptr : it->second;
}

} // namespace

std::wstring MobileProxyReadyPath(const std::wstring& baseDir,
                                  const std::string& key) {
    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto o = g_registry.find(key);
        if (o == g_registry.end()) return {};
        it = o->second;
    }
    std::wstring src = MediaPathForCheck(baseDir, it);
    if (!IsVideoPath(src)) return {};
    std::wstring out = src + L".1080.mp4";
    return FileSize(out) > 0 ? out : std::wstring();
}

std::wstring EnsureMobileProxyAsync(const std::wstring& baseDir,
                                    const std::string& key, int timeoutMs) {
    RemoteItem it;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto o = g_registry.find(key);
        if (o == g_registry.end()) return {};
        it = o->second;
    }
    std::wstring src = MediaPathForCheck(baseDir, it);
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    // Un resto inválido no sirve como origen: se devuelve vacío para que el
    // llamante lo vuelva a pedir (ahora por el `fetch` del plugin).
    if (!MediaFileLooksValid(src)) return {};
    if (!IsVideoPath(src)) return src;
    std::wstring out = src + L".1080.mp4";
    if (FileSize(out) > 0) return out;

    std::shared_ptr<ProxyJob> job;
    {
        std::lock_guard<std::mutex> lock(g_proxyJobsMutex);
        auto o = g_proxyJobs.find(key);
        if (o != g_proxyJobs.end()) {
            job = o->second;
            // Un job terminado cuyo fichero desapareció (borrado manual) no
            // debe reutilizarse: se relanza.
            bool stale = false;
            {
                std::lock_guard<std::mutex> lk(job->m);
                stale = job->done && FileSize(job->out) <= 0;
            }
            if (stale) {
                g_proxyJobs.erase(o);
                job = nullptr;
            }
        }
        if (!job) {
            std::wstring ff = FfmpegExe();
            if (ff.empty()) return src;
            job = std::make_shared<ProxyJob>();
            job->src = src;
            job->out = out;
            g_proxyJobs[key] = job;
            std::thread([job, ff]() { RunProxyJob(job, ff); }).detach();
        }
    }
    std::unique_lock<std::mutex> lk(job->m);
    job->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                     [&] { return job->done; });
    return job->path.empty() ? src : job->path;
}

std::wstring EnsureMobileProxy(const std::wstring& baseDir,
                               const std::string& key) {
    return EnsureMobileProxyAsync(baseDir, key, 180000);
}

std::string MobileProxyStatusJson(const std::string& key) {
    std::shared_ptr<ProxyJob> job = FindProxyJob(key);
    if (!job) {
        return "{\"state\":\"none\",\"percent\":0}";
    }
    std::lock_guard<std::mutex> lk(job->m);
    const char* state = job->done ? (job->ok ? "done" : "error") : "running";
    std::string j = "{\"state\":\"";
    j += state;
    j += "\",\"percent\":" + std::to_string(job->percent);
    j += ",\"size\":" + std::to_string(job->path.empty() ? 0
                                                        : FileSize(job->path));
    j += ",\"path\":\"";
    // No exponer la ruta de disco al móvil; solo el estado.
    j += "\"}";
    return j;
}

bool CancelMobileProxy(const std::string& key) {
    std::shared_ptr<ProxyJob> job = FindProxyJob(key);
    if (!job) return false;
    HANDLE p = nullptr;
    {
        std::lock_guard<std::mutex> lk(job->m);
        job->cancel = true;
        p = job->process;
    }
    if (p) TerminateProcess(p, 1);
    return true;
}

int GetRemoteVersion() { return g_remoteVersion.load(); }


// ------------------------------------------------------------------
// Snapshot del catálogo
// ------------------------------------------------------------------
namespace {

std::string SnapEsc(const std::string& in) {
    std::string o;
    for (char c : in) {
        if (c == '"' || c == '\\') o.push_back('\\');
        if (c == '\n') { o += "\\n"; continue; }
        if (c == '\r') continue;
        o.push_back(c);
    }
    return o;
}

// Miniatura ya cacheada en disco para este ítem (animada o estática).
std::wstring CachedThumbUrl(const std::wstring& baseDir,
                            const RemoteItem& it) {
    std::wstring dir = baseDir + L"\\res\\cache\\thumbs\\remote2\\" +
                       ToWide(it.provider);
    std::wstring base = dir + L"\\" + SanitizeId(it.id);
    const wchar_t* exts[] = {L".thumb.webp", L".webp", L".jpg",
                             L".jpeg", L".png", L".avif", L".gif"};
    for (const wchar_t* e : exts) {
        std::wstring p = base + e;
        if (FileSize(p) > 0) return ToWide(FileUrl(p));
    }
    return {};
}

std::wstring SnapshotPath(const std::wstring& baseDir) {
    return baseDir + L"\\res\\cache\\catalog.json";
}

} // namespace

void SaveCatalogSnapshot(const std::wstring& baseDir,
                         const std::vector<RemoteItem>& items) {
    // Un solo escritor: el escaneo y el guardado periódico pueden coincidir.
    static std::mutex snapMutex;
    std::lock_guard<std::mutex> snapLock(snapMutex);
    if (items.empty()) return;
    CreateDirectoryW((baseDir + L"\\res\\cache").c_str(), nullptr);

    std::string json = "[";
    bool first = true;
    for (const auto& it : items) {
        if (!first) json.push_back(',');
        first = false;
        std::wstring localThumb = CachedThumbUrl(baseDir, it);
        std::string thumb = localThumb.empty() ? it.staticThumb
                                               : ToUtf8(localThumb);
        std::string thumbAnim = localThumb.empty() ? it.animatedThumb
                                                   : ToUtf8(localThumb);
        json += "{";
        json += "\"key\":\"" + SnapEsc(it.key) + "\"";
        json += ",\"provider\":\"" + SnapEsc(it.provider) + "\"";
        json += ",\"id\":\"" + SnapEsc(it.id) + "\"";
        json += ",\"title\":\"" + SnapEsc(it.title) + "\"";
        json += ",\"mediaType\":\"" + SnapEsc(it.mediaType) + "\"";
        json += ",\"staticThumb\":\"" + SnapEsc(thumb) + "\"";
        json += ",\"animatedThumb\":\"" + SnapEsc(thumbAnim) + "\"";
        json += ",\"staticCropped\":";
        json += it.staticCropped ? "true" : "false";
        json += ",\"animatedCropped\":";
        json += it.animatedCropped ? "true" : "false";
        json += ",\"mediaUrl\":\"" + SnapEsc(it.mediaUrl) + "\"";
        json += ",\"animated\":";
        json += it.animated ? "true" : "false";
        json += ",\"tags\":\"" + SnapEsc(it.tags) + "\"";
        json += ",\"width\":" + std::to_string(it.width);
        json += ",\"height\":" + std::to_string(it.height);
        json += ",\"publishedAt\":\"" + SnapEsc(it.publishedAt) + "\"";
        json += ",\"rating\":" + std::to_string(it.rating);
        json += ",\"sizeBytes\":" + std::to_string(it.sizeBytes);
        json += ",\"downloadable\":";
        json += it.downloadable ? "true" : "false";
        json += "}";
    }
    json.push_back(']');

    std::wstring out = SnapshotPath(baseDir);
    std::wstring tmp = out + L".part";
    FILE* f = nullptr;
    _wfopen_s(&f, tmp.c_str(), L"wb");
    if (!f) return;
    fwrite(json.data(), 1, json.size(), f);
    fclose(f);
    MoveFileExW(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
    DlLog("snapshot: %d items (%zu bytes)", (int)items.size(), json.size());
}

bool LoadCatalogSnapshot(const std::wstring& baseDir) {
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        if (!g_registry.empty()) return false;
    }
    std::string text = ReadAll(SnapshotPath(baseDir));
    if (text.empty()) return false;
    JVal root = ParseJson(text);
    if (root.type != JVal::Arr || root.arr.empty()) return false;

    size_t loaded = 0;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        for (const auto& v : root.arr) {
            if (v.type != JVal::Obj) continue;
            RemoteItem it;
            it.key = v.AsString("key");
            it.provider = v.AsString("provider");
            it.id = v.AsString("id");
            it.title = v.AsString("title");
            if (it.key.empty() || it.provider.empty() || it.id.empty()) continue;
            it.mediaType = v.AsString("mediaType", "video");
            it.staticThumb = v.AsString("staticThumb");
            it.animatedThumb = v.AsString("animatedThumb");
            it.staticCropped = v.AsBool("staticCropped", false);
            it.animatedCropped = v.AsBool("animatedCropped", false);
            it.mediaUrl = v.AsString("mediaUrl");
            it.animated = v.AsBool("animated", it.mediaType == "video");
            it.tags = v.AsString("tags");
            it.width = (int)v.AsNum("width", 0);
            it.height = (int)v.AsNum("height", 0);
            it.publishedAt = v.AsString("publishedAt");
            it.rating = v.AsNum("rating", -1.0);
            it.sizeBytes = (long long)v.AsNum("sizeBytes", 0);
            it.downloadable = v.AsBool("downloadable", true);
            std::wstring local = CachedThumbUrl(baseDir, it);
            if (!local.empty()) {
                it.staticThumb = ToUtf8(local);
                it.animatedThumb = ToUtf8(local);
            }
            g_registry[it.key] = it;
            ++loaded;
        }
    }
    if (loaded > 0) {
        BumpRemoteVersion();
        DlLog("snapshot: cargados %d items", (int)loaded);
        return true;
    }
    return false;
}

void WarmRemoteThumbs(const std::wstring& baseDir, int maxStatic,
                      int maxAnimated) {
    if (g_warmRunning.exchange(true)) return;
    std::thread([baseDir, maxStatic, maxAnimated]() {
        struct Task { std::string key, provider, id, url; bool animated; };
        std::vector<RemoteItem> items = GetRemoteRegistryCopy();
        std::vector<Task> tasks;
        int ns = 0, na = 0;
        std::wstring root = baseDir + L"\\res\\cache\\thumbs\\remote2\\";
        CreateDirectoryW((baseDir + L"\\res\\cache\\thumbs").c_str(), nullptr);
        CreateDirectoryW((baseDir + L"\\res\\cache").c_str(), nullptr);
        CreateDirectoryW(root.c_str(), nullptr);
        for (const auto& it : items) {
            std::wstring pdir = root + ToWide(it.provider);
            CreateDirectoryW(pdir.c_str(), nullptr);
            std::wstring base = pdir + L"\\" + SanitizeId(it.id);
            // Miniatura animada creada en una ejecución anterior: se registra
            // para que la ficha la muestre sin re-descargar el vídeo.
            if (FileSize(base + L".thumb.webp") > 0) {
                SetRegistryThumb(it.key, true, FileUrl(base + L".thumb.webp"));
            }
            if (IsRemoteHttp(it.staticThumb) && ns < maxStatic) {
                std::wstring target = base + ExtFromUrl(it.staticThumb);
                if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    tasks.push_back({it.key, it.provider, it.id, it.staticThumb, false});
                    ++ns;
                }
            }
            if (IsRemoteHttp(it.animatedThumb) && na < maxAnimated) {
                std::wstring target = base + L".webp";
                if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    tasks.push_back({it.key, it.provider, it.id, it.animatedThumb, true});
                    ++na;
                }
            }
        }
        if (tasks.empty()) {
            g_warmRunning = false;
            return;
        }
        std::mutex qm;
        size_t next = 0;
        auto worker = [&]() {
            for (;;) {
                Task t;
                {
                    std::lock_guard<std::mutex> lock(qm);
                    if (next >= tasks.size()) return;
                    t = tasks[next++];
                }
                std::wstring pdir = root + ToWide(t.provider);
                std::wstring base = pdir + L"\\" + SanitizeId(t.id);
                if (!t.animated) {
                    std::wstring target = base + ExtFromUrl(t.url);
                    if (HttpDownloadToFile(t.url, target)) {
                        SetRegistryThumb(t.key, false, FileUrl(target));
                    } else {
                        DeleteFileW(target.c_str());
                    }
                } else {
                    std::wstring target = base + L".webp";
                    std::wstring tmp = base + L".tmp" + ExtFromUrl(t.url);
                    if (HttpDownloadToFile(t.url, tmp)) {
                        std::wstring ext = ExtFromUrl(t.url);
                        bool ok = false;
                        if (ext == L".webp") {
                            MoveFileExW(tmp.c_str(), target.c_str(),
                                        MOVEFILE_REPLACE_EXISTING);
                            ok = FileSize(target) > 0;
                        } else {
                            ok = ConvertToAnimatedWebp(tmp, target);
                        }
                        if (ok) SetRegistryThumb(t.key, true, FileUrl(target));
                    }
                    DeleteFileW(tmp.c_str());
                }
            }
        };
        std::vector<std::thread> pool;
        for (int i = 0; i < 4; ++i) pool.emplace_back(worker);
        for (auto& th : pool) th.join();
        {
            FILE* f = nullptr;
            fopen_s(&f, "res\\dnxwallpaper.log", "a");
            if (f) {
                fprintf(f, "thumbs-remote: %d estaticas, %d animadas\n", ns, na);
                fclose(f);
            }
        }
        g_warmRunning = false;
    }).detach();
}

void SetPluginCacheTtlHours(int hours) {
    if (hours >= 0 && hours <= 24 * 30) g_cacheTtlHours = hours;
}

void PruneProviders(const std::vector<std::string>& keep) {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    for (auto it = g_registry.begin(); it != g_registry.end();) {
        bool found = false;
        for (const auto& k : keep) {
            if (it->second.provider == k) {
                found = true;
                break;
            }
        }
        if (!found) it = g_registry.erase(it);
        else ++it;
    }
}

std::vector<std::string> KnownPluginIds() {
    std::lock_guard<std::mutex> lock(g_knownMutex);
    std::vector<std::string> ids;
    for (const auto& p : g_knownPlugins) ids.push_back(p.id);
    return ids;
}

void RemoveProvider(const std::string& provider) {
    std::vector<RemoteItem> keep;
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        for (const auto& kv : g_registry) {
            if (kv.second.provider != provider) keep.push_back(kv.second);
        }
    }
    SetRemoteRegistry(keep);
}

// Barajado: cada plugin se recarga en una pagina aleatoria y se descartan sus
// items anteriores. Barajar por pagina (offset) es barato y cambia el catalogo
// entero de golpe.
// Página de un plugin sin tocar el catálogo (para pedir varias en paralelo).
std::vector<RemoteItem> PluginPageItems(const std::wstring& baseDir,
                                        const std::string& pluginId, int page,
                                        int limit) {
    std::vector<RemoteItem> out;
    PluginInfo target;
    {
        std::lock_guard<std::mutex> lock(g_knownMutex);
        for (const auto& p : g_knownPlugins) {
            if (p.id == pluginId) { target = p; break; }
        }
    }
    if (target.id.empty()) return out;
    std::wstring args = L"list --limit " + std::to_wstring(limit) +
                        L" --page " + std::to_wstring(page > 0 ? page : 1) +
                        L" --sort newest";
    std::string json = RunPluginCommand(target, args, 25000);
    if (json.empty()) return out;
    const JVal root = ParseJson(json);
    const JVal* items = root.Get("items");
    if (!items || items->type != JVal::Arr) return out;
    for (const auto& it : items->arr) {
        if (it.type != JVal::Obj) continue;
        RemoteItem r;
        r.provider = target.id;
        r.id = it.AsString("id");
        if (r.id.empty()) continue;
        r.key = r.provider + ":" + r.id;
        r.title = it.AsString("title", r.id);
        r.mediaType = it.AsString("mediaType", "video");
        r.animated = it.AsBool("animated", r.mediaType == "video");
        r.tags = CollectTags(it);
        r.width = (int)it.AsNum("width", 0);
        r.height = (int)it.AsNum("height", 0);
        const JVal* res = it.Get("resolution");
        if (res && res->type == JVal::Obj) {
            if (r.width <= 0) r.width = (int)res->AsNum("width", 0);
            if (r.height <= 0) r.height = (int)res->AsNum("height", 0);
        }
        r.publishedAt = it.AsString("publishedAt");
        r.downloadable = it.AsBool("downloadable", true);
        const JVal* media = it.Get("media");
        if (media && media->type == JVal::Obj) r.mediaUrl = media->AsString("url");
        const JVal* thumbs = it.Get("thumb");
        if (!thumbs) thumbs = it.Get("thumbs");
        if (thumbs && thumbs->type == JVal::Obj) {
            r.animatedThumb = thumbs->AsString("animatedUrl");
            r.staticThumb = thumbs->AsString("staticUrl");
            r.staticCropped = thumbs->AsBool("staticCropped", false);
            r.animatedCropped = thumbs->AsBool("animatedCropped", false);
            if (r.staticThumb.empty()) r.staticThumb = r.animatedThumb;
        }
        const JVal* rating = it.Get("rating");
        if (rating && rating->type == JVal::Obj) {
            r.rating = rating->AsNum("average", -1.0);
        }
        if (r.mediaUrl.empty() && r.staticThumb.empty()) continue;
        out.push_back(std::move(r));
    }
    return out;
}

std::string RandomizeLibraryJson(const std::wstring& baseDir, int perPlugin,
                                 int maxPage) {
    if (maxPage < 2) maxPage = 2;
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(2, maxPage);
    const auto ids = KnownPluginIds();
    // Se piden las páginas en paralelo (antes eran 10 procesos en serie, ~26 s)
    // y después se reemplazan los items de cada proveedor de una vez.
    std::vector<int> pages(ids.size(), 2);
    for (auto& p : pages) p = dist(rng);
    std::vector<std::vector<RemoteItem>> fetched(ids.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < ids.size(); ++i) {
        threads.emplace_back([&, i]() {
            fetched[i] = PluginPageItems(baseDir, ids[i], pages[i], perPlugin);
        });
    }
    for (auto& t : threads) t.join();
    // No se borra nada: el barajado sólo aporta una página distinta de cada
    // plugin y se mezcla con lo que ya hay (antes vaciaba el catálogo entero).
    std::vector<RemoteItem> mixed;
    for (const auto& v : fetched) {
        MergeRemoteItems(v);
        for (const auto& it : v) mixed.push_back(it);
    }
    SaveCatalogSnapshot(baseDir, GetRemoteRegistryCopy());
    return BuildRemoteItemsJson(mixed);
}
