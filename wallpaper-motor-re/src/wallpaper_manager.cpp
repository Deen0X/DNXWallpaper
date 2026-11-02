// wallpaper_manager.cpp — Implementación del gestor principal.
#include "wallpaper_manager.h"

#include <windows.h>
#include "render/dwm_attach.h"
#include "render/d3d11_renderer.h"
#include "media/scene_wallpaper.h"
#include "media/video_wallpaper.h"
#include "media/audio.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
D3D11Renderer g_renderer;
SceneWallpaper g_scene;
VideoWallpaper g_video;
AudioProcessor g_audio;
HWND g_parent = nullptr;
HWND g_view = nullptr;

// --- Asignación por monitor ------------------------------------------------
// Una pantalla puede tener su propio vídeo: el motor reproduce N ficheros a
// la vez y dibuja cada uno en su caja del escritorio virtual. Así el modo
// "Individual" no necesita componer un vídeo con ffmpeg.
struct CellState {
    VideoWallpaper video;
    std::wstring file;
    int x = 0, y = 0, w = 1920, h = 1080;
    int mode = 0;                        // 0 cover, 1 contain, 2 stretch
    float cx = 0, cy = 0, cw = 1, ch = 1; // recorte UV (zoom/paneo)
};
std::vector<std::unique_ptr<CellState>> g_cells;
bool g_cellsMode = false;
int g_vx = 0, g_vy = 0;   // origen del escritorio virtual (Init)

void StopCells() {
    for (auto& c : g_cells) c->video.Stop();
    g_cells.clear();
    g_renderer.ClearVideoSlots();
    g_cellsMode = false;
}

int FitModeFromName(const std::string& fit) {
    if (fit == "center" || fit == "fit") return 1;
    if (fit == "fill" || fit == "stretch") return 2;
    return 0;  // expand / cover
}

struct PlaylistState {
    std::vector<std::wstring> items;
    size_t index = 0;
    DWORD intervalMs = 60000;
    DWORD transitionMs = 2000;
    DWORD lastSwitch = 0;
    DWORD transitionStart = 0;
    bool transitioning = false;
    bool enabled = false;   // false = wallpaper manual fijo
};
PlaylistState g_playlist;

void LogStage(const char* stage) {
    // Log externo (res/dnxwallpaper.log): feedback visible del arranque.
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, stage);
        fclose(f);
    }
    OutputDebugStringA(stage);
    OutputDebugStringA("\n");
}
} // namespace

WallpaperManager& WallpaperManager::Instance() {
    static WallpaperManager inst;
    return inst;
}

namespace {

// Ajustes de imagen (res/config/optimizations.json):
//   "quality": "auto" | "high" | "medium" | "low"
//   "hdr": true | false
// quality limita la altura de decodificacion (low 720p, medium 1080p,
// high/auto nativa). hdr usa swapchain 16F + PQ si el monitor es HDR.
struct DisplayOpts {
    int maxHeight = 0;
    bool hdr = true;
};

DisplayOpts LoadDisplayOpts() {
    DisplayOpts o;
    std::wstring dir;
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        dir = exe;
        dir = dir.substr(0, dir.find_last_of(L"\\/"));
    }
    FILE* f = nullptr;
    _wfopen_s(&f, (dir + L"\\res\\config\\optimizations.json").c_str(), L"rb");
    if (!f) return o;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string j((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) fread(j.data(), 1, (size_t)n, f);
    fclose(f);

    size_t q = j.find("\"quality\"");
    if (q != std::string::npos) {
        size_t a = j.find('"', j.find(':', q));
        size_t b = (a == std::string::npos) ? a : j.find('"', a + 1);
        if (a != std::string::npos && b != std::string::npos) {
            std::string v = j.substr(a + 1, b - a - 1);
            if (v == "low") o.maxHeight = 720;
            else if (v == "medium") o.maxHeight = 1080;
            else o.maxHeight = 0;  // high / auto
        }
    }
    size_t h = j.find("\"hdr\"");
    if (h != std::string::npos) {
        size_t c = j.find(':', h);
        if (c != std::string::npos) o.hdr = j.compare(c + 1, 4, "true") == 0;
    }
    return o;
}

DisplayOpts g_display;

// Aplica los ajustes al reproductor (la calidad sólo afecta a decoders
// nuevos; el renderer necesita el flag antes de Init).
void ApplyDisplayOpts() {
    g_display = LoadDisplayOpts();
    g_video.SetMaxHeight(g_display.maxHeight);
}

}  // namespace

bool WallpaperManager::EnsureSingleInstance() {
    // Mutex observado: "dnxWallpaperV3GlobalMutex".
    // Mutex propio del build local: permite coexistir con el WE original
    // durante el desarrollo (el original usa dnxWallpaperV3GlobalMutex).
    instanceMutex_ = CreateMutexW(nullptr, TRUE, L"dnxWallpaperV3Mutex");
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

bool WallpaperManager::EnumerateMonitors() {
    monitors_.clear();
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT r, LPARAM l) {
        auto* v = reinterpret_cast<std::vector<MonitorTarget>*>(l);
        MonitorTarget m;
        m.index = (int)v->size();
        m.x = r->left;
        m.y = r->top;
        m.width = r->right - r->left;
        m.height = r->bottom - r->top;
        v->push_back(m);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors_));
    return !monitors_.empty();
}

bool WallpaperManager::LoadScene(const std::wstring& path, WallpaperType type) {
    if (type == WallpaperType::Scene) {
        return g_scene.LoadSceneAndProperties(path);
    }
    if (type == WallpaperType::Video) {
        return g_video.StartVideoWithNewPlayer(path);
    }
    // Web/Application se delegan a procesos aparte (webwallpaper64 /
    // applicationwallpaperinject) vía IPC — fase 2.
    return false;
}

void WallpaperManager::ShowContent() {
    ShowWallpaperParent(g_parent, 255);
}

HWND WallpaperManager::ParentHwnd() const {
    return g_parent;
}

void WallpaperManager::ReloadDisplaySettings() {
    int prev = g_video.MaxHeight();
    ApplyDisplayOpts();
    bool cellChanged = false;
    for (auto& c : g_cells) {
        if (c->video.MaxHeight() != g_display.maxHeight) cellChanged = true;
    }
    if ((g_video.MaxHeight() != prev && g_video.IsPlaying()) || cellChanged) {
        // El tope de decodificación sólo se aplica al crear el lector:
        // se reaplica la selección para reiniciarlo.
        ApplySelection();
    }
}

void WallpaperManager::SetVideoPaused(bool paused) {
    g_video.SetPaused(paused);
    for (auto& c : g_cells) c->video.SetPaused(paused);
}

bool WallpaperManager::HasScene() const {
    return g_cellsMode || !g_scene.Objects().empty() || g_video.IsPlaying();
}


namespace {
void RecordRecent(const std::wstring& path) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring file = dir + L"\\res\\config\\recent.json";
    std::vector<std::wstring> items;
    FILE* f = nullptr;
    _wfopen_s(&f, file.c_str(), L"rb");
    if (f) {
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
            if (w.find(L':') != std::wstring::npos) items.push_back(w);
            p = e + 1;
        }
    }
    std::vector<std::wstring> out;
    out.push_back(path);
    for (auto& it : items) {
        if (_wcsicmp(it.c_str(), path.c_str()) != 0 && out.size() < 10)
            out.push_back(it);
    }
    FILE* w = nullptr;
    _wfopen_s(&w, file.c_str(), L"w, ccs=UTF-8");
    if (!w) return;
    fwprintf(w, L"{\"items\":[");
    for (size_t i = 0; i < out.size(); ++i) {
        std::wstring esc;
        for (wchar_t c : out[i]) {
            if (c == L'\\' || c == L'"') esc.push_back(L'\\');
            esc.push_back(c);
        }
        fwprintf(w, L"%s\"%s\"", i ? L"," : L"", esc.c_str());
    }
    fwprintf(w, L"]}");
    fclose(w);
}
} // namespace

namespace {

// --- Asignación por monitor: lectura de res/current.json --------------------
// La UI/servidor escribe:
//   {"displays":[{"index":0,"file":"C:\\a.mp4","fit":"expand",
//                 "x":0,"y":0,"w":1920,"h":1080,
//                 "cx":0,"cy":0,"cw":1,"ch":1}, ...]}
// Se parsea a mano: el formato lo controla el propio servidor y sólo se
// necesitan cadenas escapadas y números.
struct CellSpec {
    std::wstring file;
    int x = 0, y = 0, w = 0, h = 0;
    std::string fit = "expand";
    float cx = 0, cy = 0, cw = 1, ch = 1;
};

std::string JsonFieldStr(const std::string& j, size_t from, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return {};
    p = j.find(':', p);
    if (p == std::string::npos) return {};
    size_t a = j.find('"', p);
    if (a == std::string::npos) return {};
    std::string out;
    for (size_t i = a + 1; i < j.size(); ++i) {
        if (j[i] == '\\' && i + 1 < j.size()) { out.push_back(j[++i]); continue; }
        if (j[i] == '"') break;
        out.push_back(j[i]);
    }
    return out;
}

double JsonFieldNum(const std::string& j, size_t from, const char* key,
                    double def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return def;
    p = j.find(':', p);
    if (p == std::string::npos) return def;
    return atof(j.c_str() + p + 1);
}

bool ParseDisplays(const std::string& json, std::vector<CellSpec>& out) {
    out.clear();
    size_t dp = json.find("\"displays\"");
    if (dp == std::string::npos) return false;
    size_t a = json.find('[', dp);
    if (a == std::string::npos) return false;
    size_t b = json.find(']', a);
    if (b == std::string::npos) return false;
    const std::string arr = json.substr(a + 1, b - a - 1);
    size_t pos = 0;
    while (true) {
        size_t ob = arr.find('{', pos);
        if (ob == std::string::npos) break;
        size_t cb = arr.find('}', ob);
        if (cb == std::string::npos) break;
        const std::string obj = arr.substr(ob, cb - ob + 1);
        pos = cb + 1;
        CellSpec s;
        const std::string file = JsonFieldStr(obj, 0, "file");
        if (file.empty()) continue;
        // El servidor escribe UTF-8: a wchar por la API de Windows.
        int wn = MultiByteToWideChar(CP_UTF8, 0, file.c_str(), -1, nullptr, 0);
        if (wn > 0) {
            std::wstring w((size_t)wn, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, file.c_str(), -1, w.data(), wn);
            while (!w.empty() && w.back() == L'\0') w.pop_back();
            s.file = w;
        }
        if (s.file.empty()) continue;
        s.fit = JsonFieldStr(obj, 0, "fit");
        if (s.fit.empty()) s.fit = "expand";
        s.x = (int)JsonFieldNum(obj, 0, "x", 0);
        s.y = (int)JsonFieldNum(obj, 0, "y", 0);
        s.w = (int)JsonFieldNum(obj, 0, "w", 0);
        s.h = (int)JsonFieldNum(obj, 0, "h", 0);
        s.cx = (float)JsonFieldNum(obj, 0, "cx", 0);
        s.cy = (float)JsonFieldNum(obj, 0, "cy", 0);
        s.cw = (float)JsonFieldNum(obj, 0, "cw", 1);
        s.ch = (float)JsonFieldNum(obj, 0, "ch", 1);
        out.push_back(s);
    }
    return true;
}

// Arranca un vídeo por pantalla. Devuelve true si al menos una funciona.
bool ApplyCells(const std::vector<CellSpec>& specs) {
    StopCells();
    g_video.Stop();
    g_renderer.SetBackgroundImage(L"");
    bool any = false;
    for (const auto& s : specs) {
        auto cell = std::make_unique<CellState>();
        cell->file = s.file;
        cell->x = s.x;
        cell->y = s.y;
        cell->w = s.w > 0 ? s.w : 1920;
        cell->h = s.h > 0 ? s.h : 1080;
        cell->mode = FitModeFromName(s.fit);
        cell->cx = s.cx; cell->cy = s.cy; cell->cw = s.cw; cell->ch = s.ch;
        cell->video.SetMaxHeight(g_display.maxHeight);
        if (!cell->video.StartVideoWithNewPlayer(s.file)) {
            LogStage("displays: video FAILED (pantalla sin fondo)");
            continue;
        }
        g_cells.push_back(std::move(cell));
        any = true;
    }
    g_cellsMode = any;
    if (any) {
        char b[96];
        sprintf_s(b, "displays: %d pantalla(s) con video propio",
                  (int)g_cells.size());
        LogStage(b);
        WallpaperManager::Instance().ShowContent();
    }
    return any;
}

}  // namespace

bool WallpaperManager::ApplySelection() {
    g_video.SetMaxHeight(g_display.maxHeight);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::string cur;
    {
        FILE* f = nullptr;
        _wfopen_s(&f, (dir + L"\\res\\current.json").c_str(), L"rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            cur.assign((size_t)(n > 0 ? n : 0), '\0');
            if (n > 0) {
                fread(cur.data(), 1, (size_t)n, f);
            }
            fclose(f);
        }
    }
    if (cur.empty()) {
        return false;
    }
    // Asignación por monitor: cada pantalla con su propio vídeo.
    {
        std::vector<CellSpec> specs;
        if (ParseDisplays(cur, specs) && !specs.empty()) {
            g_playlist.enabled = false;
            if (ApplyCells(specs)) return true;
            LogStage("apply: displays sin video valido -> se sigue con project");
        }
    }
    // Cualquier otro modo (panorámico, zoom, playlist) vuelve a una sola
    // superficie: se paran los vídeos por pantalla.
    StopCells();
    size_t kp = cur.find("\"project\"");
    if (kp == std::string::npos) {
        return false;
    }
    size_t a = cur.find('"', cur.find(':', kp));
    size_t b = cur.find('"', a + 1);
    if (a == std::string::npos || b == std::string::npos) {
        return false;
    }
    std::string projRaw = cur.substr(a + 1, b - a - 1);
    // Des-escapar JSON (\\ -> \, \" -> ").
    std::string proj;
    for (size_t i = 0; i < projRaw.size(); ++i) {
        if (projRaw[i] == '\\' && i + 1 < projRaw.size()) {
            proj.push_back(projRaw[++i]);
        } else {
            proj.push_back(projRaw[i]);
        }
    }
    // Si es una ruta absoluta (galeria de carpetas), saltar project.json.
    // Playlist: la UI escribe "playlist": true al activarla.
    {
        bool playlistMode = false;
        size_t pk = cur.find("\"playlist\"");
        if (pk != std::string::npos) {
            size_t colon = cur.find(':', pk);
            if (colon != std::string::npos) {
                size_t v = cur.find_first_not_of(" \t\r\n", colon + 1);
                playlistMode = v != std::string::npos && cur[v] == 't';
            }
        }
        if (playlistMode) {
            LoadPlaylistConfig();
            g_playlist.enabled = true;
            g_playlist.lastSwitch = GetTickCount();
            if (!g_playlist.items.empty()) {
                g_playlist.index = 0;
                ApplyFile(g_playlist.items[0]);
                LogStage("apply: playlist activada");
                return true;
            }
            LogStage("apply: playlist vacia -> manual");
        }
        g_playlist.enabled = false;
    }
    bool projIsPath = proj.size() > 2 && proj[1] == ':';
    if (proj.empty()) {
        return false;
    }
    std::wstring pdir = dir + L"\\res\\projects\\";
    if (!projIsPath) {
        for (char c : proj) {
            pdir.push_back(c == '/' ? L'\\' : (wchar_t)c);
        }
    }
    // project.json -> file (scene.json o vídeo).
    if (projIsPath) {
        std::wstring absPath;
        for (char c : proj) {
            absPath.push_back(c == '/' ? L'\\' : (wchar_t)c);
        }
        std::wstring low = absPath;
        for (auto& c : low) c = (wchar_t)towlower(c);
        bool isVideo = low.size() > 4 &&
                       (low.compare(low.size() - 4, 4, L".mp4") == 0 ||
                        low.compare(low.size() - 4, 4, L".mkv") == 0 ||
                        low.compare(low.size() - 4, 4, L".avi") == 0 ||
                        low.compare(low.size() - 5, 5, L".webm") == 0 ||
                        low.compare(low.size() - 4, 4, L".wmv") == 0 ||
                        low.compare(low.size() - 4, 4, L".mov") == 0);
        if (isVideo) {
            if (!g_video.StartVideoWithNewPlayer(absPath)) {
                LogStage("apply: video FAILED");
                return false;
            }
            g_renderer.SetBackgroundImage(L"");
            ShowContent();
            LogStage("apply: video (folder source) OK");
            return true;
        }
        if (!g_renderer.SetBackgroundImage(absPath)) {
            LogStage("apply: image FAILED");
            return false;
        }
        g_video.Stop();
        ShowContent();
        LogStage("apply: image (folder source) OK");
        return true;
    }

    std::string manifest;
    {
        FILE* f = nullptr;
        _wfopen_s(&f, (pdir + L"\\project.json").c_str(), L"rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            manifest.assign((size_t)(n > 0 ? n : 0), '\0');
            if (n > 0) {
                fread(manifest.data(), 1, (size_t)n, f);
            }
            fclose(f);
        }
    }
    if (manifest.empty()) {
        return false;
    }
    auto getStr = [](const std::string& json, const std::string& key) {
        std::string k = "\"" + key + "\"";
        size_t pos = json.find(k);
        if (pos == std::string::npos) {
            return std::string();
        }
        pos = json.find(':', pos + k.size());
        if (pos == std::string::npos) {
            return std::string();
        }
        size_t x = json.find('"', pos);
        size_t y = json.find('"', x + 1);
        if (x == std::string::npos || y == std::string::npos) {
            return std::string();
        }
        return json.substr(x + 1, y - x - 1);
    };
    std::string file = getStr(manifest, "file");
    std::string type = getStr(manifest, "type");
    if (file.empty()) {
        return false;
    }
    std::wstring wfile;
    bool absolute = file.size() > 2 && file[1] == ':';
    if (!absolute) {
        wfile = pdir + L"\\";
    }
    for (char c : file) {
        wfile.push_back(c == '/' ? L'\\' : (wchar_t)c);
    }
    WallpaperType wt = WallpaperType::Scene;
    if (type == "video") {
        wt = WallpaperType::Video;
    }
    {
        char b[256];
        sprintf_s(b, "apply: loading '%s' type=%s", file.c_str(), type.c_str());
        LogStage(b);
    }
    if (!LoadScene(wfile, wt)) {
        LogStage("apply: LoadScene FAILED");
        return false;
    }
    // Fondo: preview del proyecto (imagen local) a pantalla completa.
    // Si no hay preview válido, queda el clearcolor de la escena.
    {
        std::string preview = getStr(manifest, "preview");
        if (!preview.empty()) {
            std::wstring wprev = pdir + L"\\";
            for (char c : preview) {
                wprev.push_back(c == '/' ? L'\\' : (wchar_t)c);
            }
            g_renderer.SetBackgroundImage(wprev);
        } else {
            g_renderer.SetBackgroundImage(L"");
        }
    }
    ShowContent();
    {
        char b[128];
        sprintf_s(b, "selection applied: %s", proj.c_str());
        LogStage(b);
    }
    return true;
}

bool WallpaperManager::ApplyFile(const std::wstring& path) {
    // Un fichero suelto (playlist, reciente) sustituye la asignación por
    // monitor: se paran las celdas.
    StopCells();
    std::wstring low = path;
    for (auto& c : low) c = (wchar_t)towlower(c);
    bool isVideo = low.size() > 4 &&
                   (low.rfind(L".mp4") == low.size() - 4 ||
                    low.rfind(L".mkv") == low.size() - 4 ||
                    low.rfind(L".webm") == low.size() - 5 ||
                    low.rfind(L".avi") == low.size() - 4 ||
                    low.rfind(L".mov") == low.size() - 4 ||
                    low.rfind(L".wmv") == low.size() - 4);
    if (isVideo) {
        if (!g_video.StartVideoWithNewPlayer(path)) {
            LogStage("apply: video FAILED");
            return false;
        }
        g_renderer.SetBackgroundImage(L"");
        ShowContent();
        RecordRecent(path);
        return true;
    }
    if (!g_renderer.SetBackgroundImage(path)) {
        LogStage("apply: image FAILED");
        return false;
    }
    g_video.Stop();
    ShowContent();
    RecordRecent(path);
    return true;
}

void WallpaperManager::LoadPlaylistConfig() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::string json;
    {
        FILE* f = nullptr;
        _wfopen_s(&f, (dir + L"\\res\\config\\playlist.json").c_str(), L"rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            json.assign((size_t)(n > 0 ? n : 0), '\0');
            if (n > 0) fread(json.data(), 1, (size_t)n, f);
            fclose(f);
        }
    }
    g_playlist.items.clear();
    if (json.empty()) {
        return;
    }
    // items: ["ruta", ...]
    size_t p = json.find("\"items\"");
    if (p == std::string::npos) return;
    size_t a = json.find('[', p);
    size_t b = json.find(']', a);
    if (a == std::string::npos || b == std::string::npos) return;
    std::string arr = json.substr(a + 1, b - a - 1);
    size_t i = 0;
    while ((i = arr.find('"', i)) != std::string::npos) {
        size_t j = arr.find('"', i + 1);
        if (j == std::string::npos) break;
        std::string item = arr.substr(i + 1, j - i - 1);
        // des-escapar
        std::string un;
        for (size_t k = 0; k < item.size(); ++k) {
            if (item[k] == '\\' && k + 1 < item.size()) un.push_back(item[++k]);
            else un.push_back(item[k]);
        }
        g_playlist.items.push_back(std::wstring(un.begin(), un.end()));
        i = j + 1;
    }
    auto num = [&](const char* key, DWORD def) -> DWORD {
        size_t q = json.find(std::string("\"") + key + "\"");
        if (q == std::string::npos) return def;
        q = json.find(':', q);
        if (q == std::string::npos) return def;
        return (DWORD)atoi(json.c_str() + q + 1);
    };
    g_playlist.intervalMs = num("intervalSeconds", 60) * 1000;
    g_playlist.transitionMs = num("transitionSeconds", 2) * 1000;
    g_playlist.index = 0;
    g_playlist.lastSwitch = GetTickCount();
    char info[128];
    sprintf_s(info, "playlist: %d elementos, %lu s, transicion %lu s",
              (int)g_playlist.items.size(),
              (unsigned long)(g_playlist.intervalMs / 1000),
              (unsigned long)(g_playlist.transitionMs / 1000));
    LogStage(info);
}

void WallpaperManager::UpdatePlaylist() {
    if (!g_playlist.enabled || g_playlist.items.size() < 2 ||
        g_playlist.intervalMs == 0) {
        return;
    }
    DWORD now = GetTickCount();
    if (!g_playlist.transitioning &&
        now - g_playlist.lastSwitch >= g_playlist.intervalMs) {
        g_playlist.index = (g_playlist.index + 1) % g_playlist.items.size();
        // Congelar el fondo actual para el crossfade.
        g_renderer.SnapshotCurrent();
        ApplyFile(g_playlist.items[g_playlist.index]);
        g_playlist.transitionStart = now;
        g_playlist.transitioning = true;
        g_playlist.lastSwitch = now;
        LogStage("playlist: siguiente wallpaper");
    }
    if (g_playlist.transitioning) {
        DWORD elapsed = now - g_playlist.transitionStart;
        if (elapsed >= g_playlist.transitionMs) {
            g_playlist.transitioning = false;
            g_renderer.ClearSnapshot();
        }
    }
}

void WallpaperManager::SetVideoMuted(bool muted) {
    g_video.SetMuted(muted);
    for (auto& c : g_cells) c->video.SetMuted(muted);
}

bool WallpaperManager::PlaylistEnabled() const { return g_playlist.enabled; }

void WallpaperManager::SetPlaylistEnabled(bool on) {
    g_playlist.enabled = on;
    g_playlist.lastSwitch = GetTickCount();
    LogStage(on ? "playlist: enabled" : "playlist: disabled (manual)");
}

void WallpaperManager::LoadPlaybackSettings() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::string j;
    FILE* f = nullptr;
    _wfopen_s(&f, (dir + L"\\res\\config\\playback.json").c_str(), L"rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        j.assign((size_t)(n > 0 ? n : 0), '\0');
        if (n > 0) fread(j.data(), 1, (size_t)n, f);
        fclose(f);
    }
    int mode = 0;
    bool audio = false;
    size_t ap = j.find("\"audio\"");
    if (ap != std::string::npos) {
        size_t colon = j.find(':', ap);
        if (colon != std::string::npos) {
            size_t v = j.find_first_not_of(" \t\r\n", colon + 1);
            audio = v != std::string::npos && j.compare(v, 4, "true") == 0;
        }
    }
    g_video.SetMuted(!audio);
    size_t k = j.find("\"mode\"");
    if (k != std::string::npos) {
        size_t q = j.find('"', j.find(':', k));
        size_t e = q == std::string::npos ? q : j.find('"', q + 1);
        if (q != std::string::npos && e != std::string::npos) {
            std::string v = j.substr(q + 1, e - q - 1);
            if (v == "fit") mode = 1;
            else if (v == "stretch") mode = 2;
        }
    }
    g_renderer.SetPlaybackMode(mode);
}

bool WallpaperManager::CaptureScreenshot(const std::wstring& path) {
    return g_renderer.SaveBackBufferPng(path);
}

void WallpaperManager::RenderFrame() {
    // Clearcolor de la escena (scene.json general.clearcolor).
    const float* c = g_scene.General().clearcolor;
    g_renderer.BeginFrame(c[0], c[1], c[2]);
    // Asignación por monitor: cada celda sube su frame y se dibuja en su caja.
    if (g_cellsMode) {
        for (size_t i = 0; i < g_cells.size(); ++i) {
            CellState& cell = *g_cells[i];
            VideoFrame f;
            if (cell.video.TakeFrame(f) && f.valid) {
                g_renderer.UpdateVideoFrameSlot((int)i, f.rgba.data(),
                                                f.width, f.height);
            }
            g_renderer.DrawCell((int)i, cell.x - g_vx, cell.y - g_vy, cell.w,
                                cell.h, cell.mode, cell.cx, cell.cy, cell.cw,
                                cell.ch);
        }
        g_renderer.EndFrame(true);
        return;
    }
    // Vídeo en curso: sube el último frame y lo dibuja; si no, imagen fija.
    VideoFrame frame;
    const bool videoActive = g_video.IsPlaying();
    if (videoActive && g_video.TakeFrame(frame) && frame.valid) {
        g_renderer.UpdateVideoFrame(frame.rgba.data(), frame.width,
                                    frame.height);
    }
    if (!videoActive) {
        // Al parar el video hay que soltar su textura: si no, el ultimo frame
        // se sigue dibujando y tapa la imagen de fondo aplicada despues.
        g_renderer.ClearVideoFrame();
    }
    float progress = 1.0f;
    if (g_playlist.transitioning && g_playlist.transitionMs > 0) {
        DWORD elapsed = GetTickCount() - g_playlist.transitionStart;
        progress = (float)elapsed / (float)g_playlist.transitionMs;
        if (progress > 1.0f) progress = 1.0f;
    }
    g_renderer.DrawWithTransition(progress);
    g_renderer.EndFrame(true);
    UpdatePlaylist();
}

namespace {
POINT g_ssStart{};
bool g_ssArmed = false;
LRESULT CALLBACK SsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEMOVE: {
        POINT p{};
        GetCursorPos(&p);
        int dx = p.x - g_ssStart.x, dy = p.y - g_ssStart.y;
        if (!g_ssArmed || (dx * dx + dy * dy) < 20 * 20) return 0;
        PostQuitMessage(0);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_MOUSEWHEEL:
        if (!g_ssArmed) return 0;
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, m, w, l);
    }
}
} // namespace

int WallpaperManager::RunScreensaverMode() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = SsProc;
    wc.hInstance = hInst;
    wc.hCursor = nullptr;
    wc.lpszClassName = L"DNXWallpaperScreensaver";
    RegisterClassW(&wc);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName,
                                L"DNXWallpaper v3 Screensaver",
                                WS_POPUP | WS_VISIBLE, vx, vy, vw, vh, nullptr,
                                nullptr, hInst, nullptr);
    if (!hwnd) { LogStage("screensaver: no window"); return 1; }
    LogStage("screensaver: window created");
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOPMOST, vx, vy, vw, vh,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetCursor(nullptr);
    GetCursorPos(&g_ssStart);

    g_parent = hwnd;
    g_view = hwnd;
    ApplyDisplayOpts();
    g_renderer.SetHdrAllowed(g_display.hdr);
    if (!g_renderer.Init(hwnd, vw, vh)) {
        LogStage("screensaver: d3d init failed");
        DestroyWindow(hwnd);
        return 1;
    }
    LogStage("screensaver: d3d ready");
    g_renderer.LoadPlaylistShaders();
    LoadPlaybackSettings();
    LoadPlaylistConfig();
    // El wallpaper del salvapantallas lo elige la UI (settings.json); el
    // servidor deja aquí la ruta ya resuelta a un fichero local.
    {
        std::string cfg;
        std::wstring f = L"res\\config\\screensaver.json";
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, f.c_str(), L"rb") == 0 && fp) {
            char buf[1024];
            size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
            fclose(fp);
            buf[n] = 0;
            cfg = buf;
        }
        size_t k = cfg.find("\"file\"");
        if (k != std::string::npos) {
            size_t a = cfg.find('"', cfg.find(':', k));
            size_t b = (a == std::string::npos) ? a : cfg.find('"', a + 1);
            if (a != std::string::npos && b != std::string::npos) {
                std::string p8 = cfg.substr(a + 1, b - a - 1);
                std::wstring w;
                for (char c : p8) w.push_back(c == '/' ? L'\\' : (wchar_t)c);
                if (!w.empty() &&
                    GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    ApplyFile(w);
                    LogStage("screensaver: wallpaper elegido");
                } else {
                    ApplySelection();
                }
            } else {
                ApplySelection();
            }
        } else {
            ApplySelection();
        }
    }
    ShowWindow(hwnd, SW_SHOW);
    LogStage("screensaver: running");
    g_ssArmed = true;

    MSG msg{};
    DWORD last = GetTickCount();
    while (true) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_renderer.Shutdown();
                DestroyWindow(hwnd);
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        DWORD now = GetTickCount();
        if (now - last >= 33) {
            last = now;
            RenderFrame();
        }
        Sleep(5);
    }
}

bool WallpaperManager::Init(const EngineOptions& opts) {
    opts_ = opts;
    LogStage("init: start");
    if (!EnsureSingleInstance()) {
        LogStage("init: another instance running, exiting");
        return false; // ya hay un motor en ejecución
    }
    LogStage("init: single instance ok");
    if (!EnumerateMonitors()) {
        LogStage("init: no monitors");
        return false;
    }
    {
        char b[128];
        sprintf_s(b, "init: %d monitor(s)", (int)monitors_.size());
        LogStage(b);
    }
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    // Escritorio virtual completo: cubre todos los monitores con una sola
    // superficie (así el fondo llega también a los monitores secundarios).
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) {
        const MonitorTarget& mon = monitors_[0];
        vx = mon.x; vy = mon.y; vw = mon.width; vh = mon.height;
    }
    g_vx = vx;
    g_vy = vy;
    g_parent = AttachWallpaperParent(hInst, vx, vy, vw, vh);
    if (!g_parent) {
        LogStage("init: AttachWallpaperParent failed");
        return false;
    }
    LogStage("init: wallpaper parent attached");
    // DirectComposition compone sobre la propia ventana padre
    // (equivalente a WPEDesktopDX11Window): sin ventana hija de render.
    g_view = g_parent;
    LogStage("init: wallpaper window created");
    ApplyDisplayOpts();
    g_renderer.SetHdrAllowed(g_display.hdr);
    if (!g_renderer.Init(g_view, vw, vh)) {
        LogStage("init: D3D11 init failed");
        return false; // sin D3D11 no hay render (D3D11CreateDevice)
    }
    LogStage("init: D3D11 ready");
    g_renderer.LoadPlaylistShaders(); // res/shaders/*.hlsl (externos)
    LogStage("init: playlist shaders loaded");
    LoadPlaybackSettings();
    LoadPlaylistConfig();
    if (!opts_.file.empty() && !opts_.noWallpapers) {
        LogStage("init: loading scene");
        LoadScene(opts_.file, opts_.type);
    } else {
        LogStage("init: no scene requested (use -file <scene.json>)");
    }
    g_audio.Start();
    LogStage("init: running (message loop)");
    return true;
}

void WallpaperManager::Shutdown() {
    g_audio.Stop();
    StopCells();
    g_video.Stop();
    g_renderer.Shutdown();
    g_view = nullptr; // es g_parent; se destruye justo debajo
    if (g_parent) {
        DestroyWindow(g_parent);
        g_parent = nullptr;
    }
    if (instanceMutex_) {
        CloseHandle(instanceMutex_);
        instanceMutex_ = nullptr;
    }
}
