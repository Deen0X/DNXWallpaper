// lan_server.cpp — Servidor HTTP mínimo para la app móvil (LAN).
//
// Sirve el catálogo ya cargado por plugin_host:
//   GET /api/info                 -> datos del PC y del servicio
//   GET /api/library?offset&limit -> catálogo (JSON)
//   GET /api/thumb/<key>          -> miniatura WebP
//   GET /dl/<key>                 -> fichero ORIGINAL (mp4/jpg/webm) con Range
//
// Pensado para el QR: http://<ip>:<puerto>/dl/<key>
// No expone rutas de disco: sólo resuelve "key" contra el catálogo.
#include "lan_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <cctype>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "../cef/plugin_host.h"
#include "../editor/editor_export.h"

// Log del host de plugins (res\dnxwallpaper.log), sin prefijo propio.
extern void DlLog(const char* fmt, ...);

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace {

int g_port = 8477;
std::atomic<bool> g_enabled{false};
std::string g_token;
std::wstring g_baseDir;
std::atomic<bool> g_started{false};

// Firma del producto: permite distinguir "es nuestro servidor" de "hay otro
// servicio escuchando en ese puerto" al elegir puerto o al emparejar.
constexpr const char* kProduct = "dnxwallpaper";
std::string g_instanceId;   // GUID estable, persistido en server.json

std::string WideToUtf8(const std::wstring& w);  // definido más abajo

// Lee un fichero completo (vacío si no existe).
std::string ReadWholeFile(const std::wstring& path) {
    std::string out;
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return out;
    char buf[8192];
    size_t got = 0;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    fclose(f);
    return out;
}

void WriteWholeFile(const std::wstring& path, const std::string& data) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"wb");
    if (!f) return;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

// Interfaz de red util para emparejar un movil: IPv4, activa y con puerta de
// enlace. Las virtuales (VPN, WSL, Docker) se marcan para que la UI pueda
// avisar, pero se pueden seleccionar.
struct NetIface {
    std::string name;
    std::string ip;
    std::string gateway;
    DWORD index = 0;
    bool isDefault = false;
    bool virtualAdapter = false;
};

bool LooksVirtual(const std::string& name, DWORD ifType) {
    if (ifType == IF_TYPE_PPP || ifType == IF_TYPE_TUNNEL) return true;
    std::string n;
    for (char c : name) n.push_back((char)tolower((unsigned char)c));
    static const char* marks[] = {"vethernet", "wsl", "docker", "vpn",
                                  "wireguard", "tailscale", "zerotier",
                                  "hyper-v", "virtualbox", "vmware", "tap-",
                                  "tun", "loopback"};
    for (const char* m : marks) {
        if (n.find(m) != std::string::npos) return true;
    }
    return false;
}

std::vector<NetIface> CollectInterfaces() {
    std::vector<NetIface> out;
    ULONG size = 16000;
    std::vector<unsigned char> buf(size);
    auto* addrs = (PIP_ADAPTER_ADDRESSES)buf.data();
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size) != NO_ERROR) {
        return out;
    }

    // Interfaz por la que sale el tráfico a internet (referencia de "la buena").
    DWORD best = 0;
    {
        DWORD dst = inet_addr("8.8.8.8");
        if (GetBestInterface(dst, &best) != NO_ERROR) best = 0;
    }

    for (auto* a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        NetIface ni;
        ni.name = WideToUtf8(a->FriendlyName ? a->FriendlyName : L"");
        ni.index = a->IfIndex;
        ni.isDefault = (best != 0 && a->IfIndex == best);

        for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
            auto* sa = (sockaddr_in*)g->Address.lpSockaddr;
            if (!sa || sa->sin_family != AF_INET) continue;
            char t[64] = {};
            inet_ntop(AF_INET, &sa->sin_addr, t, sizeof(t));
            ni.gateway = t;
            break;
        }
        if (ni.gateway.empty()) continue;  // sin puerta de enlace: no sirve

        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sa = (sockaddr_in*)u->Address.lpSockaddr;
            if (!sa || sa->sin_family != AF_INET) continue;
            char t[64] = {};
            inet_ntop(AF_INET, &sa->sin_addr, t, sizeof(t));
            std::string cand = t;
            if (cand.rfind("169.254.", 0) == 0) continue;
            if (cand.rfind("127.", 0) == 0) continue;
            ni.ip = cand;
            break;
        }
        if (ni.ip.empty()) continue;

        ni.virtualAdapter = LooksVirtual(ni.name, a->IfType);
        out.push_back(ni);
    }
    return out;
}

// Aplica un item del catalogo. Equivale a
// browseWallpaperObject.selectWallpaper(): deja res\current.json y avisa al
// motor por el evento dnxWallpaperV3Reload.
bool ApplyCatalogItem(const std::wstring& baseDir, const std::string& key,
                      std::string* err) {
    const RemoteItem* ri = FindRemoteItem(key);
    if (!ri) {
        if (err) *err = "item no encontrado";
        return false;
    }
    std::wstring file = RemoteMediaPath(baseDir, key);
    if (file.empty() || GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (!FetchRemoteMediaSync(baseDir, key, 180000)) {
            if (err) *err = "no se pudo descargar el medio";
            return false;
        }
        file = RemoteMediaPath(baseDir, key);
    }
    if (file.empty()) {
        if (err) *err = "sin fichero";
        return false;
    }

    std::string pathUtf8 = WideToUtf8(file);
    std::string esc;
    for (char c : pathUtf8) {
        if (c == '\\' || c == '"') esc.push_back('\\');
        esc.push_back(c);
    }
    FILE* f = nullptr;
    _wfopen_s(&f, (baseDir + L"\\res\\current.json").c_str(), L"w, ccs=UTF-8");
    if (!f) {
        if (err) *err = "no se pudo guardar la seleccion";
        return false;
    }
    fwprintf(f, L"{\"project\": \"%hs\"}", esc.c_str());
    fclose(f);

    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, L"dnxWallpaperV3Reload");
    if (ev) {
        SetEvent(ev);
        CloseHandle(ev);
    }
    return true;
}

std::string MonitorsJson() {
    struct Ctx {
        std::string j;
        int n = 0;
    } ctx;
    ctx.j = "[";
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM l) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(l);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(h, &mi)) return TRUE;
            char buf[320];
            sprintf_s(buf,
                      "%s{\"index\":%d,\"x\":%ld,\"y\":%ld,"
                      "\"width\":%ld,\"height\":%ld,\"primary\":%s}",
                      c->n ? "," : "", c->n, mi.rcMonitor.left, mi.rcMonitor.top,
                      mi.rcMonitor.right - mi.rcMonitor.left,
                      mi.rcMonitor.bottom - mi.rcMonitor.top,
                      (mi.dwFlags & MONITORINFOF_PRIMARY) ? "true" : "false");
            c->j += buf;
            ++c->n;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    ctx.j += "]";
    return ctx.j;
}

std::string InterfacesJson() {
    std::string out = "[";
    bool first = true;
    for (const auto& i : CollectInterfaces()) {
        if (!first) out += ",";
        first = false;
        char b[512];
        sprintf_s(b,
                  "{\"name\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\","
                  "\"default\":%s,\"virtual\":%s,\"url\":\"http://%s:%d\"}",
                  i.name.c_str(), i.ip.c_str(), i.gateway.c_str(),
                  i.isDefault ? "true" : "false",
                  i.virtualAdapter ? "true" : "false", i.ip.c_str(), g_port);
        out += b;
    }
    out += "]";
    return out;
}

std::string NewGuidString() {
    GUID g{};
    if (CoCreateGuid(&g) != S_OK) return "00000000";
    wchar_t b[64] = {};
    StringFromGUID2(g, b, 64);
    std::wstring w(b);
    if (w.size() > 2 && w.front() == L'{' && w.back() == L'}') {
        w = w.substr(1, w.size() - 2);
    }
    return WideToUtf8(w);
}

// Listener con puerto exclusivo (en Windows SO_REUSEADDR permitiria que dos
// procesos escuchasen el mismo puerto: aqui queremos lo contrario).
SOCKET TryListenOn(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&yes,
               sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    if (listen(s, 8) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

// Campo de texto de un JSON plano: {"key":"valor", ...}
std::string JsonField(const std::string& j, const char* key) {
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

// Fija (o añade) un campo de texto en un JSON plano.
std::string JsonSetField(const std::string& j, const char* key,
                         const std::string& value) {
    std::string v;
    for (char c : value) {
        if (c == '\\' || c == '"') v.push_back('\\');
        v.push_back(c);
    }
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) {
        size_t end = j.find_last_of('}');
        if (end == std::string::npos) return j;
        std::string ins = k + ":\"" + v + "\",";
        return j.substr(0, end) + ins + j.substr(end);
    }
    size_t colon = j.find(':', p);
    if (colon == std::string::npos) return j;
    size_t q1 = j.find('"', colon);
    if (q1 == std::string::npos) return j;
    size_t q2 = q1 + 1;
    while (q2 < j.size()) {
        if (j[q2] == '\\') {
            q2 += 2;
            continue;
        }
        if (j[q2] == '"') break;
        ++q2;
    }
    return j.substr(0, q1 + 1) + v + j.substr(q2);
}

std::string ReadConfigStr(const std::string& json, const char* key) {    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return {};
    p = json.find(':', p);
    if (p == std::string::npos) return {};
    p = json.find('"', p);
    if (p == std::string::npos) return {};
    size_t e = json.find('"', p + 1);
    if (e == std::string::npos) return {};
    return json.substr(p + 1, e - p - 1);
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr,
                                nullptr);
    std::string out((size_t)(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr,
                            nullptr);
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

std::string UrlDecode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out.push_back((char)strtol(hex, nullptr, 16));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

const char* MimeFor(const std::wstring& path) {
    std::wstring low = path;
    for (auto& c : low) c = (wchar_t)towlower(c);
    auto ends = [&low](const wchar_t* e) {
        size_t n = wcslen(e);
        return low.size() >= n && low.rfind(e) == low.size() - n;
    };
    if (ends(L".mp4")) return "video/mp4";
    if (ends(L".webm")) return "video/webm";
    if (ends(L".mkv")) return "video/x-matroska";
    if (ends(L".mov")) return "video/quicktime";
    if (ends(L".webp")) return "image/webp";
    if (ends(L".png")) return "image/png";
    if (ends(L".gif")) return "image/gif";
    if (ends(L".js")) return "application/javascript; charset=utf-8";
    if (ends(L".css")) return "text/css; charset=utf-8";
    if (ends(L".html")) return "text/html; charset=utf-8";
    if (ends(L".svg")) return "image/svg+xml";
    if (ends(L".json")) return "application/json; charset=utf-8";
    return "image/jpeg";
}

void SendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, (int)(len - sent), 0);
        if (n <= 0) return;
        sent += (size_t)n;
    }
}

void SendText(SOCKET s, int code, const char* status, const std::string& body,
              const char* type = "application/json; charset=utf-8") {
    char head[512];
    sprintf_s(head,
              "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
              "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
              code, status, type, body.size());
    std::string all = std::string(head) + body;
    SendAll(s, all.data(), all.size());
}

bool SendFile(SOCKET s, const std::wstring& path, const std::string& range,
              bool attachment, bool headOnly = false) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    long long total = size.QuadPart;
    long long start = 0, end = total - 1;
    bool partial = false;
    if (!range.empty() && range.rfind("bytes=", 0) == 0) {
        std::string r = range.substr(6);
        size_t dash = r.find('-');
        if (dash != std::string::npos) {
            if (dash > 0) start = _strtoi64(r.substr(0, dash).c_str(), nullptr, 10);
            if (dash + 1 < r.size()) {
                end = _strtoi64(r.substr(dash + 1).c_str(), nullptr, 10);
            }
            if (end >= total) end = total - 1;
            partial = true;
        }
    }
    long long len = end - start + 1;
    std::string name = WideToUtf8(path);
    size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name = name.substr(slash + 1);

    char head[1024];
    sprintf_s(head,
              "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lld\r\n"
              "Accept-Ranges: bytes\r\nContent-Range: bytes %lld-%lld/%lld\r\n"
              "Content-Disposition: %s; filename=\"%s\"\r\n"
              "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
              partial ? 206 : 200, partial ? "Partial Content" : "OK",
              MimeFor(path), len, start, end, total,
              attachment ? "attachment" : "inline", name.c_str());
    SendAll(s, head, strlen(head));

    if (headOnly) {
        CloseHandle(h);
        return true;
    }
    LARGE_INTEGER pos{};
    pos.QuadPart = start;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    char buf[64 * 1024];
    long long left = len;
    while (left > 0) {
        DWORD want = (DWORD)(left > (long long)sizeof(buf) ? sizeof(buf) : left);
        DWORD got = 0;
        if (!ReadFile(h, buf, want, &got, nullptr) || got == 0) break;
        SendAll(s, buf, got);
        left -= got;
    }
    CloseHandle(h);
    return true;
}


// ---------------------------------------------------------------- subidas
// Vídeos/imágenes que el móvil envía para editar en el PC.
std::mutex g_upMu;
std::map<std::string, std::wstring> g_uploads;

std::string NewUploadId() {
    static std::atomic<int> seq{0};
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char b[64];
    sprintf_s(b, "up%02d%02d%02d%02d%02d%02d%03d", st.wYear % 100, st.wMonth,
              st.wDay, st.wHour, st.wMinute, st.wSecond, seq++ % 1000);
    return b;
}

long long HeaderContentLength(const std::string& req) {
    std::string lower = req;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    size_t p = lower.find("content-length:");
    if (p == std::string::npos) return -1;
    size_t e = lower.find("\r\n", p);
    return _strtoi64(req.substr(p + 15, e - p - 15).c_str(), nullptr, 10);
}

std::string HeaderValue(const std::string& req, const char* name) {
    std::string lower = req;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    std::string k = std::string(name) + ":";
    size_t p = lower.find(k);
    if (p == std::string::npos) return {};
    size_t b = p + k.size();
    while (b < req.size() && (req[b] == ' ' || req[b] == '\t')) ++b;
    size_t e = req.find("\r\n", b);
    return req.substr(b, e == std::string::npos ? std::string::npos : e - b);
}

// URL interna para servir un fichero local (mismo formato que plugin_host).
std::string WpxFileUrl(const std::wstring& path) {
    std::string utf8 = WideToUtf8(path);
    std::string out = "http://wpx.internal/__file/";
    char hex[5] = {};
    for (unsigned char c : utf8) {
        bool plain = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                     (c >= 'A' && c <= 'Z') || c == '/' || c == ':' ||
                     c == '\\' || c == '.' || c == '-' || c == '_' ||
                     c == '~';
        if (plain) {
            out.push_back((char)c);
        } else {
            sprintf_s(hex, "%%%02X", (unsigned int)c);
            out += hex;
        }
    }
    return out;
}

// Convierte una subida en un item del catalogo (provider "uploads").
// A partir de aqui funciona todo lo demas: /api/library, /dl, /api/thumb,
// /api/apply, proxy movil y export.
void RegisterUploadItem(const std::wstring& baseDir, const std::string& id,
                        const std::wstring& media, const std::wstring& poster,
                        const std::string& title, int w, int h) {
    RemoteItem it;
    it.provider = "uploads";
    it.id = id;
    it.key = "uploads:" + id;
    it.title = title.empty() ? id : title;
    it.mediaType = "video";
    it.animated = true;
    it.mediaUrl = WpxFileUrl(media);
    it.width = w;
    it.height = h;
    it.downloadable = true;
    it.tags = "subido";

    SYSTEMTIME st{};
    GetSystemTime(&st);
    char ts[40];
    sprintf_s(ts, "%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth,
              st.wDay, st.wHour, st.wMinute, st.wSecond);
    it.publishedAt = ts;

    if (!poster.empty() &&
        GetFileAttributesW(poster.c_str()) != INVALID_FILE_ATTRIBUTES) {
        it.staticThumb = WpxFileUrl(poster);
    }

    MergeRemoteItems(std::vector<RemoteItem>{it});
    SaveCatalogSnapshot(baseDir, GetRemoteRegistryCopy());
}

std::wstring UploadPath(const std::string& id) {
    std::lock_guard<std::mutex> lk(g_upMu);
    auto it = g_uploads.find(id);
    return it == g_uploads.end() ? std::wstring() : it->second;
}

void HandleUpload(SOCKET client, const std::string& req,
                  const std::string& query) {
    const long long want = HeaderContentLength(req);
    if (want <= 0 || want > 400LL * 1024 * 1024) {
        SendText(client, 413, "Payload Too Large", "{\"ok\":false,\"error\":\"size\"}");
        closesocket(client);
        return;
    }
    std::string name = HeaderValue(req, "x-filename");
    std::string ext = ".mp4";
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        std::string e = name.substr(dot);
        for (auto& c : e) c = (char)tolower((unsigned char)c);
        if (e == ".mp4" || e == ".mov" || e == ".webm" || e == ".mkv" ||
            e == ".jpg" || e == ".jpeg" || e == ".png" || e == ".webp") {
            ext = e;
        }
    }

    std::wstring dir = g_baseDir + L"\\res\\uploads";
    CreateDirectoryW((g_baseDir + L"\\res").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::string id = NewUploadId();
    const std::wstring file = dir + L"\\" + Utf8ToWide(id) + Utf8ToWide(ext);

    FILE* f = nullptr;
    _wfopen_s(&f, file.c_str(), L"wb");
    if (!f) {
        SendText(client, 500, "Error", "{\"ok\":false,\"error\":\"file\"}");
        closesocket(client);
        return;
    }

    size_t hdrEnd = req.find("\r\n\r\n");
    size_t start = (hdrEnd == std::string::npos) ? req.size() : hdrEnd + 4;
    long long written = 0;
    if (start < req.size()) {
        size_t n = req.size() - start;
        if ((long long)n > want) n = (size_t)want;
        fwrite(req.data() + start, 1, n, f);
        written = (long long)n;
    }
    std::vector<char> buf(256 * 1024);
    while (written < want) {
        int got = recv(client, buf.data(), (int)buf.size(), 0);
        if (got <= 0) break;
        fwrite(buf.data(), 1, (size_t)got, f);
        written += got;
    }
    fclose(f);

    if (written != want) {
        DeleteFileW(file.c_str());
        SendText(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"incomplete\"}");
        closesocket(client);
        return;
    }

    int w = 0, h = 0;
    double dur = 0;
    std::wstring poster;
    if (!EditorProbeFile(file, poster, w, h, dur)) {
        DeleteFileW(file.c_str());
        SendText(client, 200, "OK", "{\"ok\":false,\"error\":\"probe\"}");
        closesocket(client);
        return;
    }

    // persist=1 -> el video entra en el catalogo del servidor (provider
    // "uploads") y queda disponible para cualquier cliente. Cualquier otro
    // valor (o ausente) mantiene el comportamiento efimero: se usa para
    // exportar y no se conserva.
    const bool persist = query.find("persist=1") != std::string::npos;
    std::string key = "up:" + id;

    std::string title = name;
    size_t bd = title.find_last_of('.');
    if (bd != std::string::npos) title = title.substr(0, bd);

    if (persist) {
        std::wstring provDir = g_baseDir + L"\\res\\providers\\uploads";
        std::wstring mediaDir = provDir + L"\\media";
        CreateDirectoryW((g_baseDir + L"\\res\\providers").c_str(), nullptr);
        CreateDirectoryW(provDir.c_str(), nullptr);
        CreateDirectoryW(mediaDir.c_str(), nullptr);

        std::wstring dest = mediaDir + L"\\" + Utf8ToWide(id) + Utf8ToWide(ext);
        if (MoveFileExW(file.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            std::wstring thumbDir =
                g_baseDir + L"\\res\\cache\\thumbs\\remote\\uploads";
            CreateDirectoryW(
                (g_baseDir + L"\\res\\cache\\thumbs\\remote").c_str(),
                nullptr);
            CreateDirectoryW(thumbDir.c_str(), nullptr);
            std::wstring thumb = thumbDir + L"\\" + Utf8ToWide(id) + L".jpg";
            if (!poster.empty() &&
                GetFileAttributesW(poster.c_str()) != INVALID_FILE_ATTRIBUTES) {
                CopyFileW(poster.c_str(), thumb.c_str(), FALSE);
            } else {
                thumb.clear();
            }
            RegisterUploadItem(g_baseDir, id, dest, thumb, title, w, h);
            key = "uploads:" + id;
            std::lock_guard<std::mutex> lk(g_upMu);
            g_uploads[id] = dest;
        } else {
            std::lock_guard<std::mutex> lk(g_upMu);
            g_uploads[id] = file;
        }
    } else {
        std::lock_guard<std::mutex> lk(g_upMu);
        g_uploads[id] = file;
    }

    char body[640];
    sprintf_s(body,
              "{\"ok\":true,\"id\":\"%s\",\"key\":\"%s\","
              "\"persist\":%s,\"width\":%d,\"height\":%d,"
              "\"duration\":%.2f}",
              id.c_str(), key.c_str(), persist ? "true" : "false", w, h, dur);
    SendText(client, 200, "OK", body);
    closesocket(client);
}

// ---- activar/desactivar plugins ------------------------------------------
std::string JsonEsc(const std::string& in);  // definido más abajo

void SetPluginDisabled(const std::string& id, bool disable) {
    const std::wstring f = g_baseDir + L"\\res\\config\\plugins.json";
    std::string j = ReadWholeFile(f);
    if (j.empty()) j = "{}";
    std::vector<std::string> ids;
    size_t dp = j.find("\"disabled\"");
    size_t open = (dp == std::string::npos) ? std::string::npos : j.find('[', dp);
    size_t close = (open == std::string::npos) ? std::string::npos : j.find(']', open);
    if (open != std::string::npos && close != std::string::npos) {
        size_t i = open;
        while ((i = j.find('"', i + 1)) != std::string::npos && i < close) {
            size_t e = j.find('"', i + 1);
            if (e == std::string::npos || e > close) break;
            ids.push_back(j.substr(i + 1, e - i - 1));
            i = e;
        }
    }
    auto it = std::find(ids.begin(), ids.end(), id);
    if (disable && it == ids.end()) ids.push_back(id);
    if (!disable && it != ids.end()) ids.erase(it);

    std::string arr = "[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) arr += ",";
        arr += "\"" + JsonEsc(ids[i]) + "\"";
    }
    arr += "]";

    if (open != std::string::npos && close != std::string::npos) {
        j = j.substr(0, open) + arr + j.substr(close + 1);
    } else {
        size_t brace = j.find_last_of('}');
        if (brace == std::string::npos) return;
        std::string ins = "\"disabled\":" + arr + ",";
        j = j.substr(0, brace) + ins + j.substr(brace);
    }
    WriteWholeFile(f, j);
}

// ---- composición del escritorio virtual -----------------------------------
// El motor pinta una única superficie que cubre todos los monitores. Para que
// "individual"/"clonar"/"panorámico" se vean bien sin tocar el motor, se compone
// un vídeo del tamaño del escritorio virtual con cada monitor en su sitio.
//
// Cuerpo:
//   { canvasW, canvasH, maxSizeMB, black?, panorama?, cells:[...] }
//   panorama: { key, fit, cx, cy, cw, ch }   (un vídeo repartido)
//   cells:    [{ key, x, y, w, h, cx, cy, cw, ch, fit }]
struct ComposeCell {
    std::wstring file;
    std::wstring inputFile;                 // file o la celda ya preparada
    bool cached = false;
    int x = 0, y = 0, w = 1920, h = 1080;
    double cx = 0, cy = 0, cw = 1, ch = 1;  // recorte normalizado del origen
    double aspect = 16.0 / 9.0;             // proporción real del origen
    std::string fit = "expand";
    std::string key;                        // clave del catálogo (para re-bajar)
};

double JsonNumIn(const std::string& j, size_t from, const char* key, double def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return def;
    p = j.find(':', p);
    if (p == std::string::npos) return def;
    return atof(j.c_str() + p + 1);
}

std::string JsonStrIn(const std::string& j, size_t from, const char* key,
                      const std::string& def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return def;
    size_t a = j.find('"', j.find(':', p));
    if (a == std::string::npos) return def;
    size_t b = j.find('"', a + 1);
    if (b == std::string::npos) return def;
    return j.substr(a + 1, b - a - 1);
}

// true/false de un objeto JSON (tolera espacio tras ':'; el navegador manda
// JSON.stringify sin espacios, pero python y otros añaden uno).
bool JsonBoolIn(const std::string& j, size_t from, const char* key, bool def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return def;
    p = j.find(':', p);
    if (p == std::string::npos) return def;
    ++p;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
    return j.compare(p, 4, "true") == 0;
}

std::wstring ComposeResolveFile(const std::string& key) {
    std::wstring file = RemoteMediaPath(g_baseDir, key);
    if (file.empty()) {
        // El medio puede no estar descargado (el catálogo sólo trae la
        // miniatura) o ser un resto inválido: se baja ahora con el mecanismo
        // del plugin (su `fetch`), no con una descarga directa a ciegas.
        if (FetchRemoteMediaSync(g_baseDir, key, 240000)) {
            file = RemoteMediaPath(g_baseDir, key);
        }
    }
    if (file.empty()) {
        // Último recurso para el móvil: el proxy 1080p ya generado.
        std::wstring proxy = EnsureMobileProxy(g_baseDir, key);
        if (!proxy.empty()) file = proxy;
    }
    return file;
}

// Cadena de filtros que lleva el origen a la caja (w x h) según el ajuste.
// "expand" replica lo que se ve en el editor: se recorta la región encuadrada
// (`rect`, puede salirse del borde → bandas negras) y se escala de forma
// uniforme para cubrir la caja. "fill" estira y "center" encaja sin recortar.
// Devuelve vacío si la región queda completamente fuera.
std::string ComposeCellChain(const ComposeCell& c, const std::string& src) {
    char buf[1024];
    if (c.fit != "expand") {
        if (c.fit == "fill") {
            sprintf_s(buf, "%sscale=%d:%d,setsar=1", src.c_str(), c.w, c.h);
        } else {
            sprintf_s(buf,
                      "%sscale=%d:%d:force_original_aspect_ratio=decrease,"
                      "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:black,setsar=1",
                      src.c_str(), c.w, c.h, c.w, c.h);
        }
        return buf;
    }

    // 1) Región visible V dentro de la región encuadrada R: la que cubre la
    //    caja sin deformar (lo que el editor muestra con "Expandir").
    const double cw = c.cw > 0.0001 ? c.cw : 1.0;
    const double ch = c.ch > 0.0001 ? c.ch : 1.0;
    const double aR = (cw / ch) * (c.aspect > 0.01 ? c.aspect : 16.0 / 9.0);
    const double aC = (double)c.w / (double)c.h;
    double vw = cw, vh = ch;
    if (aR > aC) vw = cw * (aC / aR);
    else vh = ch * (aR / aC);
    const double vcx = c.cx + cw / 2.0;
    const double vcy = c.cy + ch / 2.0;
    const double vx = vcx - vw / 2.0;
    const double vy = vcy - vh / 2.0;

    // 2) Parte de V que cae dentro del origen (el resto son bandas negras).
    const double x0 = vx > 0.0 ? vx : 0.0;
    const double y0 = vy > 0.0 ? vy : 0.0;
    const double x1 = (vx + vw) < 1.0 ? (vx + vw) : 1.0;
    const double y1 = (vy + vh) < 1.0 ? (vy + vh) : 1.0;
    if (x1 - x0 < 0.0005 || y1 - y0 < 0.0005) return "";

    // 3) Escala uniforme: V (normalizada) ocupa la caja.
    const double sx = c.w / vw;
    const double sy = c.h / vh;
    int outW = (int)((x1 - x0) * sx + 0.5);
    int outH = (int)((y1 - y0) * sy + 0.5);
    int offX = (int)((x0 - vx) * sx + 0.5);
    int offY = (int)((y0 - vy) * sy + 0.5);
    if (outW % 2) --outW;
    if (outH % 2) --outH;
    if (outW < 2) outW = 2;
    if (outH < 2) outH = 2;
    if (offX < 0) offX = 0;
    if (offY < 0) offY = 0;
    if (offX + outW > c.w) offX = c.w - outW;
    if (offY + outH > c.h) offY = c.h - outH;
    if (offX < 0) offX = 0;
    if (offY < 0) offY = 0;

    sprintf_s(buf,
              "%scrop=w=trunc(iw*%.6f/2)*2:h=trunc(ih*%.6f/2)*2:"
              "x=trunc(iw*%.6f/2)*2:y=trunc(ih*%.6f/2)*2,"
              "scale=%d:%d,pad=%d:%d:%d:%d:black,setsar=1",
              src.c_str(), x1 - x0, y1 - y0, x0, y0, outW, outH, c.w, c.h,
              offX, offY);
    return buf;
}

// ---- caché de celdas y cola de preparación --------------------------------
// Al asignar un wallpaper se prepara en segundo plano su celda (recorte y
// escala al tamaño del monitor). Así "Aplicar" sólo superpone celdas ya listas
// en vez de decodificar los 4K originales, que es lo que tarda.
std::wstring CellCacheDir() {
    const std::wstring dir = g_baseDir + L"\\res\\cache\\cells";
    CreateDirectoryW((g_baseDir + L"\\res\\cache").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::string CellHash(const ComposeCell& c) {
    char p[512];
    sprintf_s(p, "%ls|%hs|%.4f|%.4f|%.4f|%.4f|%d|%d|%.4f", c.file.c_str(),
              c.fit.c_str(), c.cx, c.cy, c.cw, c.ch, c.w, c.h, c.aspect);
    unsigned long long h = 1469598103934665603ULL;
    for (const char* s = p; *s; ++s) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    char b[32];
    sprintf_s(b, "%016llx", h);
    return b;
}

std::wstring CellCachePath(const std::string& hash) {
    return CellCacheDir() + L"\\" + Utf8ToWide(hash) + L".mp4";
}

// Cadena que prepara la celda a partir del origen (misma que usa el compuesto
// cuando no hay caché).
std::string CellPrepFilter(const ComposeCell& c) {
    return ComposeCellChain(c, "[0:v]");
}


// El editor de pantallas muestra varios wallpapers a la vez y la UI va sin GPU.
// Un WebP animado de 480 px se decodifica como imagen (coste mínimo), se repite
// solo y no pasa por el decodificador de vídeo, que sólo atiende a uno o dos.
// ---- progreso de ffmpeg y cola de preparación ------------------------------
// Las celdas se preparan en segundo plano al asignar el wallpaper; "Aplicar"
// reutiliza lo ya preparado y, mientras trabaja, la UI puede leer el avance.

struct ProcSlot {
    std::mutex m;
    HANDLE proc = nullptr;
};

struct ProgressState {
    std::mutex m;
    bool active = false;
    int percent = -1;
    std::string stage;
};

ProgressState g_prog;
std::atomic<int> g_compPct{-1};  // avance de la composición en curso
// Un solo ffmpeg pesado a la vez: dos decodificaciones 4K simultáneas agotan
// la memoria del equipo y tumban el proceso de la interfaz.
std::mutex g_encodeMutex;

void SetProgress(bool active, int pct, const std::string& stage) {
    std::lock_guard<std::mutex> lk(g_prog.m);
    g_prog.active = active;
    g_prog.percent = pct;
    g_prog.stage = stage;
}

std::string ProgressJson() {
    std::lock_guard<std::mutex> lk(g_prog.m);
    int pct = g_prog.percent;
    const int cp = g_compPct.load();
    if (cp >= 0) pct = cp;
    char b[256];
    sprintf_s(b, "{\"active\":%s,\"percent\":%d,\"stage\":\"%hs\"}",
              g_prog.active ? "true" : "false", pct, g_prog.stage.c_str());
    return b;
}

// Ejecuta el comando leyendo `-progress pipe:1` para informar del avance.
// `errTail` (opcional) recoge las líneas de error de ffmpeg: con `-v error`
// sólo llegan al fallar, y sin ellas el único mensaje posible era "ffmpeg".
DWORD RunWithProgress(const std::wstring& cmd, double durationSec,
                      std::atomic<int>* pct, ProcSlot* slot,
                      std::string* errTail = nullptr) {
    // Serializa con el resto de codificaciones (preparación de celdas y
    // composición) para no coincidir dos ffmpeg decodificando 4K.
    std::lock_guard<std::mutex> encoder(g_encodeMutex);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return (DWORD)-1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    // Sólo se hereda la tubería: con bInheritHandles a secas el hijo recibía
    // también sockets y handles de CEF, y al cerrarse tumbaba el proceso.
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool haveAttrs = false;
    if (InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) {
        HANDLE inherit[1] = {wr};
        haveAttrs = UpdateProcThreadAttribute(
            attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
            sizeof(inherit), nullptr, nullptr) != FALSE;
    }
    STARTUPINFOEXW six{};
    six.StartupInfo.cb = sizeof(six);
    six.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    six.StartupInfo.wShowWindow = SW_HIDE;
    six.StartupInfo.hStdOutput = wr;
    six.StartupInfo.hStdError = wr;
    six.StartupInfo.hStdInput = nullptr;
    six.lpAttributeList = haveAttrs ? attrs : nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring mcmd = cmd;
    DWORD flags = CREATE_NO_WINDOW | (haveAttrs ? EXTENDED_STARTUPINFO_PRESENT : 0);
    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, TRUE, flags, nullptr,
                        nullptr, &six.StartupInfo, &pi)) {
        if (haveAttrs) DeleteProcThreadAttributeList(attrs);
        CloseHandle(rd);
        CloseHandle(wr);
        return (DWORD)-1;
    }
    if (haveAttrs) DeleteProcThreadAttributeList(attrs);
    CloseHandle(wr);
    if (slot) {
        std::lock_guard<std::mutex> lk(slot->m);
        slot->proc = pi.hProcess;
    }

    std::string line;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        for (DWORD i = 0; i < got; ++i) {
            const char c = buf[i];
            if (c == '\n' || c == '\r') {
                size_t p = line.find("out_time_us=");
                if (p == std::string::npos) p = line.find("out_time_ms=");
                const bool esProgreso = p != std::string::npos;
                if (esProgreso) {
                    if (durationSec > 0.1) {
                        const double us = atof(line.c_str() + p + 12);
                        int pc = (int)(us / (durationSec * 1000000.0) * 100.0);
                        if (pc < 0) pc = 0;
                        if (pc > 99) pc = 99;
                        if (pct) *pct = pc;
                    }
                } else if (errTail && !line.empty()) {
                    // Línea que no es de progreso: mensaje de ffmpeg.
                    static const char* kProg[] = {
                        "frame=", "fps=", "bitrate=", "total_size=", "out_time=",
                        "dup_frames=", "drop_frames=", "speed=", "progress=",
                        "stream_0_0_q="
                    };
                    bool prog = false;
                    for (const char* kp : kProg) {
                        if (line.rfind(kp, 0) == 0) { prog = true; break; }
                    }
                    if (!prog) {
                        errTail->append(line);
                        errTail->push_back('\n');
                        if (errTail->size() > 1500) {
                            errTail->erase(0, errTail->size() - 1500);
                        }
                    }
                }
                line.clear();
            } else if (line.size() < 400) {
                line.push_back(c);
            }
        }
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    if (slot) {
        std::lock_guard<std::mutex> lk(slot->m);
        slot->proc = nullptr;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return code;
}

struct PrepJob {
    std::string slot;
    std::string hash;
    std::wstring out;
    std::wstring cmd;
    double dur = 10;
    std::atomic<int> percent{0};
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
    std::atomic<bool> cancelled{false};
    ProcSlot proc;
};

std::vector<std::shared_ptr<PrepJob>> g_prep;
std::deque<std::shared_ptr<PrepJob>> g_queue;
std::mutex g_prepMutex;
std::condition_variable g_prepCv;
std::once_flag g_prepOnce;

void PrepWorker() {
    for (;;) {
        std::shared_ptr<PrepJob> job;
        {
            std::unique_lock<std::mutex> lk(g_prepMutex);
            g_prepCv.wait(lk, [] { return !g_queue.empty(); });
            job = g_queue.front();
            g_queue.pop_front();
        }
        if (job->cancelled) {
            job->done = true;
            continue;
        }
        const std::wstring tmp = job->out + L".part.mp4";
        DeleteFileW(tmp.c_str());
        const DWORD code = RunWithProgress(job->cmd, job->dur, &job->percent,
                                           &job->proc);
        if (!job->cancelled && code == 0 &&
            GetFileAttributesW(tmp.c_str()) != INVALID_FILE_ATTRIBUTES) {
            MoveFileExW(tmp.c_str(), job->out.c_str(), MOVEFILE_REPLACE_EXISTING);
            job->ok = true;
        } else {
            DeleteFileW(tmp.c_str());
        }
        job->percent = job->ok ? 100 : job->percent.load();
        job->done = true;
    }
}

void EnsurePrepWorker() {
    std::call_once(g_prepOnce, [] { std::thread(PrepWorker).detach(); });
}

// Encadena la preparación de una celda (o reutiliza la que ya está en cola).
std::string QueueCellPrep(const ComposeCell& c, const std::string& slot,
                          bool front) {
    if (c.file.empty()) return "{\"ok\":false,\"error\":\"media\"}";
    const std::string hash = CellHash(c);
    const std::wstring out = CellCachePath(hash);
    if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return "{\"ok\":true,\"cached\":true,\"hash\":\"" + hash + "\"}";
    }
    std::string chain = CellPrepFilter(c);
    if (chain.empty()) return "{\"ok\":true,\"skip\":true}";

    int dw = 0, dh = 0;
    double dur = 10;
    if (!EditorProbeDims(c.file, dw, dh, dur) || dur < 0.2) dur = 10;
    std::wstring ff = EditorFfmpegPath();
    if (ff.empty()) return "{\"ok\":false,\"error\":\"ffmpeg\"}";

    EnsurePrepWorker();
    {
        std::lock_guard<std::mutex> lk(g_prepMutex);
        for (const auto& j : g_prep) {
            if (j->hash == hash && !j->done) {
                return "{\"ok\":true,\"queued\":true,\"hash\":\"" + hash + "\"}";
            }
        }
        auto job = std::make_shared<PrepJob>();
        job->slot = slot;
        job->hash = hash;
        job->out = out;
        job->dur = dur;
        std::string chain2 = chain + "[out]";
        job->cmd = L"\"" + ff + L"\" -y -v error -nostats -progress pipe:1 "
                   L"-i \"" + c.file + L"\" -filter_complex \"" +
                   Utf8ToWide(chain2) + L"\" -map \"[out]\" -an -c:v libx264 "
                   L"-preset veryfast -crf 16 -pix_fmt yuv420p -threads 2 "
                   L"-movflags +faststart \"" + (out + L".part.mp4") + L"\"";
        g_prep.push_back(job);
        if (front) g_queue.push_front(job);
        else g_queue.push_back(job);
        if (g_prep.size() > 24) {
            g_prep.erase(g_prep.begin(),
                         g_prep.begin() + (g_prep.size() - 24));
        }
    }
    g_prepCv.notify_one();
    return "{\"ok\":true,\"queued\":true,\"hash\":\"" + hash + "\"}";
}

void CancelSlotPrep(const std::string& slot) {
    std::vector<std::shared_ptr<PrepJob>> kill;
    {
        std::lock_guard<std::mutex> lk(g_prepMutex);
        for (auto& j : g_prep) {
            if (j->done) continue;
            if (!slot.empty() && j->slot != slot) continue;
            j->cancelled = true;
            if (!j->done) kill.push_back(j);
        }
        g_queue.erase(std::remove_if(g_queue.begin(), g_queue.end(),
                                     [](const std::shared_ptr<PrepJob>& j) {
                                         return j->cancelled.load();
                                     }),
                      g_queue.end());
    }
    for (auto& j : kill) {
        std::lock_guard<std::mutex> lk(j->proc.m);
        if (j->proc.proc) TerminateProcess(j->proc.proc, (UINT)-1);
    }
}

std::string PrepStatusJson() {
    std::lock_guard<std::mutex> lk(g_prepMutex);
    std::string j = "{\"jobs\":[";
    bool first = true;
    for (const auto& it : g_prep) {
        if (!first) j += ",";
        first = false;
        char b[256];
        sprintf_s(b,
                  "{\"slot\":\"%hs\",\"percent\":%d,\"done\":%s,\"ok\":%s,"
                  "\"cancelled\":%s}",
                  it->slot.c_str(), it->percent.load(),
                  it->done ? "true" : "false", it->ok ? "true" : "false",
                  it->cancelled ? "true" : "false");
        j += b;
    }
    j += "]}";
    return j;
}

// Cuerpo de /api/prepare: una celda suelta + slot.
std::string PrepareCell(const std::string& body) {
    ComposeCell c;
    const std::string key = JsonStrIn(body, 0, "key", "");
    if (key.empty()) return "{\"ok\":false,\"error\":\"key\"}";
    c.file = ComposeResolveFile(key);
    if (c.file.empty()) return "{\"ok\":false,\"error\":\"media\"}";
    c.w = (int)JsonNumIn(body, 0, "w", 1920);
    c.h = (int)JsonNumIn(body, 0, "h", 1080);
    if (c.w < 2) c.w = 2;
    if (c.h < 2) c.h = 2;
    c.cx = JsonNumIn(body, 0, "cx", 0);
    c.cy = JsonNumIn(body, 0, "cy", 0);
    c.cw = JsonNumIn(body, 0, "cw", 1);
    c.ch = JsonNumIn(body, 0, "ch", 1);
    c.aspect = JsonNumIn(body, 0, "aspect", 16.0 / 9.0);
    c.fit = JsonStrIn(body, 0, "fit", "expand");
    const std::string slot = JsonStrIn(body, 0, "slot", "");
    const bool front = JsonBoolIn(body, 0, "front", true);
    return QueueCellPrep(c, slot, front);
}

// Rellena width/height de un item la primera vez que se sirve su medio: así
// los filtros de resolución alcanzan también a los plugins que no los declaran.
void MaybeFillItemDims(const std::string& key, const std::wstring& file) {
    static std::mutex m;
    static std::set<std::string> tried;
    {
        std::lock_guard<std::mutex> lk(m);
        if (!tried.insert(key).second) return;
    }
    int w = 0, h = 0;
    double d = 0;
    if (EditorProbeDims(file, w, h, d)) UpdateRemoteItemDims(key, w, h);
}

// Un fichero que el motor puede reproducir como vídeo en su celda.
bool IsVideoFileName(const std::wstring& f) {
    std::wstring low = f;
    for (auto& c : low) c = (wchar_t)towlower(c);
    return low.size() > 4 &&
           (low.rfind(L".mp4") == low.size() - 4 ||
            low.rfind(L".mkv") == low.size() - 4 ||
            low.rfind(L".webm") == low.size() - 5 ||
            low.rfind(L".mov") == low.size() - 4 ||
            low.rfind(L".avi") == low.size() - 4 ||
            low.rfind(L".wmv") == low.size() - 4);
}

// Asignación real por monitor: escribe res/current.json con un vídeo por
// pantalla y avisa al motor (evento de recarga). Devuelve el JSON de respuesta,
// o "" si no se pudo escribir (se sigue con la composición normal).
std::string ApplyDisplaysPlan(const std::vector<ComposeCell>& cells) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    std::string j = "{\"displays\":[";
    bool first = true;
    for (const auto& c : cells) {
        const std::string path = WideToUtf8(c.inputFile);
        std::string esc;
        for (char ch : path) {
            if (ch == '\\' || ch == '"') esc.push_back('\\');
            esc.push_back(ch);
        }
        if (!first) j += ",";
        first = false;
        char b[360];
        sprintf_s(b,
                  "{\"file\":\"%s\",\"fit\":\"%s\",\"x\":%d,\"y\":%d,"
                  "\"w\":%d,\"h\":%d,\"cx\":0,\"cy\":0,\"cw\":1,\"ch\":1}",
                  esc.c_str(), c.fit.c_str(), c.x + vx, c.y + vy, c.w, c.h);
        j += b;
    }
    j += "]}";
    FILE* f = nullptr;
    _wfopen_s(&f, (g_baseDir + L"\\res\\current.json").c_str(), L"wb");
    if (!f) return {};
    fwrite(j.data(), 1, j.size(), f);
    fclose(f);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, L"dnxWallpaperV3Reload");
    if (ev) {
        SetEvent(ev);
        CloseHandle(ev);
    }
    return "{\"ok\":true,\"mode\":\"displays\",\"displays\":" +
           std::to_string(cells.size()) + "}";
}

// ---- Mosaico: rejilla de vídeos sobre un fondo -----------------------------
// Genera un vídeo nuevo (no una playlist): un fondo opcional a pantalla
// completa y una rejilla de celdas encima. Cada capa puede ser vídeo, imagen o
// color. Las celdas vacías dejan ver el fondo.
struct MosaicLayer {
    std::string type = "empty";  // empty | video | image | color
    std::string key;
    std::string color;           // "#rrggbb"
    std::wstring file;
    double fade = 0;
    double cx = 0, cy = 0, cw = 1, ch = 1;
    double dur = 0;              // duración del origen (0 en imágenes)
    int srcW = 0, srcH = 0;
    int inputIndex = -1;
    int x = 0, y = 0, w = 0, h = 0;  // caja destino
    double rot = 0;                  // grados (capas libres)
};

static bool IsImageFileName(const std::wstring& f) {
    std::wstring low = f;
    for (auto& c : low) c = (wchar_t)towlower(c);
    size_t q = low.find_first_of(L"?#");
    if (q != std::wstring::npos) low = low.substr(0, q);
    const wchar_t* exts[] = {L".jpg", L".jpeg", L".png", L".webp",
                             L".gif", L".avif", L".bmp"};
    for (const wchar_t* e : exts) {
        const size_t n = wcslen(e);
        if (low.size() > n && low.compare(low.size() - n, n, e) == 0) return true;
    }
    return false;
}

// Objetos {...} de un array JSON, respetando cadenas y anidamiento.
static std::vector<std::string> JsonObjectsInArray(const std::string& s,
                                                   size_t from) {
    std::vector<std::string> out;
    size_t p = s.find('[', from);
    if (p == std::string::npos) return out;
    // Cierre del array: sin esto se seguían leyendo objetos de arrays
    // posteriores ("layers" se colaba como celdas).
    size_t end = std::string::npos;
    {
        int depth = 0;
        bool inStr = false;
        for (size_t i = p; i < s.size(); ++i) {
            const char c = s[i];
            if (inStr) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == '[') ++depth;
            else if (c == ']') {
                --depth;
                if (depth == 0) { end = i; break; }
            }
        }
    }
    while (true) {
        size_t ob = s.find('{', p);
        if (ob == std::string::npos) break;
        if (end != std::string::npos && ob > end) break;
        int depth = 0;
        bool inStr = false;
        size_t i = ob;
        for (; i < s.size(); ++i) {
            const char c = s[i];
            if (inStr) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') ++depth;
            else if (c == '}') {
                --depth;
                if (depth == 0) break;
            }
        }
        if (i >= s.size()) break;
        out.push_back(s.substr(ob, i - ob + 1));
        p = i + 1;
    }
    return out;
}

// Primer objeto {...} a partir de `from` (para "background": {...}).
static bool JsonObjectAt(const std::string& s, size_t from, std::string& out) {
    size_t ob = s.find('{', from);
    if (ob == std::string::npos) return false;
    int depth = 0;
    bool inStr = false;
    for (size_t i = ob; i < s.size(); ++i) {
        const char c = s[i];
        if (inStr) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) {
                out = s.substr(ob, i - ob + 1);
                return true;
            }
        }
    }
    return false;
}

static MosaicLayer ParseMosaicLayer(const std::string& obj) {
    MosaicLayer L;
    L.key = JsonStrIn(obj, 0, "key", "");
    L.type = JsonStrIn(obj, 0, "type", "");
    L.color = JsonStrIn(obj, 0, "color", "");
    L.fade = JsonNumIn(obj, 0, "fade", 0);
    L.cx = JsonNumIn(obj, 0, "cx", 0);
    L.cy = JsonNumIn(obj, 0, "cy", 0);
    L.cw = JsonNumIn(obj, 0, "cw", 1);
    L.ch = JsonNumIn(obj, 0, "ch", 1);
    L.x = (int)JsonNumIn(obj, 0, "x", 0);
    L.y = (int)JsonNumIn(obj, 0, "y", 0);
    L.w = (int)JsonNumIn(obj, 0, "w", 0);
    L.h = (int)JsonNumIn(obj, 0, "h", 0);
    L.rot = JsonNumIn(obj, 0, "rot", 0);
    if (L.rot < -180) L.rot = -180;
    if (L.rot > 180) L.rot = 180;
    if (L.fade < 0) L.fade = 0;
    if (L.fade > 3) L.fade = 3;
    if (L.cw < 0.01) L.cw = 1;
    if (L.ch < 0.01) L.ch = 1;
    if (L.type.empty()) {
        if (!L.color.empty()) L.type = "color";
        else if (!L.key.empty()) L.type = "media";
        else L.type = "empty";
    }
    return L;
}

// Guarda el proyecto del mosaico para poder recuperarlo desde la UI.
static void SaveMosaicProject(const std::string& body, const std::string& key) {
    const std::wstring f = g_baseDir + L"\\res\\config\\mosaic.json";
    CreateDirectoryW((g_baseDir + L"\\res\\config").c_str(), nullptr);
    std::string j = body;
    while (!j.empty() &&
           (j.back() == '\n' || j.back() == '\r' || j.back() == ' '))
        j.pop_back();
    if (!j.empty() && j.back() == '}') j.pop_back();
    if (!key.empty()) j += ",\"generated\":\"" + JsonEsc(key) + "\"";
    j += "}";
    FILE* fp = nullptr;
    _wfopen_s(&fp, f.c_str(), L"wb");
    if (!fp) return;
    fwrite(j.data(), 1, j.size(), fp);
    fclose(fp);
}

// Claves aleatorias del catálogo (para el botón de aleatorio). Se excluyen
// las que ya están asignadas y los resultados generados.
std::string MosaicRandomJson(int count, const std::string& exclude) {
    auto items = GetRemoteRegistryCopy();
    std::vector<std::string> vids;
    for (const auto& it : items) {
        if (it.mediaType != "video") continue;
        if (it.provider == "optimized" || it.provider == "mosaic") continue;
        if (!it.downloadable) continue;
        if (!exclude.empty() &&
            exclude.find("|" + it.key + "|") != std::string::npos) {
            continue;
        }
        vids.push_back(it.key);
    }
    if (vids.empty()) return "{\"keys\":[]}";
    if (count < 1) count = 1;
    if (count > 64) count = 64;
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned)GetTickCount());
        seeded = true;
    }
    for (size_t i = vids.size(); i > 1; --i) {
        size_t j = (size_t)(rand() % (int)i);
        std::swap(vids[i - 1], vids[j]);
    }
    std::string j = "{\"keys\":[";
    for (int i = 0; i < count && i < (int)vids.size(); ++i) {
        if (i) j += ",";
        j += "\"" + JsonEsc(vids[i]) + "\"";
    }
    j += "]}";
    return j;
}

std::string MosaicWorkspace(const std::string& body) {
    // Un ffmpeg pesado a la vez: RunWithProgress ya toma g_encodeMutex.
    static std::mutex mosaicMutex;
    std::lock_guard<std::mutex> lock(mosaicMutex);

    int cols = (int)JsonNumIn(body, 0, "cols", 2);
    int rows = (int)JsonNumIn(body, 0, "rows", 2);
    if (cols < 1) cols = 1;
    if (cols > 5) cols = 5;
    if (rows < 1) rows = 1;
    if (rows > 5) rows = 5;

    int W = (int)JsonNumIn(body, 0, "width", 1920);
    int H = (int)JsonNumIn(body, 0, "height", 1080);
    if (W < 320) W = 1920;
    if (H < 240) H = 1080;
    if (W > 7680) W = 7680;
    if (H > 4320) H = 4320;
    if (W % 2) --W;
    if (H % 2) --H;

    size_t cp = body.find("\"cells\"");
    if (cp == std::string::npos) return "{\"ok\":false,\"error\":\"cells\"}";
    std::vector<MosaicLayer> cells;
    for (const auto& obj : JsonObjectsInArray(body, cp)) {
        cells.push_back(ParseMosaicLayer(obj));
    }
    if (cells.empty()) return "{\"ok\":false,\"error\":\"sin celdas\"}";

    MosaicLayer bg;
    {
        size_t bp = body.find("\"background\"");
        if (bp != std::string::npos) {
            std::string obj;
            if (JsonObjectAt(body, bp, obj)) bg = ParseMosaicLayer(obj);
        }
    }

    // Capa 3: elementos libres (posición, tamaño y rotación propios).
    std::vector<MosaicLayer> layers;
    {
        size_t lp = body.find("\"layers\"");
        if (lp != std::string::npos) {
            for (const auto& obj : JsonObjectsInArray(body, lp)) {
                layers.push_back(ParseMosaicLayer(obj));
            }
        }
    }

    const int cellW = ((W / cols) / 2) * 2;
    const int cellH = ((H / rows) / 2) * 2;
    const int offX = (W - cellW * cols) / 2;
    const int offY = (H - cellH * rows) / 2;
    bg.x = 0;
    bg.y = 0;
    bg.w = W;
    bg.h = H;
    for (size_t i = 0; i < cells.size() && i < (size_t)(cols * rows); ++i) {
        cells[i].x = offX + (int)(i % cols) * cellW;
        cells[i].y = offY + (int)(i / cols) * cellH;
        cells[i].w = cellW;
        cells[i].h = cellH;
    }

    // Resolver medios. `dur` = la más larga de las capas con vídeo.
    // Descargar los que falten puede tardar: se informa en /api/progress.
    SetProgress(true, -1, "descargando medios");
    double dur = 0;
    int inputs = 0;
    int content = 0;
    auto resolve = [&](MosaicLayer& L) -> std::string {
        if (L.type == "empty") return {};
        if (L.type == "color") {
            if (L.color.empty()) L.type = "empty";
            else ++content;
            return {};
        }
        if (L.key.empty()) {
            L.type = "empty";
            return {};
        }
        L.file = ComposeResolveFile(L.key);
        if (L.file.empty()) return "no se pudo descargar";
        const bool video = IsVideoFileName(L.file);
        if (!video && !IsImageFileName(L.file)) return "formato no soportado";
        if (video) {
            if (!EditorProbeDims(L.file, L.srcW, L.srcH, L.dur) ||
                L.dur <= 0.05) {
                return "no se pudo leer el video";
            }
            L.type = "video";
            if (L.dur > dur) dur = L.dur;
        } else {
            int w = 0, h = 0;
            double d = 0;
            EditorProbeDims(L.file, w, h, d);
            L.srcW = w;
            L.srcH = h;
            L.dur = 0;
            L.type = "image";
        }
        L.inputIndex = inputs++;
        ++content;
        return {};
    };
    {
        std::string e = resolve(bg);
        if (!e.empty()) {
            return "{\"ok\":false,\"error\":\"" + e + "\",\"detail\":\"" +
                   JsonEsc(bg.key) + "\"}";
        }
        for (auto& c : cells) {
            e = resolve(c);
            if (!e.empty()) {
                return "{\"ok\":false,\"error\":\"" + e + "\",\"detail\":\"" +
                       JsonEsc(c.key) + "\"}";
            }
        }
        for (auto& L : layers) {
            if (L.w < 8) L.w = (int)(W * 0.3) & ~1;
            if (L.h < 8) L.h = (int)(H * 0.3) & ~1;
            e = resolve(L);
            if (!e.empty()) {
                return "{\"ok\":false,\"error\":\"" + e + "\",\"detail\":\"" +
                       JsonEsc(L.key) + "\"}";
            }
        }
    }
    if (!content) return "{\"ok\":false,\"error\":\"sin contenido\"}";
    if (dur <= 0.1) dur = 10.0;  // sólo imágenes/colores
    if (dur > 600) dur = 600;    // tope de seguridad
    DlLog("mosaic: %dx%d, %d entradas, dur %.2f", cols, rows, inputs, dur);

    // Orden de entradas de ffmpeg (inputIndex).
    std::vector<MosaicLayer*> sources((size_t)inputs, nullptr);
    if (bg.inputIndex >= 0) sources[(size_t)bg.inputIndex] = &bg;
    for (auto& c : cells) {
        if (c.inputIndex >= 0) sources[(size_t)c.inputIndex] = &c;
    }
    for (auto& L : layers) {
        if (L.inputIndex >= 0) sources[(size_t)L.inputIndex] = &L;
    }

    std::string filter;
    std::string prev;
    int tagId = 0;

    // Cadena de una capa -> [Lx] de longitud `target` (con fundido si fade>0
    // se recorta después a `dur`).
    auto addLayer = [&](const MosaicLayer& L, double target, bool fade,
                        std::string& label) -> bool {
        const std::string tag = "L" + std::to_string(tagId++);
        if (L.type == "color") {
            std::string hex = L.color;
            if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
            if (hex.size() != 6) return false;
            filter += ";color=c=0x" + hex +
                      ":s=" + std::to_string(L.w) + "x" + std::to_string(L.h) +
                      ":r=30:d=" + std::to_string(dur) +
                      ",setpts=PTS-STARTPTS[" + tag + "]";
            label = tag;
            return true;
        }
        if (L.inputIndex < 0) return false;
        const std::string in = "[" + std::to_string(L.inputIndex) + ":v]";
        ComposeCell cc;
        cc.file = L.file;
        cc.inputFile = L.file;
        cc.w = L.w;
        cc.h = L.h;
        cc.cx = L.cx;
        cc.cy = L.cy;
        cc.cw = L.cw;
        cc.ch = L.ch;
        cc.fit = "expand";
        cc.aspect = L.srcW > 0 && L.srcH > 0 ? (double)L.srcW / (double)L.srcH
                                             : 16.0 / 9.0;
        if (L.type == "image") {
            std::string chain = ComposeCellChain(cc, in + "fps=30,");
            if (chain.empty()) return false;
            filter += ";" + chain + ",tpad=stop_mode=clone:stop_duration=2,"
                      "trim=duration=" + std::to_string(dur) +
                      ",setpts=PTS-STARTPTS[" + tag + "]";
            label = tag;
            return true;
        }
        // Vídeo: repetir y estirar hasta `target`.
        const double ratio = L.dur / dur;
        int loops = 1;
        if (ratio < 0.4) {
            loops = (int)(1.0 / ratio);
            if (loops < 2) loops = 2;
            if (loops > 32) loops = 32;
        } else if (ratio < 0.6) {
            loops = 2;
        }
        const double speed = target / (L.dur * loops);
        const std::string loop = "lp" + std::to_string(tagId++);
        if (loops > 1) {
            std::string labels;
            for (int k = 0; k < loops; ++k) {
                labels += "[" + loop + "_" + std::to_string(k) + "]";
            }
            filter += ";" + in + "split=" + std::to_string(loops) + labels;
            filter += ";" + labels + "concat=n=" + std::to_string(loops) +
                      ":v=1:a=0[" + loop + "]";
        } else {
            filter += ";" + in + "null[" + loop + "]";
        }
        char b[2048];
        sprintf_s(b, "[%s]setpts=PTS*%.6f[ts%s]", loop.c_str(), speed,
                  loop.c_str());
        filter += ";" + std::string(b);

        ComposeCell cc2 = cc;
        std::string chain = ComposeCellChain(cc2, "[ts" + loop + "]");
        if (chain.empty()) return false;
        sprintf_s(b, ",tpad=stop_mode=clone:stop_duration=2,trim=duration=%.3f,"
                     "setpts=PTS-STARTPTS[fit%s]", target, loop.c_str());
        chain += b;
        filter += ";" + chain;

        if (fade && L.fade > 0.05) {
            sprintf_s(b,
                      ";[fit%s]split=3[%s_a][%s_b][%s_c];"
                      "[%s_b]trim=start=0:end=%.3f,setpts=PTS-STARTPTS[%s_h];"
                      "[%s_c]trim=start=%.3f,setpts=PTS-STARTPTS[%s_t];"
                      "[%s_a]trim=start=%.3f:end=%.3f,setpts=PTS-STARTPTS[%s_m];"
                      "[%s_t][%s_h]xfade=transition=fade:duration=%.3f:"
                      "offset=0[%s_s];"
                      "[%s_m][%s_s]concat=n=2:v=1:a=0[%s]",
                      loop.c_str(),
                      loop.c_str(), loop.c_str(), loop.c_str(),
                      loop.c_str(), L.fade, loop.c_str(),
                      loop.c_str(), target - L.fade, loop.c_str(),
                      loop.c_str(), L.fade, target - L.fade, loop.c_str(),
                      loop.c_str(), loop.c_str(), L.fade, loop.c_str(),
                      loop.c_str(), loop.c_str(),
                      tag.c_str());
            filter += b;
            label = tag;
            return true;
        }
        sprintf_s(b, ";[fit%s]trim=duration=%.3f,setpts=PTS-STARTPTS[%s]",
                  loop.c_str(), dur, tag.c_str());
        filter += b;
        label = tag;
        return true;
    };

    // Capa 1: fondo. La base es siempre negra (así el filtro nunca empieza
    // por ';', que ffmpeg rechaza) y el fondo se superpone encima.
    filter = "color=c=black:s=" + std::to_string(W) + "x" +
             std::to_string(H) + ":r=30:d=" + std::to_string(dur) + "[base0]";
    int slot = 0;
    if (bg.type != "empty") {
        std::string lbl;
        if (addLayer(bg, dur, false, lbl)) {
            filter += ";[base0][" + lbl + "]overlay=0:0:shortest=1[base1]";
            slot = 1;
        }
    }

    // Capa 2: celdas de la cuadrícula.
    for (size_t i = 0; i < cells.size() && i < (size_t)(cols * rows); ++i) {
        const MosaicLayer& c = cells[i];
        if (c.type == "empty") continue;
        const double target = dur + (c.fade > 0.05 ? c.fade : 0.0);
        std::string lbl;
        if (!addLayer(c, target, true, lbl)) continue;
        const std::string next = "base" + std::to_string(slot + 1);
        filter += ";[base" + std::to_string(slot) + "][" + lbl + "]overlay=" +
                  std::to_string(c.x) + ":" + std::to_string(c.y) +
                  ":shortest=1[" + next + "]";
        prev = next;
        ++slot;
    }
    // Capa 3: elementos libres encima de todo (posición, tamaño y rotación
    // propios). No hay límite de elementos.
    int over = slot;
    for (size_t i = 0; i < layers.size(); ++i) {
        MosaicLayer& L = layers[i];
        if (L.type == "empty") continue;
        const double target = dur + (L.fade > 0.05 ? L.fade : 0.0);
        std::string lbl;
        if (!addLayer(L, target, true, lbl)) continue;
        int ox = L.x;
        int oy = L.y;
        if (fabs(L.rot) > 0.01) {
            const double a = L.rot * 3.14159265358979 / 180.0;
            const int ow = (int)ceil(fabs(L.w * cos(a)) + fabs(L.h * sin(a)));
            const int oh = (int)ceil(fabs(L.w * sin(a)) + fabs(L.h * cos(a)));
            const std::string rot = "rot" + std::to_string(i);
            char rb[256];
            sprintf_s(rb, ";[%s]format=rgba,rotate=%.6f:ow=%d:oh=%d:c=none[%s]",
                      lbl.c_str(), a, ow, oh, rot.c_str());
            filter += rb;
            lbl = rot;
            ox = L.x + (L.w - ow) / 2;
            oy = L.y + (L.h - oh) / 2;
        }
        const std::string next = "base" + std::to_string(over + 1);
        filter += ";[base" + std::to_string(over) + "][" + lbl + "]overlay=" +
                  std::to_string(ox) + ":" + std::to_string(oy) +
                  ":shortest=1[" + next + "]";
        over++;
    }
    slot = over;
    if (slot == 0 && bg.type == "empty") {
        return "{\"ok\":false,\"error\":\"sin contenido dibujable\"}";
    }

    // Longitud exacta + último fotograma clonado si algo quedó corto.
    filter += ";[base" + std::to_string(slot) + "]tpad=stop_mode=clone:"
              "stop_duration=1,trim=duration=" + std::to_string(dur) +
              ",setpts=PTS-STARTPTS[out]";
    {
        char dbg[128];
        sprintf_s(dbg, "mosaic: filtro %zu bytes, %d celdas, %d libres",
                  filter.size(), (int)cells.size(), (int)layers.size());
        DlLog("%s", dbg);
        FILE* df = nullptr;
        _wfopen_s(&df, (g_baseDir + L"\\res\\cache\\mosaic-filter.txt").c_str(),
                  L"wb");
        if (df) {
            fwrite(filter.data(), 1, filter.size(), df);
            fclose(df);
        }
    }

    const std::wstring dir = g_baseDir + L"\\res\\mosaics";
    CreateDirectoryW((g_baseDir + L"\\res").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    char idb[64];
    sprintf_s(idb, "ms%08x", (unsigned)GetTickCount());
    const std::wstring out = dir + L"\\" + Utf8ToWide(idb) + L".mp4";

    std::wstring ff = EditorFfmpegPath();
    if (ff.empty()) return "{\"ok\":false,\"error\":\"ffmpeg\"}";

    int capMB = (int)JsonNumIn(body, 0, "maxSizeMB", 64);
    if (capMB < 4) capMB = 4;
    if (capMB > 512) capMB = 512;
    int kbps = (int)(capMB * 8.0 * 0.92 / dur * 1000.0);
    if (kbps < 800) kbps = 800;

    std::wstring cmd = L"\"" + ff + L"\" -y -v error -nostats -filter_threads 1 "
                       L"-threads 2 ";
    for (auto* L : sources) {
        if (!L) continue;
        if (L->type == "image") {
            cmd += L"-loop 1 -t " + std::to_wstring((int)dur + 3) + L" ";
        }
        cmd += L"-i \"" + L->file + L"\" ";
    }
    cmd += L"-filter_complex \"" + Utf8ToWide(filter) + L"\" -map \"[out]\" -an "
           L"-c:v libx264 -preset veryfast -b:v " + std::to_wstring(kbps) +
           L"k -maxrate " + std::to_wstring(kbps) + L"k -bufsize " +
           std::to_wstring(kbps * 2) + L"k -profile:v high -pix_fmt yuv420p "
           L"-threads 2 -movflags +faststart -progress pipe:1 \"" + out + L"\"";

    SetProgress(true, 0, "mosaico");
    g_compPct = 0;
    std::string ffErr;
    DlLog("mosaic: ffmpeg (%zu bytes de comando)", cmd.size());
    const DWORD code = RunWithProgress(cmd, dur, &g_compPct, nullptr, &ffErr);
    DlLog("mosaic: ffmpeg code=%lu", (unsigned long)code);
    g_compPct = -1;
    SetProgress(false, -1, "");
    if (code != 0) {
        DeleteFileW(out.c_str());
        std::string det = ffErr;
        if (det.size() > 700) det = det.substr(det.size() - 700);
        return "{\"ok\":false,\"error\":\"ffmpeg\",\"detail\":\"" + JsonEsc(det) +
               "\"}";
    }

    RemoteItem it;
    it.provider = "mosaics";
    it.id = idb;
    it.key = "mosaics:" + it.id;
    it.title = "Mosaico " + std::to_string(cols) + "x" + std::to_string(rows);
    it.mediaType = "video";
    it.animated = true;
    it.mediaUrl = WpxFileUrl(out);
    it.downloadable = true;
    it.tags = "mosaico,escritorio";
    it.width = W;
    it.height = H;
    // Ficha junto al vídeo: el plugin "mosaics" la usa para el título.
    {
        char meta[256];
        sprintf_s(meta,
                  "{\"title\":\"Mosaico %dx%d\",\"width\":%d,\"height\":%d,"
                  "\"seconds\":%d}",
                  cols, rows, W, H, (int)dur);
        const std::wstring jf = dir + L"\\" + Utf8ToWide(idb) + L".json";
        FILE* mf = nullptr;
        _wfopen_s(&mf, jf.c_str(), L"wb");
        if (mf) {
            fwrite(meta, 1, strlen(meta), mf);
            fclose(mf);
        }
    }
    {
        SYSTEMTIME st{};
        GetSystemTime(&st);
        char ts[40];
        sprintf_s(ts, "%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond);
        it.publishedAt = ts;
    }
    MergeRemoteItems(std::vector<RemoteItem>{it});
    SaveCatalogSnapshot(g_baseDir, GetRemoteRegistryCopy());
    SaveMosaicProject(body, it.key);

    return "{\"ok\":true,\"key\":\"" + it.key + "\",\"width\":" +
           std::to_string(W) + ",\"height\":" + std::to_string(H) +
           ",\"seconds\":" + std::to_string((int)dur) + "}";
}

std::string ComposeWorkspace(const std::string& body, int* outW, int* outH) {
    // Una composición a la vez: dos ffmpeg simultáneos (dos clics en Aplicar)
    // multiplican memoria y CPU y en un equipo justo pueden tumbar el proceso.
    static std::mutex composeMutex;
    std::lock_guard<std::mutex> composeLock(composeMutex);
    const int canvasW = (int)JsonNumIn(body, 0, "canvasW", 0);
    const int canvasH = (int)JsonNumIn(body, 0, "canvasH", 0);
    if (canvasW < 64 || canvasH < 64) return "{\"ok\":false,\"error\":\"canvas\"}";

    int capMB = (int)JsonNumIn(body, 0, "maxSizeMB", 16);
    if (capMB < 1) capMB = 1;
    if (capMB > 64) capMB = 64;

    std::vector<ComposeCell> cells;
    int missing = 0;
    bool panorama = false;
    ComposeCell pan;
    const bool black = JsonBoolIn(body, 0, "black", false);
    if (black) {
        // Sin fondo: no hay celdas ni panorama que componer.
    } else if (body.find("\"panorama\"") != std::string::npos) {
        size_t pp = body.find("\"panorama\"");
        pan.fit = JsonStrIn(body, pp, "fit", "expand");
        pan.cx = JsonNumIn(body, pp, "cx", 0);
        pan.cy = JsonNumIn(body, pp, "cy", 0);
        pan.cw = JsonNumIn(body, pp, "cw", 1);
        pan.ch = JsonNumIn(body, pp, "ch", 1);
        pan.aspect = JsonNumIn(body, pp, "aspect", 16.0 / 9.0);
        pan.w = canvasW;
        pan.h = canvasH;
        pan.key = JsonStrIn(body, pp, "key", "");
        pan.file = ComposeResolveFile(pan.key);
        if (pan.file.empty()) return "{\"ok\":false,\"error\":\"panorama\"}";
        panorama = true;
    } else {
        size_t cp = body.find("\"cells\"");
        if (cp == std::string::npos) return "{\"ok\":false,\"error\":\"cells\"}";
        size_t p = body.find('[', cp);
        if (p == std::string::npos) return "{\"ok\":false,\"error\":\"cells\"}";
        // El array de celdas acaba en el primer ']' de este nivel; las celdas
        // son objetos {..} seguidos, así que se recorren por sus "key".
        size_t arrEnd = body.find(']', p);
        while (true) {
            size_t k = body.find("\"key\"", p);
            if (k == std::string::npos) break;
            if (arrEnd != std::string::npos && k > arrEnd) break;
            size_t objEnd = body.find('}', k);
            if (objEnd == std::string::npos) break;
            std::string obj = body.substr(k, objEnd - k);
            ComposeCell c;
            c.key = JsonStrIn(obj, 0, "key", "");
            c.file = ComposeResolveFile(c.key);
            if (c.file.empty()) { ++missing; p = objEnd + 1; continue; }
            c.x = (int)JsonNumIn(obj, 0, "x", 0);
            c.y = (int)JsonNumIn(obj, 0, "y", 0);
            c.w = (int)JsonNumIn(obj, 0, "w", canvasW);
            c.h = (int)JsonNumIn(obj, 0, "h", canvasH);
            c.cx = JsonNumIn(obj, 0, "cx", 0);
            c.cy = JsonNumIn(obj, 0, "cy", 0);
            c.cw = JsonNumIn(obj, 0, "cw", 1);
            c.ch = JsonNumIn(obj, 0, "ch", 1);
            c.aspect = JsonNumIn(obj, 0, "aspect", 16.0 / 9.0);
            c.fit = JsonStrIn(obj, 0, "fit", "expand");
            // El encuadre puede salirse del origen (bandas negras): no se
            // recorta aquí, se resuelve en el filtro.
            if (c.cw <= 0.001) c.cw = 1;
            if (c.ch <= 0.001) c.ch = 1;
            if (c.w < 2) c.w = 2;
            if (c.h < 2) c.h = 2;
            cells.push_back(c);
            p = objEnd + 1;
        }
    }

    if (!panorama && cells.empty() && !black) {
        if (missing > 0) {
            return "{\"ok\":false,\"error\":\"no se pudo descargar el wallpaper\","
                   "\"missing\":" + std::to_string(missing) + "}";
        }
        return "{\"ok\":false,\"error\":\"sin medios\"}";
    }
    if (missing > 0) {
        return "{\"ok\":false,\"error\":\"falta descargar un wallpaper\","
               "\"missing\":" + std::to_string(missing) + "}";
    }

    // Cada celda se toma de la caché de preparación si ya está lista.
    if (panorama) {
        pan.inputFile = pan.file;
    } else {
        for (auto& c : cells) {
            if (c.file.empty()) continue;
            const std::wstring cached = CellCachePath(CellHash(c));
            if (GetFileAttributesW(cached.c_str()) != INVALID_FILE_ATTRIBUTES) {
                c.inputFile = cached;
                c.cached = true;
            } else {
                c.inputFile = c.file;
            }
        }
    }

    // El medio tiene que poder decodificarse: una descarga cortada, un fichero
    // que no es vídeo o un códec no soportado hacían que ffmpeg fallara con un
    // "Error: ffmpeg" sin pista ninguna. Se comprueba con ffprobe y, si el
    // origen está incompleto, se vuelve a descargar una vez.
    auto medioOk = [](const std::wstring& f) {
        int w = 0, h = 0;
        double d = 0;
        return !f.empty() && EditorProbeDims(f, w, h, d);
    };
    auto asegurarMedio = [&](ComposeCell& c) -> bool {
        if (medioOk(c.inputFile)) return true;
        if (c.cached) {
            // Celda preparada ilegible: se descarta y se usa el original.
            DeleteFileW(c.inputFile.c_str());
            c.cached = false;
            c.inputFile = c.file;
            if (medioOk(c.inputFile)) return true;
        }
        if (c.key.empty()) return false;
        if (!c.file.empty()) DeleteFileW(c.file.c_str());
        if (!FetchRemoteMediaSync(g_baseDir, c.key, 240000)) return false;
        c.file = ComposeResolveFile(c.key);
        if (c.file.empty()) return false;
        c.inputFile = c.file;
        int w = 0, h = 0;
        double d = 0;
        if (!EditorProbeDims(c.file, w, h, d)) return false;
        if (w > 0 && h > 0) c.aspect = (double)w / (double)h;
        return true;
    };
    std::string badKey;
    if (panorama) {
        if (!asegurarMedio(pan)) badKey = pan.key;
    } else {
        for (auto& c : cells) {
            if (asegurarMedio(c)) continue;
            badKey = c.key;
            break;
        }
    }
    if (!badKey.empty()) {
        return "{\"ok\":false,\"error\":\"no se pudo leer el video\",\"detail\":\"" +
               JsonEsc(badKey) + "\"}";
    }

    // Asignación real por monitor: con dos o más pantallas y sin fundido de
    // bucle, el motor reproduce un vídeo por pantalla. No se compone nada (ni
    // espera ni disco). Con fundido hace falta ffmpeg (crossfade real).
    const double fadeReq = JsonNumIn(body, 0, "fade", 0.0);
    if (!black && !panorama && cells.size() >= 2 && fadeReq <= 0.0001) {
        // Se prefiere el fichero original (calidad nativa) a la celda ya
        // preparada por ffmpeg; si falta, se usa la celda preparada.
        std::vector<ComposeCell> disp = cells;
        bool full = true;
        for (auto& c : disp) {
            if (c.cx > 0.0005 || c.cy > 0.0005 || c.cw < 0.9995 ||
                c.ch < 0.9995) {
                full = false;
                break;
            }
            if (!c.file.empty() && IsVideoFileName(c.file) &&
                GetFileAttributesW(c.file.c_str()) != INVALID_FILE_ATTRIBUTES) {
                c.inputFile = c.file;
            }
            if (!IsVideoFileName(c.inputFile)) full = false;
        }
        if (full) {
            std::string applied = ApplyDisplaysPlan(disp);
            if (!applied.empty()) return applied;
        }
    }

    // Un input por fichero distinto. Se resuelven antes de construir el filtro:
    // la duración hace falta para el fundido de bucle.
    std::vector<std::wstring> inputs;
    auto inputFor = [&](const std::wstring& f) -> int {
        for (size_t j = 0; j < inputs.size(); ++j) {
            if (inputs[j] == f) return (int)j;
        }
        inputs.push_back(f);
        return (int)inputs.size() - 1;
    };
    if (panorama) inputFor(pan.inputFile);
    for (const auto& c : cells) {
        if (!c.inputFile.empty()) inputFor(c.inputFile);
    }

    double dur = 10.0;
    if (!inputs.empty()) {
        int dw = 0, dh = 0;
        double d = 0;
        if (EditorProbeDims(inputs[0], dw, dh, d) && d > 0.2) dur = d;
    }
    if (black) dur = 3.0;

    // Fundido de bucle: se solapa el final con el principio para que el vídeo
    // vuelva a empezar sin salto (crossfade). El clip final dura dur - fade.
    double fade = JsonNumIn(body, 0, "fade", 0.0);
    if (fade < 0) fade = 0;
    if (fade > 3) fade = 3;
    if (black || inputs.empty() || dur < fade * 3.0 + 0.5) fade = 0;

    std::string filter;
    std::string prev;
    if (black) {
        filter = "color=c=black:s=" + std::to_string(canvasW) + "x" +
                 std::to_string(canvasH) + ":r=30:d=3,setsar=1[out]";
        prev = "out";
    } else if (panorama) {
        int idx = inputFor(pan.inputFile);
        std::string chain = pan.cached
            ? "[" + std::to_string(idx) + ":v]setsar=1"
            : ComposeCellChain(pan, "[" + std::to_string(idx) + ":v]");
        if (chain.empty()) {
            // Encuadre totalmente fuera del vídeo: escritorio en negro.
            filter = "color=c=black:s=" + std::to_string(canvasW) + "x" +
                     std::to_string(canvasH) + ":r=30:d=3,setsar=1[out]";
        } else {
            filter = chain + "[out]";
        }
        prev = "out";
    } else {
        filter = "color=c=black:s=" + std::to_string(canvasW) + "x" +
                 std::to_string(canvasH) + ":r=30[base0]";
        prev = "base0";
        int used = 0;
        for (size_t i = 0; i < cells.size(); ++i) {
            const ComposeCell& c = cells[i];
            const int idx = inputFor(c.inputFile);
            std::string chain = c.cached
                ? "[" + std::to_string(idx) + ":v]setsar=1"
                : ComposeCellChain(c, "[" + std::to_string(idx) + ":v]");
            if (chain.empty()) continue;  // fuera del vídeo: queda el negro base
            const int n = used++;
            filter += ";" + chain + "[c" + std::to_string(n) + "]";
            char ov[256];
            sprintf_s(ov, "[%s][c%d]overlay=%d:%d:shortest=1[base%d]",
                      prev.c_str(), n, c.x, c.y, n + 1);
            filter += ";" + std::string(ov);
            prev = "base" + std::to_string(n + 1);
        }
    }

    if (fade > 0.05) {
        char f[512];
        sprintf_s(f,
                  ";[%s]split=3[loopA][loopB][loopC];"
                  "[loopB]trim=start=0:end=%.3f,setpts=PTS-STARTPTS[head];"
                  "[loopC]trim=start=%.3f,setpts=PTS-STARTPTS[tail];"
                  "[loopA]trim=start=%.3f:end=%.3f,setpts=PTS-STARTPTS[mid];"
                  "[tail][head]xfade=transition=fade:duration=%.3f:offset=0[seam];"
                  "[mid][seam]concat=n=2:v=1:a=0[loopout]",
                  prev.c_str(), fade, dur - fade, fade, dur - fade, fade);
        filter += f;
        prev = "loopout";
        dur -= fade;
    }

    // Resolución de salida opcional (clonar a la del monitor más pequeño).
    int targetH = (int)JsonNumIn(body, 0, "outH", 0);
    if (targetH >= 120 && targetH != canvasH) {
        int targetW = (int)((long long)canvasW * targetH / canvasH);
        if (targetW % 2) --targetW;
        if (targetH % 2) --targetH;
        if (targetW >= 120) {
            filter += ";" + std::string("[") + prev + "]scale=" +
                      std::to_string(targetW) + ":" + std::to_string(targetH) +
                      ",setsar=1[resized]";
            prev = "resized";
        }
    }

    const std::wstring dir = g_baseDir + L"\\res\\providers\\optimized\\media";
    CreateDirectoryW((g_baseDir + L"\\res\\providers").c_str(), nullptr);
    CreateDirectoryW((g_baseDir + L"\\res\\providers\\optimized").c_str(),
                     nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    char idb[64];
    sprintf_s(idb, "ws%08x", (unsigned)GetTickCount());
    const std::wstring out = dir + L"\\" + Utf8ToWide(idb) + L".mp4";

    std::wstring ff = EditorFfmpegPath();
    if (ff.empty()) return "{\"ok\":false,\"error\":\"ffmpeg\"}";

    std::wstring cmd = L"\"" + ff + L"\" -y -v error -nostats -filter_threads 1 "
                       L"-threads 2 ";
    for (const auto& in : inputs) {
        cmd += L"-i \"" + in + L"\" ";
    }
    int kbps = (int)(capMB * 8.0 * 0.92 / dur * 1000.0);
    if (kbps < 600) kbps = 600;
    // Sin búfer fijo: un filtro largo desbordaba y abortaba el proceso.
    cmd += L"-filter_complex \"" + Utf8ToWide(filter) + L"\" -map \"[" +
           Utf8ToWide(prev) + L"]\" -an -c:v libx264 -preset veryfast -b:v " +
           std::to_wstring(kbps) + L"k -maxrate " + std::to_wstring(kbps) +
           L"k -bufsize " + std::to_wstring(kbps * 2) +
           L"k -profile:v high -pix_fmt yuv420p -threads 2 "
           L"-movflags +faststart -progress pipe:1 \"" + out + L"\"";

    SetProgress(true, 0, "componiendo");
    g_compPct = 0;
    std::string ffErr;
    const DWORD code = RunWithProgress(cmd, dur, &g_compPct, nullptr, &ffErr);
    g_compPct = -1;
    SetProgress(false, -1, "");
    if (code != 0) {
        DeleteFileW(out.c_str());
        // Se devuelven las últimas líneas de ffmpeg: es lo único que explica
        // el fallo (códec, filtro, memoria...).
        std::string det = ffErr;
        if (det.size() > 700) det = det.substr(det.size() - 700);
        return "{\"ok\":false,\"error\":\"ffmpeg\",\"detail\":\"" + JsonEsc(det) +
               "\"}";
    }

    // Se registra como item y se aplica.
    std::string id = idb;
    RemoteItem it;
    it.provider = "optimized";
    it.id = id;
    it.key = "optimized:" + id;
    it.title = "Escritorio compuesto";
    it.mediaType = "video";
    it.animated = true;
    it.mediaUrl = WpxFileUrl(out);
    it.downloadable = true;
    it.tags = "escritorio";
    {
        SYSTEMTIME st{};
        GetSystemTime(&st);
        char ts[40];
        sprintf_s(ts, "%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond);
        it.publishedAt = ts;
    }
    MergeRemoteItems(std::vector<RemoteItem>{it});
    SaveCatalogSnapshot(g_baseDir, GetRemoteRegistryCopy());

    std::string err;
    bool applied = ApplyCatalogItem(g_baseDir, it.key, &err);
    if (outW) *outW = canvasW;
    if (outH) *outH = canvasH;
    return "{\"ok\":true,\"key\":\"" + it.key + "\",\"applied\":" +
           (applied ? "true" : "false") + "}";
}

// ---- proxy de vista previa -------------------------------------------------
// El preview del editor pinta varios vídeos a la vez y la UI va con la GPU
// desactivada: dos 4K en software no se sostienen y uno se queda congelado.
// Se genera una copia ligera (540p) por item, cacheada, sólo para el preview.
std::string PreviewProxyName(const std::string& key) {
    std::string out;
    for (char c : key) {
        const unsigned char u = (unsigned char)c;
        if (isalnum(u) || c == '-' || c == '_' || c == '.') out.push_back(c);
        else out.push_back('_');
    }
    if (out.size() > 120) out = out.substr(out.size() - 120);
    return out;
}

std::wstring EnsurePreviewProxy(const std::wstring& baseDir,
                                const std::string& key) {
    std::wstring src = RemoteMediaPath(baseDir, key);
    if (src.empty()) return {};
    // Imágenes y medios sin ffmpeg se sirven tal cual.
    int w = 0, h = 0;
    double dur = 0;
    if (!EditorProbeDims(src, w, h, dur)) return src;
    if (h <= 720 && w <= 1280) return src;

    const std::wstring dir = baseDir + L"\\res\\cache\\preview";
    CreateDirectoryW((baseDir + L"\\res\\cache").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring out =
        dir + L"\\" + Utf8ToWide(PreviewProxyName(key)) + L".mp4";
    if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) return out;

    static std::mutex previewMutex;
    std::lock_guard<std::mutex> lock(previewMutex);
    if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) return out;
    std::lock_guard<std::mutex> encoder(g_encodeMutex);

    std::wstring ff = EditorFfmpegPath();
    if (ff.empty()) return src;
    const std::wstring tmp = out + L".part.mp4";
    DeleteFileW(tmp.c_str());
    std::wstring cmd = L"\"" + ff + L"\" -y -v error -nostats -i \"" + src +
                       L"\" -vf \"scale=-2:540:flags=lanczos\" -c:v libx264 "
                       L"-preset veryfast -crf 25 -pix_fmt yuv420p "
                       L"-movflags +faststart -an \"" + tmp + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mcmd = cmd;
    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return src;
    }
    WaitForSingleObject(pi.hProcess, 300000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code != 0 || GetFileAttributesW(tmp.c_str()) == INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(tmp.c_str());
        return src;
    }
    if (!MoveFileExW(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return src;
    }
    return out;
}

// ---- vista previa animada (WebP) -------------------------------------------
// El editor de pantallas muestra varios wallpapers a la vez y la UI va sin GPU.
// Ejecuta un proceso sin ventana (declarada antes de usarse en las ayudas de
// vista previa; su definición está más abajo, con la descarga de ffmpeg).
DWORD RunHidden(const std::wstring& cmd);

std::wstring EnsurePreviewWebp(const std::wstring& baseDir, const std::string& key) {
    std::wstring src = RemoteMediaPath(baseDir, key);
    std::wstring tempClip;
    if (src.empty()) {
        // Sin medio descargado: se pide al plugin el clip de preview con su
        // propio mecanismo (`fetch --kind preview`), que es el que resuelve
        // páginas protegidas. Sólo si no hay plugin se prueba el preview del
        // catálogo por HTTP.
        tempClip = FetchRemotePreviewSync(baseDir, key, 180000);
        if (!tempClip.empty()) src = tempClip;
    }
    if (src.empty()) {
        // Último recurso: descargar el medio (una sola vez) y generar de ahí.
        if (FetchRemoteMediaSync(baseDir, key, 180000)) {
            src = RemoteMediaPath(baseDir, key);
        }
    }
    if (src.empty()) return {};

    const std::wstring dir = baseDir + L"\\res\\cache\\preview";
    CreateDirectoryW((baseDir + L"\\res\\cache").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring out =
        dir + L"\\" + Utf8ToWide(PreviewProxyName(key)) + L"-2.webp";
    if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) return out;

    static std::mutex webpMutex;
    std::lock_guard<std::mutex> lock(webpMutex);
    if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) return out;
    // No espera al ffmpeg grande (preparación/composición): esta miniatura es
    // pequeña y el usuario está mirando la pantalla.

    std::wstring ff = EditorFfmpegPath();
    if (ff.empty()) return {};
    const std::wstring tmp = out + L".part.webp";
    DeleteFileW(tmp.c_str());
    // 4 s a 8 fps: se genera rápido y sigue viéndose el movimiento.
    std::wstring cmd = L"\"" + ff + L"\" -y -nostdin -v error -nostats -t 4 -i \"" + src +
                       L"\" -vf \"fps=8,scale=480:-2:flags=lanczos\" "
                       L"-c:v libwebp_anim -lossless 0 -q:v 70 -loop 0 -an \"" +
                       tmp + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mcmd = cmd;
    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return {};
    }
    WaitForSingleObject(pi.hProcess, 300000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code != 0 || GetFileAttributesW(tmp.c_str()) == INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(tmp.c_str());
        return {};
    }
    if (!MoveFileExW(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return {};
    }
    return out;
}

// ---- herramientas: descarga de ffmpeg --------------------------------------
// Si ffmpeg no está en el sistema, se ofrece descargarlo a dist\tools desde el
// build oficial recomendado por ffmpeg.org (gyan.dev).
std::atomic<bool> g_ffmpegDownloading{false};
std::atomic<int> g_ffmpegStage{0};  // 0 nada, 1 descargando, 2 listo, -1 error

DWORD RunHidden(const std::wstring& cmd) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mcmd = cmd;
    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return (DWORD)-1;
    }
    WaitForSingleObject(pi.hProcess, 600000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

std::string FfmpegStatusJson() {
    std::wstring ff = EditorFfmpegPath();
    std::string path = ff.empty() ? "" : WideToUtf8(ff);
    std::string esc;
    for (char c : path) {
        if (c == '\\' || c == '"') esc.push_back('\\');
        esc.push_back(c);
    }
    char b[64];
    sprintf_s(b, ",\"stage\":%d", g_ffmpegStage.load());
    return std::string("{\"ok\":true,\"missing\":") + (ff.empty() ? "true" : "false") +
           ",\"path\":\"" + esc + "\"" + b + "}";
}

void InstallFfmpegAsync() {
    if (g_ffmpegDownloading.exchange(true)) return;
    g_ffmpegStage = 1;
    std::thread([]() {
        const std::wstring tools = g_baseDir + L"\\tools";
        CreateDirectoryW(tools.c_str(), nullptr);
        const std::wstring zip = tools + L"\\ffmpeg.zip";
        const std::wstring tmp = tools + L"\\_ff";
        const std::wstring url =
            L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip";
        std::wstring dl = L"curl.exe -L --fail --silent --show-error -o \"" + zip +
                          L"\" \"" + url + L"\"";
        bool ok = RunHidden(dl) == 0;
        if (ok) {
            std::wstring ps =
                L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" +
                std::wstring(L"Expand-Archive -LiteralPath '") + zip +
                L"' -DestinationPath '" + tmp + L"' -Force; " +
                L"Get-ChildItem -Path '" + tmp +
                L"' -Recurse -Filter ffmpeg.exe | Select-Object -First 1 | "
                L"Copy-Item -Destination '" + tools + L"' -Force; " +
                L"Get-ChildItem -Path '" + tmp +
                L"' -Recurse -Filter ffprobe.exe | Select-Object -First 1 | "
                L"Copy-Item -Destination '" + tools + L"' -Force; " +
                L"Remove-Item '" + zip + L"' -Force -ErrorAction SilentlyContinue; " +
                L"Remove-Item '" + tmp + L"' -Recurse -Force -ErrorAction SilentlyContinue\"";
            ok = RunHidden(ps) == 0;
        }
        if (ok) {
            ok = !EditorFfmpegPath().empty();
        }
        g_ffmpegStage = ok ? 2 : -1;
        g_ffmpegDownloading = false;
    }).detach();
}

// ---- playlist (formato del motor: rutas locales + intervalos) -------------
std::wstring PlaylistFile() {
    return g_baseDir + L"\\res\\config\\playlist.json";
}

int JsonIntField(const std::string& j, const char* key, int def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return def;
    p = j.find(':', p);
    if (p == std::string::npos) return def;
    return atoi(j.c_str() + p + 1);
}

std::vector<std::string> JsonStringArray(const std::string& j, const char* key) {
    std::vector<std::string> out;
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return out;
    size_t a = j.find('[', p);
    size_t b = j.find(']', a);
    if (a == std::string::npos || b == std::string::npos) return out;
    size_t i = a;
    while ((i = j.find('"', i + 1)) != std::string::npos && i < b) {
        std::string v;
        size_t c = i + 1;
        for (; c < b && j[c] != '"'; ++c) {
            if (j[c] == '\\' && c + 1 < b) ++c;
            v.push_back(j[c]);
        }
        out.push_back(v);
        i = c;
    }
    return out;
}

std::string JsonEsc(const std::string& in) {
    std::string o;
    for (unsigned char c : in) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    char b[8];
                    sprintf_s(b, "\\u%04x", c);
                    o += b;
                } else {
                    o.push_back((char)c);
                }
        }
    }
    return o;
}

void WritePlaylist(int intervalSec, int transitionSec,
                   const std::vector<std::string>& items) {
    std::string j = "{\"intervalSeconds\":" + std::to_string(intervalSec) +
                    ",\"transitionSeconds\":" + std::to_string(transitionSec) +
                    ",\"items\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) j += ",";
        j += "\"" + JsonEsc(items[i]) + "\"";
    }
    j += "]}";
    WriteWholeFile(PlaylistFile(), j);
}

void Handle(SOCKET client, bool local) {
    std::string req;
    char buf[8192] = {};
    while (req.find("\r\n\r\n") == std::string::npos) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, (size_t)n);
        if (req.size() > 64 * 1024) break;
    }
    if (req.empty()) {
        closesocket(client);
        return;
    }
    size_t sp1 = req.find(' ');
    size_t sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        closesocket(client);
        return;
    }
    std::string method = req.substr(0, sp1);
    bool isHead = (method == "HEAD");
    std::string path = req.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string range;
    size_t rp = req.find("Range:");
    if (rp != std::string::npos) {
        size_t e = req.find("\r\n", rp);
        range = req.substr(rp + 6, e - rp - 6);
        while (!range.empty() && range.front() == ' ') range.erase(0, 1);
    }

    // Token opcional (?t=...)
    std::string query;
    size_t q = path.find('?');
    if (q != std::string::npos) {
        query = path.substr(q + 1);
        path = path.substr(0, q);
    }
    std::string body;
    // Subida de vídeo/imagen desde el móvil: se escribe a disco en streaming
    // (no se acumula en memoria).
    if (path == "/api/upload" && method == "POST") {
        HandleUpload(client, req, query);
        return;
    }
    {
        size_t hdrEnd = req.find("\r\n\r\n");
        if (hdrEnd != std::string::npos) {
            body = req.substr(hdrEnd + 4);
            std::string lowerReq = req;
            for (auto& c : lowerReq) c = (char)tolower((unsigned char)c);
            size_t clp = lowerReq.find("content-length:");
            if (clp != std::string::npos) {
                size_t cle = lowerReq.find("\r\n", clp);
                long long want = _strtoi64(
                    req.substr(clp + 15, cle - clp - 15).c_str(), nullptr, 10);
                while ((long long)body.size() < want) {
                    char mb[4096];
                    int got = recv(client, mb, sizeof(mb), 0);
                    if (got <= 0) break;
                    body.append(mb, got);
                }
            }
        }
    }

    if (!g_token.empty()) {
        std::string t;
        size_t tp = query.find("t=");
        if (tp != std::string::npos) {
            size_t te = query.find('&', tp);
            t = query.substr(tp + 2, te == std::string::npos ? std::string::npos
                                                             : te - tp - 2);
        }
        if (t != g_token) {
            SendText(client, 403, "Forbidden", "{\"error\":\"token\"}");
            closesocket(client);
            return;
        }
    }

    if (path.rfind("/ui2/", 0) == 0 || path == "/ui2") {
        std::string rel = (path == "/ui2") ? "/index.html" : path.substr(5);
        if (rel.empty() || rel == "/") rel = "/index.html";
        if (rel[0] != '/') rel = "/" + rel;
        if (rel.find("..") != std::string::npos) {
            SendText(client, 404, "Not Found", "{\"error\":\"path\"}");
        } else {
            std::wstring file = g_baseDir + L"\\res\\dnxui" + Utf8ToWide(rel);
            if (!SendFile(client, file, range, false, isHead)) {
                SendText(client, 404, "Not Found", "{\"error\":\"asset\"}");
            }
        }
    } else if (path.rfind("/editor/", 0) == 0 || path == "/editor") {
        std::string rel = (path == "/editor") ? "/index.html"
                                              : path.substr(7);
        if (rel.empty() || rel == "/") rel = "/index.html";
        if (rel[0] != '/') rel = "/" + rel;
        if (rel.find("..") != std::string::npos) {
            SendText(client, 404, "Not Found", "{\"error\":\"path\"}");
        } else {
            std::wstring file = g_baseDir + L"\\res\\editor" +
                                Utf8ToWide(rel);
            if (!SendFile(client, file, range, false, isHead)) {
                SendText(client, 404, "Not Found", "{\"error\":\"asset\"}");
            }
        }
    } else if (path == "/api/editor/current") {
        SendText(client, 200, "OK", EditorCurrentJson(g_baseDir));
    } else if (path == "/api/editor/lan") {
        SendText(client, 200, "OK", LanInfoJson());
    } else if (path == "/api/editor/info") {
        std::string file;
        size_t fp = query.find("file=");
        if (fp != std::string::npos) {
            size_t fe = query.find('&', fp);
            file = UrlDecode(query.substr(fp + 5,
                fe == std::string::npos ? std::string::npos : fe - fp - 5));
        }
        SendText(client, 200, "OK", EditorProbeJson(g_baseDir, file, local));
    } else if (path == "/api/editor/file") {
        std::string file;
        size_t fp = query.find("file=");
        if (fp != std::string::npos) {
            size_t fe = query.find('&', fp);
            file = UrlDecode(query.substr(fp + 5,
                fe == std::string::npos ? std::string::npos : fe - fp - 5));
        }
        std::wstring full;
        if (!EditorResolveSource(g_baseDir, file, full, local) ||
            !SendFile(client, full, range, false, isHead)) {
            SendText(client, 404, "Not Found", "{\"error\":\"source\"}");
        }
    } else if (path == "/api/editor/export" && method == "POST") {
        // El móvil no puede mandar rutas: identifica el wallpaper por su clave
        // del catálogo y el servidor resuelve la ruta local.
        std::string body2 = body;
        bool allow = local;
        std::string key = JsonField(body, "key");
        if (!key.empty()) {
            std::wstring p = RemoteMediaPath(g_baseDir, key);
            if (p.empty() && key.rfind("up:", 0) == 0) {
                p = UploadPath(key.substr(3));
            }
            if (p.empty()) {
                // Aun no descargado: el editor necesita el fichero en disco.
                // Sin esto, exportar un item remoto nuevo fallaba con
                // "source" (la app mostraba "No se pudo iniciar la
                // exportacion").
                if (FetchRemoteMediaSync(g_baseDir, key, 45000)) {
                    p = RemoteMediaPath(g_baseDir, key);
                }
            }
            if (!p.empty()) {
                // Fuentes grandes: se exporta desde el proxy 1080p. Evita
                // decodificar 4K (memoria) y el resultado nunca pasa de 1080p.
                if (body.find("\"proxy\":true") != std::string::npos &&
                    key.rfind("up:", 0) != 0) {
                    std::wstring proxy = EnsureMobileProxy(g_baseDir, key);
                    if (!proxy.empty()) p = proxy;
                }
                body2 = JsonSetField(body, "file", WideToUtf8(p));
                allow = true;
            }
        }
        SendText(client, 200, "OK", StartEditorExport(g_baseDir, body2, allow));
    } else if (path.rfind("/api/editor/status/", 0) == 0) {
        std::string id = UrlDecode(path.substr(19));
        SendText(client, 200, "OK", EditorExportStatusJson(id));
    } else if (path.rfind("/api/editor/cancel/", 0) == 0 &&
               method == "POST") {
        std::string id = UrlDecode(path.substr(19));
        bool ok = CancelEditorExport(id);
        SendText(client, ok ? 200 : 404, ok ? "OK" : "Not Found",
                 ok ? "{\"cancelled\":true}" : "{\"error\":\"no job\"}");
    } else if (path.rfind("/api/mobile/", 0) == 0) {
        std::string id = UrlDecode(path.substr(12));
        bool okId = id.size() >= 8 && id.size() <= 20;
        for (char c : id) {
            if (!isxdigit((unsigned char)c)) okId = false;
        }
        if (!okId) {
            SendText(client, 404, "Not Found", "{\"error\":\"id\"}");
        } else {
            std::wstring meta = g_baseDir + L"\\res\\mobile\\" +
                                Utf8ToWide(id) + L".json";
            std::string json;
            FILE* f = nullptr;
            _wfopen_s(&f, meta.c_str(), L"rb");
            if (f) {
                char mb[8192];
                size_t got = fread(mb, 1, sizeof(mb) - 1, f);
                fclose(f);
                json.assign(mb, got);
            }
            if (json.empty()) {
                SendText(client, 404, "Not Found", "{\"error\":\"mobile\"}");
            } else {
                SendText(client, 200, "OK", json);
            }
        }
    } else if (path.rfind("/up/", 0) == 0) {
        std::string rel = UrlDecode(path.substr(4));
        if (rel.find("..") != std::string::npos || rel.empty()) {
            SendText(client, 404, "Not Found", "{\"error\":\"path\"}");
        } else {
            std::wstring file = g_baseDir + L"\\res\\uploads\\" +
                                Utf8ToWide(rel);
            if (!SendFile(client, file, range, false, isHead)) {
                SendText(client, 404, "Not Found", "{\"error\":\"asset\"}");
            }
        }
    } else if (path.rfind("/m/", 0) == 0) {
        std::string rest = UrlDecode(path.substr(3));
        bool jpg = false;
        if (rest.size() > 4 && rest.rfind(".jpg") == rest.size() - 4) {
            jpg = true;
            rest = rest.substr(0, rest.size() - 4);
        }
        bool okId = rest.size() >= 8 && rest.size() <= 20;
        for (char c : rest) {
            if (!isxdigit((unsigned char)c)) okId = false;
        }
        if (!okId) {
            SendText(client, 404, "Not Found", "{\"error\":\"id\"}");
        } else {
            std::wstring file = g_baseDir + L"\\res\\mobile\\" +
                                Utf8ToWide(rest) + (jpg ? L".jpg" : L".mp4");
            bool inlineMedia = jpg || query.find("inline=1") != std::string::npos;
            if (!SendFile(client, file, range, !inlineMedia, isHead)) {
                SendText(client, 404, "Not Found", "{\"error\":\"export\"}");
            }
        }
    } else if (path == "/api/info") {
        wchar_t pc[256] = {};
        DWORD sz = 255;
        GetComputerNameW(pc, &sz);
        char body[2048];
        sprintf_s(body,
                  "{\"product\":\"%s\",\"instanceId\":\"%s\","
                  "\"name\":\"%s\",\"port\":%d,\"version\":\"1.0\","
                  "\"auth\":%s,\"items\":%d,\"interfaces\":%s}",
                  kProduct, g_instanceId.c_str(), WideToUtf8(pc).c_str(), g_port,
                  g_token.empty() ? "false" : "true",
                  (int)GetRemoteRegistryCopy().size(),
                  InterfacesJson().c_str());
        SendText(client, 200, "OK", body);
    } else if (path == "/api/net/interfaces") {
        std::string body = "{\"port\":" + std::to_string(g_port) +
                           ",\"product\":\"" + kProduct +
                           "\",\"interfaces\":" + InterfacesJson() + "}";
        SendText(client, 200, "OK", body);
    } else if (path == "/api/current") {
        std::string cur = ReadWholeFile(g_baseDir + L"\\res\\current.json");
        // El fichero se escribe con BOM; un JSON no lo admite.
        if (cur.size() >= 3 && (unsigned char)cur[0] == 0xEF &&
            (unsigned char)cur[1] == 0xBB && (unsigned char)cur[2] == 0xBF) {
            cur = cur.substr(3);
        }
        if (cur.empty()) cur = "{\"project\":\"\"}";
        // Se anade la clave del catalogo (si el fichero es de un item) para que
        // la UI pueda usar /dl/<key> como preview animado.
        const std::string proj = JsonField(cur, "project");
        if (!proj.empty()) {
            std::string key;
            std::string mediaType = "image";
            for (const auto& it : GetRemoteRegistryCopy()) {
                std::wstring p = RemoteMediaPath(g_baseDir, it.key);
                if (!p.empty() && WideToUtf8(p) == proj) {
                    key = it.key;
                    mediaType = it.mediaType;
                    break;
                }
            }
            if (!key.empty()) {
                cur.pop_back();  // quita '}' final
                cur += ",\"key\":\"" + key + "\",\"mediaType\":\"" +
                       mediaType + "\"}";
            }
        }
        SendText(client, 200, "OK", cur);
    } else if (path == "/api/plugins") {
        SendText(client, 200, "OK", BuildPluginListJson());
    } else if (path.rfind("/api/plugin/", 0) == 0 &&
               path.find("/config") != std::string::npos) {
        // Configuración de un plugin (por ahora: lista de carpetas del local).
        std::string id = path.substr(12);
        const size_t cpos = id.find("/config");
        if (cpos != std::string::npos) id = id.substr(0, cpos);
        bool known = false;
        for (const auto& k : KnownPluginIds()) {
            if (k == id) { known = true; break; }
        }
        if (!known || id.empty() || id.find("..") != std::string::npos) {
            SendText(client, 404, "Not Found", "{\"ok\":false}");
        } else {
            const std::wstring dir = g_baseDir + L"\\plugins\\" + Utf8ToWide(id);
            const std::wstring cfgFile = dir + L"\\config.json";
            if (method == "POST") {
                WriteWholeFile(cfgFile, body);
                // Forzar reindexado del plugin (caché de 5 min).
                DeleteFileW((dir + L"\\index.json").c_str());
                SendText(client, 200, "OK", "{\"ok\":true}");
            } else {
                std::string cur = ReadWholeFile(cfgFile);
                if (cur.empty()) {
                    // Valores por defecto razonables: Videos\Wallpapers del usuario.
                    std::string def;
                    if (const char* up = getenv("USERPROFILE")) {
                        std::string cand = std::string(up) + "\\Videos\\Wallpapers";
                        if (GetFileAttributesW(Utf8ToWide(cand).c_str()) !=
                            INVALID_FILE_ATTRIBUTES) {
                            def = cand;
                        }
                    }
                    std::string esc2;
                    for (char c : def) {
                        if (c == '\\' || c == '"') esc2.push_back('\\');
                        esc2.push_back(c);
                    }
                    cur = def.empty() ? "{\"folders\":[]}"
                                      : "{\"folders\":[\"" + esc2 + "\"]}";
                }
                SendText(client, 200, "OK", cur);
            }
        }
    } else if (path == "/api/library/randomize") {
        // Barajado por página: cada plugin aporta una página distinta.
        SendText(client, 200, "OK", RandomizeLibraryJson(g_baseDir, 60, 15));
    } else if (path == "/api/monitors") {
        std::string body = "{\"monitors\":" + MonitorsJson() + "}";
        SendText(client, 200, "OK", body);
    } else if (path == "/api/settings") {
        // Ajustes de la UI (salvapantallas, encuadre...). Se guardan tal cual.
        const std::wstring f = g_baseDir + L"\\res\\config\\settings.json";
        if (method == "POST") {
            WriteWholeFile(f, body);
            // Salvapantallas: el motor no conoce el catálogo, así que aquí se
            // resuelve la clave elegida a una ruta local y se deja en su
            // fichero de configuración.
            size_t sp = body.find("\"screensaver\"");
            if (sp != std::string::npos) {
                const std::string skey = JsonStrIn(body, sp, "key", "");
                const std::wstring cfg =
                    g_baseDir + L"\\res\\config\\screensaver.json";
                auto writeCfg = [cfg](const std::string& key,
                                      const std::wstring& file) {
                    const std::string p8 = WideToUtf8(file);
                    std::string esc;
                    for (char c : p8) {
                        if (c == '\\' || c == '"') esc.push_back('\\');
                        esc.push_back(c);
                    }
                    std::string j = "{\"key\":\"" + key + "\",\"file\":\"" + esc +
                                    "\"}";
                    WriteWholeFile(cfg, j);
                };
                std::wstring file = skey.empty()
                                        ? std::wstring()
                                        : RemoteMediaPath(g_baseDir, skey);
                if (!file.empty()) {
                    writeCfg(skey, file);
                } else if (!skey.empty()) {
                    // Aún no descargado: se resuelve en segundo plano.
                    const std::wstring base = g_baseDir;
                    std::thread([base, skey, writeCfg]() {
                        if (FetchRemoteMediaSync(base, skey, 300000)) {
                            std::wstring p = RemoteMediaPath(base, skey);
                            if (!p.empty()) writeCfg(skey, p);
                        }
                    }).detach();
                } else {
                    writeCfg("", L"");
                }
            }
            // El ajuste (fit) lo aplica el motor: se refleja en playback.json y
            // se le avisa para que lo relea.
            size_t fp = body.find("\"fit\"");
            if (fp != std::string::npos) {
                size_t q1 = body.find('"', body.find(':', fp));
                size_t q2 = (q1 == std::string::npos) ? q1 : body.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    std::string fit = body.substr(q1 + 1, q2 - q1 - 1);
                    std::string mode = "fill";
                    if (fit == "stretch") mode = "stretch";
                    else if (fit == "center" || fit == "fit") mode = "fit";
                    const std::wstring pb =
                        g_baseDir + L"\\res\\config\\playback.json";
                    std::string cfg = ReadWholeFile(pb);
                    if (cfg.empty()) {
                        cfg = "{\"mode\":\"" + mode + "\",\"audio\":false}";
                    } else {
                        size_t m = cfg.find("\"mode\"");
                        if (m != std::string::npos) {
                            size_t a = cfg.find('"', cfg.find(':', m));
                            size_t b = (a == std::string::npos) ? a : cfg.find('"', a + 1);
                            if (a != std::string::npos && b != std::string::npos) {
                                cfg = cfg.substr(0, a + 1) + mode + cfg.substr(b);
                            }
                        } else {
                            cfg.pop_back();
                            cfg += ",\"mode\":\"" + mode + "\"}";
                        }
                    }
                    // Solo se avisa al motor si el ajuste cambia de verdad:
                    // antes, cualquier guardado de la UI (modo, selección de
                    // wallpaper) reescribía playback.json y lanzaba la recarga,
                    // y parecía que se estaba aplicando solo.
                    const bool changed = ReadWholeFile(pb) != cfg;
                    if (changed) {
                        WriteWholeFile(pb, cfg);
                        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE,
                                                 L"dnxWallpaperV3Reload");
                        if (ev) {
                            SetEvent(ev);
                            CloseHandle(ev);
                        }
                    }
                }
            }
            SendText(client, 200, "OK", "{\"ok\":true}");
        } else {
            std::string cur = ReadWholeFile(f);
            if (cur.size() >= 3 && (unsigned char)cur[0] == 0xEF &&
                (unsigned char)cur[1] == 0xBB && (unsigned char)cur[2] == 0xBF) {
                cur = cur.substr(3);
            }
            SendText(client, 200, "OK", cur.empty() ? "{}" : cur);
        }
    } else if (path == "/api/plugins/toggle") {
        size_t ip = query.find("id=");
        std::string id;
        if (ip != std::string::npos) {
            id = UrlDecode(query.substr(ip + 3));
            size_t amp = id.find('&');
            if (amp != std::string::npos) id = id.substr(0, amp);
        }
        const bool enable = query.find("enabled=1") != std::string::npos;
        if (id.empty()) {
            SendText(client, 400, "Bad Request", "{\"ok\":false}");
        } else {
            SetPluginDisabled(id, !enable);
            // Al desactivar, sus items salen del catalogo de inmediato.
            if (!enable) RemoveProvider(id);
            SendText(client, 200, "OK", "{\"ok\":true}");
        }
    } else if (path == "/api/playlist/add") {
        // Se resuelve la clave a fichero local (descargando si hace falta) y se
        // anade al formato que ya usa el motor.
        std::string key;
        size_t kp = query.find("key=");
        if (kp != std::string::npos) key = UrlDecode(query.substr(kp + 4));
        std::wstring file = RemoteMediaPath(g_baseDir, key);
        if (file.empty()) {
            if (FetchRemoteMediaSync(g_baseDir, key, 120000)) {
                file = RemoteMediaPath(g_baseDir, key);
            }
        }
        if (file.empty()) {
            SendText(client, 404, "Not Found", "{\"ok\":false,\"error\":\"item\"}");
        } else {
            std::string cur = ReadWholeFile(PlaylistFile());
            int iv = JsonIntField(cur, "intervalSeconds", 45);
            int tr = JsonIntField(cur, "transitionSeconds", 2);
            auto items = JsonStringArray(cur, "items");
            const std::string p8 = WideToUtf8(file);
            if (std::find(items.begin(), items.end(), p8) == items.end()) {
                items.push_back(p8);
            }
            WritePlaylist(iv, tr, items);
            SendText(client, 200, "OK", "{\"ok\":true}");
        }
    } else if (path == "/api/playlist/remove") {
        std::string cur = ReadWholeFile(PlaylistFile());
        int iv = JsonIntField(cur, "intervalSeconds", 45);
        int tr = JsonIntField(cur, "transitionSeconds", 2);
        auto items = JsonStringArray(cur, "items");
        size_t idx = 0;
        size_t ip = query.find("index=");
        if (ip != std::string::npos) idx = (size_t)atoi(query.c_str() + ip + 6);
        if (idx < items.size()) items.erase(items.begin() + idx);
        WritePlaylist(iv, tr, items);
        SendText(client, 200, "OK", "{\"ok\":true}");
    } else if (path == "/api/playlist/activate") {
        // El motor activa la rotación cuando current.json lleva
        // "playlist": true; la lista y el intervalo viven en playlist.json.
        std::string cur = ReadWholeFile(PlaylistFile());
        auto items = JsonStringArray(cur, "items");
        if (items.empty()) {
            SendText(client, 400, "Bad Request",
                     "{\"ok\":false,\"error\":\"playlist vacia\"}");
        } else {
            const std::wstring f = g_baseDir + L"\\res\\current.json";
            std::string existing = ReadWholeFile(f);
            if (existing.find("\"displays\"") != std::string::npos) {
                // Asignación por monitor: se conserva y se le añade la rotación
                // (el motor rota cada pantalla dentro de la playlist).
                size_t end = existing.find_last_of('}');
                if (end != std::string::npos) {
                    existing.insert(end, ",\"playlist\":true");
                    WriteWholeFile(f, existing);
                } else {
                    WriteWholeFile(f, "{\"project\": \"\", \"playlist\": true}");
                }
            } else {
                WriteWholeFile(f, "{\"project\": \"\", \"playlist\": true}");
            }
            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE,
                                     L"dnxWallpaperV3Reload");
            if (ev) {
                SetEvent(ev);
                CloseHandle(ev);
            }
            SendText(client, 200, "OK", "{\"ok\":true}");
        }
    } else if (path == "/api/playlist/interval") {
        // Cambiar el intervalo sin tocar la lista (formato del motor).
        std::string cur = ReadWholeFile(PlaylistFile());
        int tr = JsonIntField(cur, "transitionSeconds", 2);
        auto items = JsonStringArray(cur, "items");
        int iv = 45;
        size_t sp = query.find("seconds=");
        if (sp != std::string::npos) iv = atoi(query.c_str() + sp + 8);
        if (iv < 3) iv = 3;
        if (iv > 3600) iv = 3600;
        WritePlaylist(iv, tr, items);
        SendText(client, 200, "OK", "{\"ok\":true,\"seconds\":" + std::to_string(iv) + "}");
    } else if (path == "/api/playlist/stop") {
        // Detener la rotación. Si hay asignación por monitor se conserva
        // (sólo se quita la marca de playlist); si no, queda el primer
        // elemento como fondo fijo.
        const std::wstring f = g_baseDir + L"\\res\\current.json";
        std::string existing = ReadWholeFile(f);
        auto stripFlag = [](std::string j) {
            for (const char* pat : {",\"playlist\":true", ", \"playlist\": true",
                                    ",\"playlist\": true", ", \"playlist\":true"}) {
                size_t p = j.find(pat);
                if (p != std::string::npos) {
                    j.erase(p, strlen(pat));
                    break;
                }
            }
            return j;
        };
        if (existing.find("\"displays\"") != std::string::npos) {
            WriteWholeFile(f, stripFlag(existing));
            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE,
                                     L"dnxWallpaperV3Reload");
            if (ev) {
                SetEvent(ev);
                CloseHandle(ev);
            }
            SendText(client, 200, "OK", "{\"ok\":true}");
            return;
        }
        auto items = JsonStringArray(ReadWholeFile(PlaylistFile()), "items");
        if (items.empty()) {
            SendText(client, 200, "OK", "{\"ok\":true}");
        } else {
            std::string esc;
            for (char c : items[0]) {
                if (c == '\\' || c == '"') esc.push_back('\\');
                esc.push_back(c);
            }
            WriteWholeFile(f, "{\"project\": \"" + esc + "\"}");
            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE,
                                     L"dnxWallpaperV3Reload");
            if (ev) {
                SetEvent(ev);
                CloseHandle(ev);
            }
            SendText(client, 200, "OK", "{\"ok\":true}");
        }
    } else if (path == "/api/playlist") {
        // Playlist de rotacion: lista ordenada de claves del catalogo.
        const std::wstring f = g_baseDir + L"\\res\\config\\playlist.json";
        if (method == "POST") {
            WriteWholeFile(f, body);
            SendText(client, 200, "OK", "{\"ok\":true}");
        } else {
            std::string cur = ReadWholeFile(f);
            SendText(client, 200, "OK",
                     cur.empty() ? "{\"items\":[],\"intervalMin\":30,\"enabled\":false}"
                                 : cur);
        }
    } else if (path == "/api/screensaver/install") {
        // Registra el motor como salvapantallas de Windows (sin diálogos).
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        dir = dir.substr(0, dir.find_last_of(L"\\/"));
        const std::wstring src = dir + L"\\DNXWallpaper.Motor.exe";
        const std::wstring scr = dir + L"\\DNXWallpaper.scr";
        bool ok = CopyFileW(src.c_str(), scr.c_str(), FALSE) != 0;
        // Espera configurada en la UI (minutos).
        int wait = 10;
        {
            const std::string cfg =
                ReadWholeFile(g_baseDir + L"\\res\\config\\settings.json");
            size_t sp = cfg.find("\"screensaver\"");
            if (sp != std::string::npos) {
                wait = (int)JsonNumIn(cfg, sp, "wait", 10);
            }
        }
        if (wait < 1) wait = 1;
        if (wait > 600) wait = 600;
        HKEY k = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0,
                            nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) ==
            ERROR_SUCCESS) {
            RegSetValueExW(k, L"SCRNSAVE.EXE", 0, REG_SZ, (const BYTE*)scr.c_str(),
                           (DWORD)((scr.size() + 1) * sizeof(wchar_t)));
            RegSetValueExW(k, L"ScreenSaveActive", 0, REG_SZ, (const BYTE*)L"1", 4);
            wchar_t t[16]{};
            swprintf_s(t, L"%d", wait * 60);
            RegSetValueExW(k, L"ScreenSaveTimeOut", 0, REG_SZ, (const BYTE*)t,
                           (DWORD)((wcslen(t) + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
            ok = true;
        }
        SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, TRUE, nullptr, 0);
        SystemParametersInfoW(SPI_SETSCREENSAVETIMEOUT, (UINT)(wait * 60),
                              nullptr, 0);
        SendText(client, 200, "OK",
                 std::string("{\"ok\":") + (ok ? "true" : "false") +
                     ",\"minutes\":" + std::to_string(wait) + "}");
    } else if (path.rfind("/api/apply", 0) == 0) {
        size_t k = query.find("key=");
        if (k == std::string::npos) {
            SendText(client, 400, "Bad Request", "{\"error\":\"falta key\"}");
        } else {
            std::string key = UrlDecode(query.substr(k + 4));
            size_t amp = key.find('&');
            if (amp != std::string::npos) key = key.substr(0, amp);
            std::string err;
            if (ApplyCatalogItem(g_baseDir, key, &err)) {
                SendText(client, 200, "OK",
                         "{\"ok\":true,\"key\":\"" + key + "\"}");
            } else {
                SendText(client, 404, "Not Found",
                         "{\"ok\":false,\"error\":\"" + err + "\"}");
            }
        }
    } else if (path == "/api/current/thumb") {
        // Miniatura del wallpaper activo (para el mapa de pantallas).
        std::string cur = ReadWholeFile(g_baseDir + L"\\res\\current.json");
        if (cur.size() >= 3 && (unsigned char)cur[0] == 0xEF &&
            (unsigned char)cur[1] == 0xBB && (unsigned char)cur[2] == 0xBF) {
            cur = cur.substr(3);
        }
        std::string file = JsonField(cur, "project");
        std::wstring f = Utf8ToWide(file);
        if (f.empty() ||
            GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) {
            SendText(client, 404, "Not Found", "{\"error\":\"sin fondo\"}");
        } else {
            // Póster junto al fichero (lo genera ffmpeg la primera vez).
            std::wstring poster = f;
            size_t dot = poster.find_last_of(L'.');
            if (dot != std::wstring::npos) poster = poster.substr(0, dot);
            poster += L".jpg";
            if (GetFileAttributesW(poster.c_str()) == INVALID_FILE_ATTRIBUTES) {
                int w = 0, h = 0;
                double d = 0;
                std::wstring out;
                EditorProbeFile(f, out, w, h, d);
                if (!out.empty()) poster = out;
            }
            if (GetFileAttributesW(poster.c_str()) != INVALID_FILE_ATTRIBUTES &&
                SendFile(client, poster, range, false, isHead)) {
                // servido
            } else if (!SendFile(client, f, range, false, isHead)) {
                SendText(client, 404, "Not Found", "{\"error\":\"sin miniatura\"}");
            }
        }
    } else if (path == "/api/probe") {
        // Dimensiones reales de un item del catálogo (para el editor de
        // pantallas: saber la proporción del vídeo).
        std::string key;
        size_t kp = query.find("key=");
        if (kp != std::string::npos) key = UrlDecode(query.substr(kp + 4));
        std::wstring file = RemoteMediaPath(g_baseDir, key);
        // Sin descargar: sondear es para el editor y no debe bloquear la UI
        // bajando un 4K entero.
        int w = 0, h = 0;
        double d = 0;
        if (!file.empty() && EditorProbeDims(file, w, h, d)) {
            char b[160];
            sprintf_s(b, "{\"ok\":true,\"width\":%d,\"height\":%d,\"duration\":%.2f}",
                      w, h, d);
            SendText(client, 200, "OK", b);
        } else {
            SendText(client, 404, "Not Found", "{\"ok\":false}");
        }
    } else if (path == "/api/compose" && method == "POST") {
        int w = 0, h = 0;
        SendText(client, 200, "OK", ComposeWorkspace(body, &w, &h));
    } else if (path == "/api/mosaic/random") {
        int count = 4;
        size_t cq = query.find("count=");
        if (cq != std::string::npos) count = atoi(query.c_str() + cq + 6);
        std::string exclude;
        size_t eq = query.find("exclude=");
        if (eq != std::string::npos) {
            exclude = UrlDecode(query.substr(eq + 8));
            size_t amp = exclude.find('&');
            if (amp != std::string::npos) exclude = exclude.substr(0, amp);
        }
        SendText(client, 200, "OK", MosaicRandomJson(count, exclude));
    } else if (path == "/api/mosaic" && method == "POST") {
        SendText(client, 200, "OK", MosaicWorkspace(body));
    } else if (path == "/api/mosaic/project") {
        const std::wstring f = g_baseDir + L"\\res\\config\\mosaic.json";
        if (method == "POST") {
            CreateDirectoryW((g_baseDir + L"\\res\\config").c_str(), nullptr);
            WriteWholeFile(f, body);
            SendText(client, 200, "OK", "{\"ok\":true}");
        } else {
            std::string cur = ReadWholeFile(f);
            SendText(client, 200, "OK", cur.empty() ? "{}" : cur);
        }
    } else if (path == "/api/progress") {
        SendText(client, 200, "OK", ProgressJson());
    } else if (path == "/api/prepare" && method == "POST") {
        SendText(client, 200, "OK", PrepareCell(body));
    } else if (path == "/api/prepare/cancel" ) {
        std::string slot;
        size_t sp = query.find("slot=");
        if (sp != std::string::npos) {
            slot = UrlDecode(query.substr(sp + 5));
            size_t amp = slot.find('&');
            if (amp != std::string::npos) slot = slot.substr(0, amp);
        }
        CancelSlotPrep(slot);
        SendText(client, 200, "OK", "{\"ok\":true}");
    } else if (path == "/api/prepare/status") {
        SendText(client, 200, "OK", PrepStatusJson());
    } else if (path == "/api/optimize/apply") {
        // La exportación optimizada vive en res\mobile\<id>.mp4. Se registra
        // como item del catálogo (provider "optimized") y se aplica, para que
        // el fondo use la versión recortada y ligera en vez del original.
        std::string id;
        size_t ip = query.find("id=");
        if (ip != std::string::npos) {
            id = UrlDecode(query.substr(ip + 3));
            size_t amp = id.find('&');
            if (amp != std::string::npos) id = id.substr(0, amp);
        }
        if (id.empty() || id.find("..") != std::string::npos) {
            SendText(client, 400, "Bad Request", "{\"ok\":false}");
        } else {
            std::wstring src = g_baseDir + L"\\res\\mobile\\" + Utf8ToWide(id) +
                               L".mp4";
            if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
                SendText(client, 404, "Not Found", "{\"ok\":false,\"error\":\"job\"}");
            } else {
                std::wstring dir = g_baseDir + L"\\res\\providers\\optimized\\media";
                CreateDirectoryW((g_baseDir + L"\\res\\providers").c_str(), nullptr);
                CreateDirectoryW(
                    (g_baseDir + L"\\res\\providers\\optimized").c_str(), nullptr);
                CreateDirectoryW(dir.c_str(), nullptr);
                std::wstring dst = dir + L"\\opt-" + Utf8ToWide(id) + L".mp4";
                CopyFileW(src.c_str(), dst.c_str(), FALSE);

                RemoteItem it;
                it.provider = "optimized";
                it.id = "opt-" + id;
                it.key = "optimized:opt-" + id;
                it.title = "Optimizado " + id;
                it.mediaType = "video";
                it.animated = true;
                it.mediaUrl = WpxFileUrl(dst);
                it.downloadable = true;
                it.tags = "optimizado";

                SYSTEMTIME st{};
                GetSystemTime(&st);
                char ts[40];
                sprintf_s(ts, "%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear,
                          st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                it.publishedAt = ts;

                MergeRemoteItems(std::vector<RemoteItem>{it});
                SaveCatalogSnapshot(g_baseDir, GetRemoteRegistryCopy());

                std::string err;
                if (ApplyCatalogItem(g_baseDir, it.key, &err)) {
                    SendText(client, 200, "OK",
                             "{\"ok\":true,\"key\":\"" + it.key + "\"}");
                } else {
                    SendText(client, 200, "OK",
                             "{\"ok\":true,\"key\":\"" + it.key +
                                 "\",\"applyError\":\"" + err + "\"}");
                }
            }
        }
    } else if (path == "/api/library") {
        size_t off = 0, lim = 500;
        size_t o = query.find("offset=");
        if (o != std::string::npos) off = (size_t)atoi(query.c_str() + o + 7);
        size_t l = query.find("limit=");
        if (l != std::string::npos) lim = (size_t)atoi(query.c_str() + l + 6);

        auto param = [&](const char* name) -> std::string {
            const std::string k = std::string(name) + "=";
            size_t p = query.find(k);
            if (p == std::string::npos) return {};
            std::string v = query.substr(p + k.size());
            size_t amp = v.find('&');
            if (amp != std::string::npos) v = v.substr(0, amp);
            return UrlDecode(v);
        };
        const std::string fq = param("q");
        const std::string fprov = param("provider");
        const std::string ftype = param("type");
        const std::string forient = param("orient");
        const int fminh = atoi(param("minh").c_str());

        std::string needle;
        for (char c : fq) needle.push_back((char)tolower((unsigned char)c));

        auto all = GetRemoteRegistryCopy();
        // Los "optimized" son resultados internos (escritorios compuestos y
        // exportaciones para el móvil): no son wallpapers de origen.
        all.erase(std::remove_if(all.begin(), all.end(),
                                 [](const RemoteItem& it) {
                                     return it.provider == "optimized";
                                 }),
                  all.end());
        if (!needle.empty() || !fprov.empty() || !ftype.empty() ||
            !forient.empty() || fminh > 0) {
            all.erase(std::remove_if(all.begin(), all.end(),
                                     [&](const RemoteItem& it) {
                if (!fprov.empty() && it.provider != fprov) return true;
                if (!ftype.empty()) {
                    const bool isVideo = it.mediaType == "video";
                    if (ftype == "video" && !isVideo) return true;
                    if (ftype == "image" && isVideo) return true;
                }
                if (!forient.empty()) {
                    if (it.width <= 0 || it.height <= 0) return true;
                    if (forient == "portrait" && it.height <= it.width) return true;
                    if (forient == "landscape" && it.width <= it.height) return true;
                }
                if (fminh > 0) {
                    if (it.height <= 0 || it.height < fminh) return true;
                }
                if (!needle.empty()) {
                    std::string t;
                    t.reserve(it.title.size());
                    for (char c : it.title) {
                        t.push_back((char)tolower((unsigned char)c));
                    }
                    if (t.find(needle) == std::string::npos) return true;
                }
                return false;
            }), all.end());
        }
        // El registro esta ordenado por clave, asi que una pagina salia con un
        // solo plugin (todos los "lively" primero). Se intercalan proveedores
        // para que cada pagina traiga variedad y los filtros sirvan.
        {
            std::map<std::string, std::vector<RemoteItem>> byProvider;
            std::vector<std::string> order;
            for (const auto& it : all) {
                if (byProvider.find(it.provider) == byProvider.end()) {
                    order.push_back(it.provider);
                }
                byProvider[it.provider].push_back(it);
            }
            std::vector<RemoteItem> mixed;
            mixed.reserve(all.size());
            for (size_t i = 0;; ++i) {
                bool any = false;
                for (const auto& p : order) {
                    const auto& v = byProvider[p];
                    if (i < v.size()) {
                        mixed.push_back(v[i]);
                        any = true;
                    }
                }
                if (!any) break;
            }
            all.swap(mixed);
        }
        std::vector<RemoteItem> page;
        for (size_t i = off; i < all.size() && page.size() < lim; ++i) {
            page.push_back(all[i]);
        }
        SendText(client, 200, "OK", BuildRemoteItemsJson(page));
    } else if (path == "/api/search") {
        // Búsqueda en vivo: pregunta a cada plugin por el término (Enter en la
        // caja de búsqueda). Devuelve el mismo formato que /api/library.
        auto param = [&](const char* name) -> std::string {
            const std::string k = std::string(name) + "=";
            size_t p = query.find(k);
            if (p == std::string::npos) return {};
            std::string v = query.substr(p + k.size());
            size_t amp = v.find('&');
            if (amp != std::string::npos) v = v.substr(0, amp);
            return UrlDecode(v);
        };
        const std::string term = param("q");
        const std::string only = param("provider");
        int per = 10;
        {
            const std::string l = param("limit");
            if (!l.empty()) per = atoi(l.c_str());
            if (per < 1) per = 1;
            if (per > 40) per = 40;
        }
        if (term.empty()) {
            SendText(client, 200, "OK", "[]");
        } else {
            std::vector<RemoteItem> all;
            std::mutex m;
            std::vector<std::thread> threads;
            for (const auto& id : KnownPluginIds()) {
                if (!only.empty() && id != only) continue;
                threads.emplace_back([&, id]() {
                    std::vector<RemoteItem> items =
                        SearchPluginItems(g_baseDir, id, term, per);
                    std::lock_guard<std::mutex> lk(m);
                    all.insert(all.end(), items.begin(), items.end());
                });
            }
            for (auto& t : threads) t.join();
            // Sin duplicados y mezclando plugins (si no, salen en bloques).
            std::map<std::string, std::vector<RemoteItem>> byProvider;
            std::vector<std::string> order;
            for (auto& it : all) {
                if (byProvider.find(it.provider) == byProvider.end()) {
                    order.push_back(it.provider);
                }
                auto& v = byProvider[it.provider];
                bool dup = false;
                for (const auto& x : v) {
                    if (x.key == it.key) { dup = true; break; }
                }
                if (!dup) v.push_back(it);
            }
            std::vector<RemoteItem> mixed;
            for (size_t i = 0;; ++i) {
                bool any = false;
                for (const auto& p : order) {
                    const auto& v = byProvider[p];
                    if (i < v.size()) { mixed.push_back(v[i]); any = true; }
                }
                if (!any) break;
            }
            // Se registran para que /dl, /api/thumb y Aplicar los resuelvan.
            MergeRemoteItems(mixed);
            SendText(client, 200, "OK", BuildRemoteItemsJson(mixed));
        }
    } else if (path == "/api/library/refresh") {
        // Recarga profunda en segundo plano (varias páginas por plugin).
        int pages = 3;
        size_t pp = query.find("pages=");
        if (pp != std::string::npos) pages = atoi(query.c_str() + pp + 6);
        if (pages < 1) pages = 1;
        if (pages > 10) pages = 10;
        if (DeepScanRunning()) {
            SendText(client, 200, "OK", "{\"ok\":true,\"running\":true}");
        } else {
            const std::wstring base = g_baseDir;
            std::thread([base, pages]() { RefreshCatalogDeep(base, pages); })
                .detach();
            SendText(client, 200, "OK", "{\"ok\":true,\"started\":true}");
        }
    } else if (path == "/api/library/refresh/status") {
        SendText(client, 200, "OK", DeepScanStatusJson());
    } else if (path == "/api/tools/ffmpeg") {
        if (method == "POST") {
            InstallFfmpegAsync();
            SendText(client, 200, "OK", "{\"ok\":true,\"started\":true}");
        } else {
            SendText(client, 200, "OK", FfmpegStatusJson());
        }
    } else if (path.rfind("/dl/", 0) == 0) {
        std::string key = UrlDecode(path.substr(4));
        const bool wantMobile = query.find("m=1") != std::string::npos;
        const bool wantPreview = query.find("p=1") != std::string::npos;
        std::wstring file = RemoteMediaPath(g_baseDir, key);
        if (file.empty() && !isHead) {
            // Aún no descargado (y es una descarga real): se baja ahora.
            if (FetchRemoteMediaSync(g_baseDir, key, 120000)) {
                file = RemoteMediaPath(g_baseDir, key);
            }
        }
        if (!file.empty() && wantPreview && !isHead) {
            // Copia ligera para la vista previa (con la GPU desactivada, dos
            // 4K en software no se sostienen y uno se queda congelado).
            std::wstring light = EnsurePreviewProxy(g_baseDir, key);
            if (!light.empty()) file = light;
        }
        if (!file.empty() && wantMobile) {
            // 1080p H.264 (compatible con cualquier móvil). En HEAD no se
            // genera nada: si el proxy no existe, se responde 409 para que
            // el cliente haga un GET (que sí espera al job).
            if (isHead) {
                std::wstring proxy = MobileProxyReadyPath(g_baseDir, key);
                if (proxy.empty()) {
                    SendText(client, 409, "Conflict",
                             "{\"error\":\"proxy_pending\"}");
                    closesocket(client);
                    return;
                }
                file = proxy;
            } else {
                std::wstring proxy =
                    EnsureMobileProxyAsync(g_baseDir, key, 180000);
                if (!proxy.empty()) file = proxy;
            }
        }
        if (!file.empty() && !isHead && !wantPreview && !wantMobile) {
            MaybeFillItemDims(key, file);
        }
        if (file.empty() || !SendFile(client, file, range, true, isHead)) {
            SendText(client, 404, "Not Found", "{\"error\":\"no file\"}");
        }
    } else if (path.rfind("/api/proxy/", 0) == 0) {
        std::string rest = path.substr(11);
        const std::string kCancel = "/cancel";
        if (rest.size() > kCancel.size() &&
            rest.rfind(kCancel) == rest.size() - kCancel.size()) {
            std::string key = UrlDecode(rest.substr(0, rest.size() - kCancel.size()));
            bool ok = CancelMobileProxy(key);
            SendText(client, ok ? 200 : 404, ok ? "OK" : "Not Found",
                     ok ? "{\"cancelled\":true}" : "{\"error\":\"no job\"}");
        } else {
            std::string key = UrlDecode(rest);
            SendText(client, 200, "OK", MobileProxyStatusJson(key));
        }
    } else if (path.rfind("/api/item/", 0) == 0) {
        std::string key = UrlDecode(path.substr(10));
        std::string j = RemoteItemJson(key);
        if (j.empty()) {
            SendText(client, 404, "Not Found", "{\"error\":\"item\"}");
        } else {
            SendText(client, 200, "OK", j);
        }
    } else if (path.rfind("/api/preview/", 0) == 0) {
        // WebP animado para la vista previa del editor de pantallas.
        std::string key = UrlDecode(path.substr(13));
        std::wstring file = EnsurePreviewWebp(g_baseDir, key);
        if (file.empty() || !SendFile(client, file, range, true, isHead)) {
            SendText(client, 404, "Not Found", "{\"error\":\"no preview\"}");
        }
    } else if (path == "/api/thumb/prefetch" && method == "POST") {
        std::vector<std::string> keys = JsonStringArray(body, "keys");
        if (keys.size() > 200) keys.resize(200);
        const int queued = PrefetchRemoteThumbsAsync(g_baseDir, keys);
        SendText(client, 200, "OK",
                 "{\"ok\":true,\"queued\":" + std::to_string(queued) + "}");
    } else if (path.rfind("/api/thumb/", 0) == 0) {
        std::string key = UrlDecode(path.substr(11));
        std::wstring file = RemoteThumbPath(g_baseDir, key);
        if (file.empty()) {
            // Encola (sin adelantar) y espera como máximo ~450 ms. El cliente
            // reintenta con espera creciente; no se bloquea la conexión.
            EnsureRemoteThumbAsync(g_baseDir, key);
            for (int i = 0; i < 3 && file.empty(); ++i) {
                Sleep(150);
                file = RemoteThumbPath(g_baseDir, key);
            }
        }
        if (file.empty() || !SendFile(client, file, range, false)) {
            SendText(client, 404, "Not Found", "{\"error\":\"no thumb\"}");
        }
    } else {
        SendText(client, 404, "Not Found", "{\"error\":\"route\"}");
    }
    closesocket(client);
}

void ServerLoop(SOCKET srv) {
    if (srv == INVALID_SOCKET) return;
    for (;;) {
        sockaddr_in peer{};
        int peerLen = sizeof(peer);
        SOCKET c = accept(srv, (sockaddr*)&peer, &peerLen);
        if (c == INVALID_SOCKET) continue;
        // Sólo 127.0.0.1 puede usar el editor con rutas fuera de res\.
        bool local = (ntohl(peer.sin_addr.s_addr) == INADDR_LOOPBACK);
        std::thread(Handle, c, local).detach();
    }
}

// Descubrimiento en LAN: responde a "WPX/Ping" por UDP 7884 con los datos del
// PC para que la app móvil no necesite QR la primera vez.
void DiscoveryLoop() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(7884);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return;
    }

    wchar_t pc[256] = {};
    DWORD sz = 256;
    GetComputerNameW(pc, &sz);
    std::string name = WideToUtf8(std::wstring(pc, sz));

    char buf[512];
    for (;;) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from,
                         &fromLen);
        if (n <= 0) continue;
        buf[n] = 0;
        if (strncmp(buf, "WPX/Ping", 8) != 0) continue;
        char reply[512];
        sprintf_s(reply,
                  "{\"product\":\"%s\",\"app\":\"DNXWallpaper v3\","
                  "\"name\":\"%s\",\"port\":%d,\"version\":\"1.0\"}",
                  kProduct, name.c_str(), g_port);
        sendto(s, reply, (int)strlen(reply), 0, (sockaddr*)&from, fromLen);
    }
}

}  // namespace

void StartLanServer(const std::wstring& baseDir) {
    if (g_started.exchange(true)) return;
    g_baseDir = baseDir;

    std::string json;
    {
        FILE* f = nullptr;
        _wfopen_s(&f, (baseDir + L"\\res\\config\\mobile.json").c_str(), L"rb");
        if (f) {
            char buf[4096] = {};
            size_t got = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            json.assign(buf, got);
        }
    }
    if (!json.empty() && json.find("\"enabled\"") != std::string::npos &&
        json.find("false", json.find("\"enabled\"")) != std::string::npos &&
        json.find("false", json.find("\"enabled\"")) <
            json.find(',', json.find("\"enabled\""))) {
        return;  // enabled:false
    }
    std::string port = ReadConfigStr(json, "port");
    if (!port.empty()) {
        int p = atoi(port.c_str());
        if (p > 1024 && p < 65535) g_port = p;
    }
    g_token = ReadConfigStr(json, "token");

    // server.json guarda lo elegido en el arranque anterior. Sin esto el puerto
    // derivaria tras cada reinicio y los QR/clientes emparejados dejarian de
    // valer.
    const std::wstring statePath = baseDir + L"\\res\\config\\server.json";
    {
        std::string st = ReadWholeFile(statePath);
        std::string p = ReadConfigStr(st, "port");
        if (!p.empty()) {
            int v = atoi(p.c_str());
            if (v > 1024 && v < 65535) g_port = v;
        }
        g_instanceId = ReadConfigStr(st, "instanceId");
    }
    if (g_instanceId.empty()) g_instanceId = NewGuidString();

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    // Si el puerto esta ocupado (otro DNXWallpaper, o cualquier otro
    // servicio), se prueba el siguiente. 30 intentos es de sobra.
    const int want = g_port;
    SOCKET srv = INVALID_SOCKET;
    int chosen = 0;
    for (int p = want; p < want + 30; ++p) {
        srv = TryListenOn(p);
        if (srv != INVALID_SOCKET) {
            chosen = p;
            break;
        }
    }
    if (srv == INVALID_SOCKET) return;  // sin puerto libre: sin servidor
    g_port = chosen;

    {
        char b[512];
        sprintf_s(b, "{\"port\":%d,\"instanceId\":\"%s\"}", g_port,
                  g_instanceId.c_str());
        WriteWholeFile(statePath, b);
    }

    g_enabled = true;
    std::thread(ServerLoop, srv).detach();
    std::thread(DiscoveryLoop).detach();
}

std::string LanInfoJson() {    std::string ip;
    ULONG size = 16000;
    std::vector<unsigned char> buf(size);
    auto* addrs = (PIP_ADAPTER_ADDRESSES)buf.data();
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size) ==
        NO_ERROR) {
        for (auto* a = addrs; a && ip.empty(); a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                auto* sa = (sockaddr_in*)u->Address.lpSockaddr;
                if (!sa || sa->sin_family != AF_INET) continue;
                char s[64] = {};
                inet_ntop(AF_INET, &sa->sin_addr, s, sizeof(s));
                std::string cand = s;
                if (cand.rfind("169.254.", 0) == 0) continue;
                if (cand.rfind("127.", 0) == 0) continue;
                ip = cand;
                break;
            }
        }
    }
    if (ip.empty()) ip = "127.0.0.1";
    char b[256];
    sprintf_s(b, "{\"ip\":\"%s\",\"port\":%d,\"enabled\":%s}", ip.c_str(),
              g_port, g_enabled.load() ? "true" : "false");
    return b;
}

int LanPort() { return g_enabled.load() ? g_port : 0; }
