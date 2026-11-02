// editor_export.cpp â€” exportaciÃ³n de wallpapers para dispositivos.
#include "editor_export.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <condition_variable>

namespace {

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0,
                                nullptr, nullptr);
    std::string out((size_t)(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n,
                            nullptr, nullptr);
    }
    return out;
}

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
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

long long FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

std::wstring ToolPath(const wchar_t* name) {
    // 1) PATH del sistema, 2) carpeta tools junto al ejecutable (distribución),
    // 3) C:\Windows (instalaciones viejas que lo dejaban ahí).
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, name, nullptr, MAX_PATH, buf, nullptr)) return buf;
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        std::wstring dir = exe;
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            dir = dir.substr(0, slash);
            std::wstring local = dir + L"\\tools\\" + name;
            if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
                return local;
            }
            // También en la raíz (por si quedó copiado al lado del .exe).
            local = dir + L"\\" + name;
            if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
                return local;
            }
        }
    }
    std::wstring win = L"C:\\Windows\\";
    win += name;
    if (GetFileAttributesW(win.c_str()) != INVALID_FILE_ATTRIBUTES) return win;
    return {};
}

std::wstring Ffmpeg() { return ToolPath(L"ffmpeg.exe"); }
std::wstring Ffprobe() { return ToolPath(L"ffprobe.exe"); }

std::string RunCapture(const std::wstring& cmd, int timeoutMs) {
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
    DWORD start = GetTickCount();
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        out.append(buf, got);
        if (GetTickCount() - start > (DWORD)timeoutMs) break;
    }
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return out;
}

std::string GetLineValue(const std::string& text, const char* key) {
    std::string k = std::string(key) + "=";
    size_t p = text.find(k);
    if (p == std::string::npos) return {};
    p += k.size();
    size_t e = text.find_first_of("\r\n", p);
    return text.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

struct SourceInfo {
    int width = 0;
    int height = 0;
    double duration = 0;
    bool hdr = false;
};

bool Probe(const std::wstring& file, SourceInfo& out, std::string& err) {
    std::wstring ff = Ffprobe();
    if (ff.empty()) {
        err = "ffprobe no encontrado";
        return false;
    }
    std::wstring cmd = L"\"" + ff + L"\" -v error -select_streams v:0 "
                       L"-show_entries stream=width,height,color_transfer "
                       L"-show_entries format=duration -of default=nw=1 \"" +
                       file + L"\"";
    std::string text = RunCapture(cmd, 15000);
    if (text.empty()) {
        err = "ffprobe sin respuesta";
        return false;
    }
    out.width = atoi(GetLineValue(text, "width").c_str());
    out.height = atoi(GetLineValue(text, "height").c_str());
    out.duration = atof(GetLineValue(text, "duration").c_str());
    std::string ct = GetLineValue(text, "color_transfer");
    out.hdr = (ct == "smpte2084" || ct == "arib-std-b67");
    if (out.width <= 0 || out.height <= 0) {
        err = "no se pudieron leer las dimensiones";
        return false;
    }
    if (out.duration <= 0) out.duration = 15.0;
    return true;
}

std::string JsonEsc(const std::string& in) {
    std::string o;
    for (char c : in) {
        if (c == '"' || c == '\\') o.push_back('\\');
        if (c == '\n') { o += "\\n"; continue; }
        if (c == '\r') continue;
        o.push_back(c);
    }
    return o;
}

std::string GetStr(const std::string& j, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return {};
    p = j.find(':', p);
    if (p == std::string::npos) return {};
    p = j.find('"', p);
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

double GetNum(const std::string& j, const char* key, double def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return def;
    p = j.find(':', p);
    if (p == std::string::npos) return def;
    return atof(j.c_str() + p + 1);
}

bool GetBool(const std::string& j, const char* key, bool def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return def;
    p = j.find(':', p);
    if (p == std::string::npos) return def;
    while (p < j.size() && (j[p] == ':' || j[p] == ' ')) ++p;
    return j.compare(p, 4, "true") == 0;
}

double GetObjNum(const std::string& j, const char* obj, const char* key,
                 double def) {
    std::string k = std::string("\"") + obj + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return def;
    p = j.find('{', p);
    if (p == std::string::npos) return def;
    size_t e = j.find('}', p);
    if (e == std::string::npos) return def;
    return GetNum(j.substr(p, e - p + 1), key, def);
}

std::string Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return ToUtf8(s);
}

bool UnderRes(const std::wstring& baseDir, const std::wstring& file,
              std::wstring& full) {
    wchar_t buf[MAX_PATH * 2];
    if (!GetFullPathNameW(file.c_str(), _countof(buf), buf, nullptr)) return false;
    full = buf;
    std::wstring root = baseDir + L"\\res\\";
    std::string f = Lower(full), r = Lower(root);
    return f.rfind(r, 0) == 0;
}

// Extensiones que el editor sabe abrir (ffmpeg/ffprobe).
bool IsMediaFile(const std::wstring& p) {
    size_t d = p.find_last_of(L'.');
    if (d == std::wstring::npos) return false;
    std::wstring ext = p.substr(d);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    static const wchar_t* kOk[] = {
        L".mp4", L".m4v", L".webm", L".mkv", L".mov", L".avi", L".wmv",
        L".mpg", L".mpeg", L".jpg", L".jpeg", L".png", L".webp", L".bmp",
        L".gif"};
    for (const wchar_t* e : kOk) {
        if (ext == e) return true;
    }
    return false;
}

// Ruta absoluta local (X:\... o \\servidor\...).
bool IsAbsolutePath(const std::wstring& p) {
    if (p.size() > 2 && p[1] == L':') return true;
    return p.size() > 2 && p[0] == L'\\' && p[1] == L'\\';
}

// Resuelve el origen permitido segÃºn el contexto de la peticiÃ³n.
bool ResolveSource(const std::wstring& baseDir, const std::wstring& in,
                   std::wstring& full, bool allowExternal) {
    if (UnderRes(baseDir, in, full)) return true;
    if (!allowExternal || !IsAbsolutePath(in) || !IsMediaFile(in)) return false;
    full = in;
    return true;
}

int Even(int v) {
    if (v < 2) return 2;
    return v & ~1;
}

std::string BaseName(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    std::wstring n = (k == std::wstring::npos) ? p : p.substr(k + 1);
    size_t d = n.find_last_of(L'.');
    if (d != std::wstring::npos) n = n.substr(0, d);
    return ToUtf8(n);
}

struct Job {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
    bool cancel = false;
    int percent = 0;
    int width = 0;
    int height = 0;
    long long size = 0;
    double duration = 0;
    std::string id;
    std::string error;
    HANDLE process = nullptr;
};

std::mutex g_jobsMutex;
std::map<std::string, std::shared_ptr<Job>> g_jobs;
std::atomic<int> g_counter{0};

std::shared_ptr<Job> FindJob(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_jobsMutex);
    auto it = g_jobs.find(id);
    return it == g_jobs.end() ? nullptr : it->second;
}

std::string MakeId() {
    char b[32];
    sprintf_s(b, "%08x%04x", (unsigned)GetTickCount64(),
              (unsigned)(g_counter.fetch_add(1) & 0xFFFF));
    return b;
}

void SetErr(std::shared_ptr<Job> job, const std::string& e) {
    std::lock_guard<std::mutex> lk(job->m);
    job->done = true;
    job->ok = false;
    job->error = e;
    job->cv.notify_all();
}

void RunExport(std::wstring baseDir, std::wstring source, std::string title,
               std::string device, std::string vfCore, int crf, double fade,
               double outDuration, bool hdr, int vkbps,
               std::shared_ptr<Job> job) {
    std::wstring ff = Ffmpeg();
    if (ff.empty()) {
        SetErr(job, "ffmpeg no encontrado");
        return;
    }
    std::wstring dir = baseDir + L"\\res\\mobile";
    CreateDirectoryW((baseDir + L"\\res").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring finalMp4 = dir + L"\\" + ToWide(job->id) + L".mp4";
    std::wstring tmpMp4 = finalMp4 + L".part.mp4";
    std::wstring poster = dir + L"\\" + ToWide(job->id) + L".jpg";
    DeleteFileW(tmpMp4.c_str());

    std::string vf = vfCore;
    if (hdr) {
        vf = "zscale=t=linear:npl=100,tonemap=hable:desat=0,"
             "zscale=p=bt709:t=bt709:m=bt709:r=tv," + vf;
    }
    std::string filter;
    if (fade > 0.01) {
        char b[1024];
        sprintf_s(b,
                  "[0:v]%s,split=3[a][b][c];"
                  "[a]trim=start=%.3f:end=%.3f,setpts=PTS-STARTPTS[main];"
                  "[b]trim=start=%.3f,setpts=PTS-STARTPTS[tail];"
                  "[c]trim=end=%.3f,setpts=PTS-STARTPTS[head];"
                  "[tail][head]xfade=transition=fade:duration=%.3f:offset=0[fade];"
                  "[main][fade]concat=n=2:v=1:a=0[outv]",
                  vf.c_str(), fade, outDuration, outDuration,
                  fade, fade);
        filter = b;
    } else {
        filter = "[0:v]" + vf + "[outv]";
    }

    // Tope de tamaÃ±o: se fija el bitrate (una sola pasada) en vez de CRF.
    wchar_t rateArgs[160];
    if (vkbps > 0) {
        swprintf_s(rateArgs, L"-b:v %dk -maxrate %dk -bufsize %dk", vkbps,
                   vkbps, vkbps * 2);
    } else {
        swprintf_s(rateArgs, L"-crf %d", crf);
    }

    wchar_t cmd[4096];
    swprintf_s(cmd,
               L"\"%s\" -y -v error -nostats -progress pipe:1 -filter_threads 1 "
               L"-threads 2 -i \"%s\" "
               L"-filter_complex \"%s\" -map \"[outv]\" -an "
               L"-c:v libx264 -preset veryfast %s -profile:v high "
               L"-pix_fmt yuv420p -threads 2 -movflags +faststart \"%s\"",
               ff.c_str(), source.c_str(), ToWide(filter).c_str(), rateArgs,
               tmpMp4.c_str());

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        SetErr(job, "pipe");
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
    std::wstring mcmd = cmd;
    if (!CreateProcessW(nullptr, mcmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        SetErr(job, "CreateProcess");
        return;
    }
    CloseHandle(wr);
    {
        std::lock_guard<std::mutex> lk(job->m);
        job->process = pi.hProcess;
    }
    std::string line;
    std::string tail;  // Ãºltimas lÃ­neas de ffmpeg, para diagnosticar fallos
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        for (DWORD i = 0; i < got; ++i) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                size_t p = line.find("out_time_us=");
                if (p == std::string::npos) p = line.find("out_time_ms=");
                if (p != std::string::npos && outDuration > 0.1) {
                    double us = atof(line.c_str() + p + 12);
                    int pc = (int)(us / (outDuration * 1000000.0) * 100.0);
                    if (pc < 0) pc = 0;
                    if (pc > 99) pc = 99;
                    std::lock_guard<std::mutex> lk(job->m);
                    job->percent = pc;
                } else if (!line.empty() && line.find("progress=") == std::string::npos) {
                    tail += line + " | ";
                    if (tail.size() > 400) tail = tail.substr(tail.size() - 400);
                }
                line.clear();
            } else if (line.size() < 1024) {
                line.push_back(c);
            }
        }
        std::lock_guard<std::mutex> lk(job->m);
        if (job->cancel) break;
    }
    if (job->process) WaitForSingleObject(job->process, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);

    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lk(job->m);
        job->process = nullptr;
        cancelled = job->cancel;
    }
    if (cancelled || code != 0 || FileSize(tmpMp4) <= 0) {
        DeleteFileW(tmpMp4.c_str());
        if (cancelled) {
            SetErr(job, "cancelado");
        } else if (!tail.empty()) {
            SetErr(job, "ffmpeg: " + tail.substr(0, 300));
        } else {
            SetErr(job, "ffmpeg fallÃ³");
        }
        return;
    }
    DeleteFileW(finalMp4.c_str());
    if (!MoveFileExW(tmpMp4.c_str(), finalMp4.c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmpMp4.c_str());
        SetErr(job, "no se pudo mover el mp4");
        return;
    }

    // PÃ³ster: primer fotograma del resultado.
    double ss = fade > 0.01 ? fade : 0.0;
    wchar_t pcmd[2048];
    swprintf_s(pcmd,
               L"\"%s\" -y -v error -ss %.3f -i \"%s\" -vf \"%s\" "
               L"-frames:v 1 -q:v 2 \"%s\"",
               ff.c_str(), ss, source.c_str(), ToWide(vf).c_str(),
               poster.c_str());
    RunCapture(pcmd, 60000);

    long long size = FileSize(finalMp4);
    // Dimensiones reales del fichero generado.
    SourceInfo outInfo;
    std::string perr;
    int outW = 0, outH = 0;
    if (Probe(finalMp4, outInfo, perr)) {
        outW = outInfo.width;
        outH = outInfo.height;
    }
    {
        std::string meta = "{";
        meta += "\"key\":\"m:" + job->id + "\"";
        meta += ",\"title\":\"" + JsonEsc(title) + "\"";
        meta += ",\"type\":\"video\"";
        meta += ",\"width\":" + std::to_string(outW);
        meta += ",\"height\":" + std::to_string(outH);
        meta += ",\"size\":" + std::to_string(size);
        meta += ",\"duration\":" + std::to_string(outDuration);
        meta += ",\"installed\":true";
        meta += ",\"device\":\"" + JsonEsc(device) + "\"";
        meta += ",\"thumb\":\"/m/" + job->id + ".jpg\"";
        meta += ",\"url\":\"/m/" + job->id + "\"";
        meta += "}";
        std::wstring mpath = dir + L"\\" + ToWide(job->id) + L".json";
        std::wstring mtmp = mpath + L".tmp";
        FILE* f = nullptr;
        _wfopen_s(&f, mtmp.c_str(), L"wb");
        if (f) {
            fwrite(meta.data(), 1, meta.size(), f);
            fclose(f);
            MoveFileExW(mtmp.c_str(), mpath.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }

    std::lock_guard<std::mutex> lk(job->m);
    job->width = outW;
    job->height = outH;
    job->size = size;
    job->duration = outDuration;
    job->percent = 100;
    job->ok = true;
    job->done = true;
    job->cv.notify_all();
}

} // namespace

bool EditorResolveSource(const std::wstring& baseDir,
                         const std::string& file, std::wstring& full,
                         bool allowExternal) {
    if (file.empty()) return false;
    if (!ResolveSource(baseDir, ToWide(file), full, allowExternal)) return false;
    return GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string EditorProbeJson(const std::wstring& baseDir,
                            const std::string& file, bool allowExternal) {
    std::wstring full;
    if (!ResolveSource(baseDir, ToWide(file), full, allowExternal) ||
        GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return "{\"ok\":false,\"error\":\"file\"}";
    }
    SourceInfo info;
    std::string err;
    if (!Probe(full, info, err)) {
        return "{\"ok\":false,\"error\":\"" + JsonEsc(err) + "\"}";
    }
    char b[512];
    sprintf_s(b,
              "{\"ok\":true,\"width\":%d,\"height\":%d,\"duration\":%.2f,"
              "\"hdr\":%s}",
              info.width, info.height, info.duration,
              info.hdr ? "true" : "false");
    return b;
}

bool EditorProbeFile(const std::wstring& file, std::wstring& posterOut,
                     int& width, int& height, double& duration) {    SourceInfo info;
    std::string err;
    if (!Probe(file, info, err)) return false;
    width = info.width;
    height = info.height;
    duration = info.duration;

    size_t dot = file.find_last_of(L'.');
    std::wstring base = (dot == std::wstring::npos) ? file : file.substr(0, dot);
    posterOut = base + L".jpg";

    std::wstring ff = Ffmpeg();
    if (!ff.empty()) {
        wchar_t cmd[2048];
        swprintf_s(cmd,
                   L"\"%s\" -y -v error -ss 0 -i \"%s\" -frames:v 1 -q:v 3 "
                   L"\"%s\"",
                   ff.c_str(), file.c_str(), posterOut.c_str());
        RunCapture(cmd, 60000);
    }
    return true;
}

std::string EditorCurrentJson(const std::wstring& baseDir) {
    std::string txt = ReadAll(baseDir + L"\\res\\current.json");
    if (txt.empty()) return "{\"ok\":false,\"error\":\"no current\"}";
    std::string file = GetStr(txt, "project");
    if (file.empty()) file = GetStr(txt, "file");
    if (file.empty()) return "{\"ok\":false,\"error\":\"no file\"}";
    return "{\"ok\":true,\"file\":\"" + JsonEsc(file) + "\"}";
}

std::string StartEditorExport(const std::wstring& baseDir,
                              const std::string& body, bool allowExternal) {
    std::string file = GetStr(body, "file");
    std::wstring full;
    if (file.empty() ||
        !ResolveSource(baseDir, ToWide(file), full, allowExternal) ||
        GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return "{\"ok\":false,\"error\":\"source\"}";
    }
    SourceInfo info;
    std::string err;
    if (!Probe(full, info, err)) {
        return "{\"ok\":false,\"error\":\"" + JsonEsc(err) + "\"}";
    }
    double rx = GetObjNum(body, "rect", "x", 0);
    double ry = GetObjNum(body, "rect", "y", 0);
    double rw = GetObjNum(body, "rect", "w", 1);
    double rh = GetObjNum(body, "rect", "h", 1);
    if (rw <= 0.001 || rh <= 0.001) return "{\"ok\":false,\"error\":\"rect\"}";
    if (rw > 8) rw = 8;
    if (rh > 8) rh = 8;

    // El rect puede salir del vÃ­deo (bandas): se recorta la parte vÃ¡lida y se
    // rellena el resto con el color indicado.
    const double iw = info.width, ih = info.height;
    const double px = rx * iw, py = ry * ih, pw = rw * iw, ph = rh * ih;
    double ix0 = px < 0 ? 0 : px;
    double iy0 = py < 0 ? 0 : py;
    double ix1 = (px + pw) > iw ? iw : (px + pw);
    double iy1 = (py + ph) > ih ? ih : (py + ph);
    if (ix1 - ix0 < 4 || iy1 - iy0 < 4) {
        return "{\"ok\":false,\"error\":\"rect\"}";
    }

    int cw = Even((int)std::lround(ix1 - ix0));
    int ch = Even((int)std::lround(iy1 - iy0));
    int cx = (int)std::lround(ix0);
    int cy = (int)std::lround(iy0);
    if (cx + cw > info.width) cx = info.width - cw;
    if (cy + ch > info.height) cy = info.height - ch;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;

    int target = (int)GetNum(body, "target", 1080);
    if (target < 360) target = 360;
    if (target > 2160) target = 2160;

    // Salida con el aspecto exacto del viewport (lado corto = target).
    const double outAspect = pw / ph;
    int ow, oh;
    if (outAspect >= 1.0) {
        oh = Even(target);
        ow = Even((int)std::lround(target * outAspect));
    } else {
        ow = Even(target);
        oh = Even((int)std::lround(target / outAspect));
    }
    if (ow < 2) ow = 2;
    if (oh < 2) oh = 2;
    if (ow > 4096) ow = 4096;
    if (oh > 4096) oh = 4096;

    // Escala y posiciÃ³n del vÃ­deo dentro del lienzo de salida.
    const double sp = (double)ow / pw;
    int sw = Even((int)std::lround((ix1 - ix0) * sp));
    int sh = Even((int)std::lround((iy1 - iy0) * sp));
    int ox = (int)std::lround((ix0 - px) * sp);
    int oy = (int)std::lround((iy0 - py) * sp);
    if (sw < 2) sw = 2;
    if (sh < 2) sh = 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox + sw > ow) sw = Even(ow - ox);
    if (oy + sh > oh) sh = Even(oh - oy);
    if (sw < 2) sw = 2;
    if (sh < 2) sh = 2;

    std::string padColor = GetStr(body, "padColor");
    if (padColor.size() != 7 || padColor[0] != '#') padColor = "#000000";
    for (size_t i = 1; i < padColor.size(); ++i) {
        char c = padColor[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F');
        if (!hex) padColor = "#000000";
    }
    std::string padHex = "0x" + padColor.substr(1);

    char vfc[768];
    if (sw == ow && sh == oh) {
        sprintf_s(vfc,
                  "crop=%d:%d:%d:%d,scale=%d:%d:flags=lanczos,setsar=1", cw, ch,
                  cx, cy, ow, oh);
    } else {
        sprintf_s(vfc,
                  "crop=%d:%d:%d:%d,scale=%d:%d:flags=lanczos,"
                  "pad=%d:%d:%d:%d:%s,setsar=1",
                  cw, ch, cx, cy, sw, sh, ow, oh, ox, oy, padHex.c_str());
    }
    std::string vfCore = vfc;

    int quality = (int)GetNum(body, "quality", 3);
    int crf = 23;
    switch (quality) {
    case 1: crf = 28; break;
    case 2: crf = 26; break;
    case 3: crf = 23; break;
    case 4: crf = 21; break;
    case 5: crf = 18; break;
    default: crf = 23; break;
    }

    double fade = GetNum(body, "fade", 0);
    if (fade < 0) fade = 0;
    if (fade > 2) fade = 2;
    if (info.duration <= fade * 2 + 0.5) fade = 0;
    double outDuration = info.duration - fade;
    if (outDuration <= 0.1) {
        fade = 0;
        outDuration = info.duration;
    }

    std::string title = GetStr(body, "title");
    if (title.empty()) title = BaseName(full);
    std::string device = GetStr(body, "device");
    if (device.empty()) device = "Dispositivo";

    // Tope de tamaÃ±o pedido por el mÃ³vil (MB). Se traduce a bitrate para no
    // tener que re-codificar varias veces.
    double maxMB = GetNum(body, "maxSizeMB", 0);
    int vkbps = 0;
    if (maxMB > 0.5 && outDuration > 0.2) {
        double bits = maxMB * 1024.0 * 1024.0 * 8.0 * 0.92;
        vkbps = (int)(bits / outDuration / 1000.0);
    }
    if (vkbps > 0 && vkbps < 300) vkbps = 300;
    if (vkbps > 20000) vkbps = 20000;

    std::shared_ptr<Job> job = std::make_shared<Job>();
    job->id = MakeId();
    {
        std::lock_guard<std::mutex> lock(g_jobsMutex);
        g_jobs[job->id] = job;
    }
    std::thread([baseDir, full, title, device, vfCore, crf, fade, outDuration,
                 hdr = info.hdr, vkbps, job]() {
        RunExport(baseDir, full, title, device, vfCore, crf, fade, outDuration,
                  hdr, vkbps, job);
    }).detach();

    return "{\"ok\":true,\"id\":\"" + job->id + "\",\"width\":" +
           std::to_string(ow) + ",\"height\":" + std::to_string(oh) +
           ",\"duration\":" + std::to_string(outDuration) + "}";
}

std::string EditorExportStatusJson(const std::string& id) {
    std::shared_ptr<Job> job = FindJob(id);
    if (!job) return "{\"ok\":false,\"state\":\"none\"}";
    std::lock_guard<std::mutex> lk(job->m);
    const char* state = job->done ? (job->ok ? "done" : "error") : "running";
    std::string j = "{\"ok\":true,\"state\":\"";
    j += state;
    j += "\",\"percent\":" + std::to_string(job->percent);
    j += ",\"id\":\"" + job->id + "\"";
    if (!job->error.empty()) j += ",\"error\":\"" + JsonEsc(job->error) + "\"";
    if (job->ok) {
        j += ",\"width\":" + std::to_string(job->width);
        j += ",\"height\":" + std::to_string(job->height);
        j += ",\"size\":" + std::to_string(job->size);
        j += ",\"duration\":" + std::to_string(job->duration);
        j += ",\"url\":\"/m/" + job->id + "\"";
    }
    j += "}";
    return j;
}

bool CancelEditorExport(const std::string& id) {
    std::shared_ptr<Job> job = FindJob(id);
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

bool EditorProbeDims(const std::wstring& file, int& width, int& height,
                     double& duration) {
    SourceInfo info;
    std::string err;
    if (!Probe(file, info, err)) return false;
    width = info.width;
    height = info.height;
    duration = info.duration;
    return true;
}

std::wstring EditorFfmpegPath() { return Ffmpeg(); }
