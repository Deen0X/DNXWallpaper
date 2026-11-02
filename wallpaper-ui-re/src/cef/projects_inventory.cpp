// projects_inventory.cpp — Inventario local (proyectos + carpetas de vídeo).
//
// Formato de entrada de la galería (observado en la UI original):
//   {file, title, type: scene|video, official, status:"installed",
//    preview, previewsmall}
// preview/previewsmall = URLs http://wpx.internal/__file/<ruta absoluta>
// servidas por LocalFileSchemeFactory (file:// está bloqueado en el render).
#include "projects_inventory.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

static void InvLog(const char* m) {
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) { fprintf(f, "%s\n", m); fclose(f); }
}

namespace {

std::string ReadAllFile(const std::wstring& p) {
    std::string out;
    FILE* f = nullptr;
    _wfopen_s(&f, p.c_str(), L"rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        out.assign((size_t)(n > 0 ? n : 0), '\0');
        if (n > 0) {
            fread(out.data(), 1, (size_t)n, f);
        }
        fclose(f);
    }
    return out;
}

std::string JsonGetStr(const std::string& json, const std::string& key) {
    std::string k = "\"" + key + "\"";
    size_t pos = json.find(k);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + k.size());
    if (pos == std::string::npos) return {};
    size_t a = json.find('"', pos);
    if (a == std::string::npos) return {};
    size_t b = json.find('"', a + 1);
    if (b == std::string::npos) return {};
    return json.substr(a + 1, b - a - 1);
}

std::string ToUtf8(const std::wstring& w) {
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

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    }
    return out;
}

std::string UrlEncodePath(std::wstring p) {
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

std::string EscapeJson(const std::string& in) {
    std::string out;
    for (char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        default: out.push_back(c);
        }
    }
    return out;
}

// --- Miniatura animada (WebP) usando ffmpeg si está disponible ---
std::wstring FfmpegPath() {
    static std::wstring cached;
    static bool done = false;
    if (done) return cached;
    done = true;
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr)) {
        cached = buf;
        return cached;
    }
    const wchar_t* candidates[] = {
        L"C:\\Windows\\ffmpeg.exe",
    };
    for (auto c : candidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) {
            cached = c;
            break;
        }
    }
    return cached;
}

bool MakeAnimatedWebp(const std::wstring& src, const std::wstring& dst) {
    if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true; // ya generada
    }
    std::wstring ff = FfmpegPath();
    if (ff.empty()) {
        return false;
    }
    std::wstring cmd = L"\"" + ff + L"\" -y -v error -i \"" + src +
                       L"\" -t 6 -vf \"fps=10,scale=320:-2:flags=lanczos\" "
                       L"-c:v libwebp_anim -lossless 0 -q:v 55 -loop 0 \"" +
                       dst + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0 &&
           GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// --- Miniatura real del vídeo/imagen vía IShellItemImageFactory + WIC ---
bool SaveThumbnailPng(const std::wstring& src, const std::wstring& dst) {
    if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true; // ya generada
    }
    // COM puede no estar inicializado en este hilo (CEF UI thread).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellItemImageFactory* factory = nullptr;
    HRESULT hrCreate = SHCreateItemFromParsingName(src.c_str(), nullptr,
                                                   IID_PPV_ARGS(&factory));
    if (FAILED(hrCreate)) {
        char b[200];
        sprintf_s(b, "thumb: SHCreateItem failed 0x%08X", (unsigned)hrCreate);
        InvLog(b);
        return false;
    }
    SIZE sz{320, 180};
    HBITMAP bmp = nullptr;
    HRESULT hr = factory->GetImage(sz, SIIGBF_THUMBNAILONLY, &bmp);
    if (FAILED(hr) || !bmp) {
        hr = factory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &bmp);
    }
    factory->Release();
    if (FAILED(hr) || !bmp) {
        char b[200];
        sprintf_s(b, "thumb: GetImage failed 0x%08X", (unsigned)hr);
        InvLog(b);
        return false;
    }
    bool ok = false;
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        IWICBitmap* wbmp = nullptr;
        if (SUCCEEDED(wic->CreateBitmapFromHBITMAP(bmp, nullptr,
                                                   WICBitmapIgnoreAlpha, &wbmp))) {
            IWICStream* stream = nullptr;
            IWICBitmapEncoder* enc = nullptr;
            IWICBitmapFrameEncode* frame = nullptr;
            if (SUCCEEDED(wic->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromFilename(dst.c_str(), GENERIC_WRITE)) &&
                SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
                SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
                SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) &&
                SUCCEEDED(frame->Initialize(nullptr))) {
                UINT w = 0, h = 0;
                wbmp->GetSize(&w, &h);
                frame->SetSize(w, h);
                WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
                frame->SetPixelFormat(&fmt);
                if (SUCCEEDED(frame->WriteSource(wbmp, nullptr)) &&
                    SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit())) {
                    ok = true;
                }
            }
            if (frame) frame->Release();
            if (enc) enc->Release();
            if (stream) stream->Release();
            wbmp->Release();
        }
        wic->Release();
    }
    DeleteObject(bmp);
    return ok;
}

std::wstring ExtLower(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return {};
    std::wstring ext = name.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

bool IsVideoExt(const std::wstring& ext) {
    return ext == L".mp4" || ext == L".mkv" || ext == L".webm" ||
           ext == L".avi" || ext == L".wmv" || ext == L".mov" || ext == L".m4v";
}

bool IsImageExt(const std::wstring& ext) {
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
           ext == L".bmp" || ext == L".gif" || ext == L".webp";
}

// Carpetas configuradas: res/config/video_sources.json {"folders":[...]}
std::vector<std::wstring> ReadVideoSources(const std::wstring& baseDir) {
    std::vector<std::wstring> out;
    std::string json = ReadAllFile(baseDir + L"\\res\\config\\video_sources.json");
    if (json.empty()) {
        return out;
    }
    size_t p = 0;
    while ((p = json.find('"', p)) != std::string::npos) {
        size_t q = json.find('"', p + 1);
        if (q == std::string::npos) break;
        std::string val = json.substr(p + 1, q - p - 1);
        p = q + 1;
        // Des-escapar JSON (\\ -> \, \" -> "): sin esto SHCreateItem falla
        std::string unescaped;
        for (size_t i = 0; i < val.size(); ++i) {
            if (val[i] == '\\' && i + 1 < val.size()) {
                unescaped.push_back(val[++i]);
            } else {
                unescaped.push_back(val[i]);
            }
        }
        if (unescaped.size() > 2 && unescaped[1] == ':') {
            out.push_back(ToWide(unescaped));
        }
    }
    return out;
}

} // namespace

void SyncVideoSources(const std::wstring& baseDir) {
    std::wstring thumbs = baseDir + L"\\res\\cache\\thumbs";
    CreateDirectoryW((baseDir + L"\\res\\cache").c_str(), nullptr);
    CreateDirectoryW(thumbs.c_str(), nullptr);
    auto folders = ReadVideoSources(baseDir);
    {
        char b[160];
        sprintf_s(b, "thumbs: %d carpetas configuradas", (int)folders.size());
        InvLog(b);
    }
    int files = 0, made = 0;
    for (const auto& folder : folders) {
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            std::wstring ext = ExtLower(name);
            if (!IsVideoExt(ext) && !IsImageExt(ext)) continue;
            std::wstring src = folder + L"\\" + name;
            std::wstring base = name.substr(0, name.find_last_of(L'.'));
            for (auto& c : base) {
                if (c == L' ' || c == L'(' || c == L')') c = L'_';
            }
            ++files;
            if (SaveThumbnailPng(src, thumbs + L"\\" + base + L".png")) {
                ++made;
            }
            if (IsVideoExt(ext)) {
                MakeAnimatedWebp(src, thumbs + L"\\" + base + L".webp");
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    {
        char b[160];
        sprintf_s(b, "thumbs: %d archivos, %d miniaturas", files, made);
        InvLog(b);
    }
}

std::string BuildProjectsJson(const std::wstring& baseDir) {
    std::string json = "[";
    bool first = true;
    std::set<std::string> seen;

    auto append = [&](const std::string& file, const std::string& title,
                      const std::string& type, const std::string& preview) {
        if (!seen.insert(file).second) {
            return; // duplicado
        }
        if (!first) json.push_back(',');
        first = false;
        json += "{\"file\":\"" + EscapeJson(file) +
                "\",\"title\":\"" + EscapeJson(title) +
                "\",\"type\":\"" + type +
                "\",\"official\":false,\"local\":true,"
                "\"status\":\"installed\","
                "\"preview\":\"" +
                EscapeJson(preview) + "\",\"previewsmall\":\"" +
                EscapeJson(preview) + "\"}";
    };

    // 1) Proyectos locales res/projects/*/project.json
    std::wstring proot = baseDir + L"\\res\\projects\\";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((proot + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring pdir = proot + fd.cFileName;
            std::string manifest = ReadAllFile(pdir + L"\\project.json");
            if (manifest.empty()) continue;
            std::string title = JsonGetStr(manifest, "title");
            std::string type = JsonGetStr(manifest, "type");
            if (type.empty()) type = "scene";
            std::string preview = JsonGetStr(manifest, "preview");
            std::wstring pfull = pdir + L"\\";
            for (char c : preview) {
                pfull.push_back(c == '/' ? L'\\' : (wchar_t)c);
            }
            std::string previewUrl;
            if (!preview.empty() &&
                GetFileAttributesW(pfull.c_str()) != INVALID_FILE_ATTRIBUTES) {
                previewUrl = FileUrl(pfull);
            }
            // file puede ser absoluto (carpetas externas) o relativo al proyecto
            std::string file = JsonGetStr(manifest, "file");
            std::string fileOut;
            if (file.size() > 2 && file[1] == ':') {
                fileOut = file;
            } else {
                fileOut = ToUtf8(fd.cFileName);
            }
            append(fileOut, title, type, previewUrl);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // 2) Carpetas de vídeo configuradas (res/config/video_sources.json)
    std::wstring thumbs = baseDir + L"\\res\\cache\\thumbs\\";
    for (const auto& folder : ReadVideoSources(baseDir)) {
        HANDLE hf = FindFirstFileW((folder + L"\\*").c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            std::wstring ext = ExtLower(name);
            bool isVideo = IsVideoExt(ext);
            if (!isVideo && !IsImageExt(ext)) continue;
            std::wstring src = folder + L"\\" + name;
            std::wstring base = name.substr(0, name.find_last_of(L'.'));
            std::wstring thumbBase = base;
            for (auto& c : thumbBase) {
                if (c == L' ' || c == L'(' || c == L')') c = L'_';
            }
            std::wstring thumbAnim = thumbs + thumbBase + L".webp";
            std::wstring thumb = thumbs + thumbBase + L".png";
            std::string previewUrl;
            if (GetFileAttributesW(thumbAnim.c_str()) != INVALID_FILE_ATTRIBUTES) {
                previewUrl = FileUrl(thumbAnim);
            } else if (GetFileAttributesW(thumb.c_str()) != INVALID_FILE_ATTRIBUTES) {
                previewUrl = FileUrl(thumb);
            }
            append(ToUtf8(src), ToUtf8(base).size() ? ToUtf8(base) : ToUtf8(name),
                   isVideo ? "video" : "image", previewUrl);
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }

    json.push_back(']');
    return json;
}


std::string BuildPreviewFileMap(const std::wstring& baseDir) {
    std::string json = "{";
    bool first = true;
    auto add = [&](const std::string& preview, const std::string& file) {
        if (preview.empty()) return;
        if (!first) json.push_back(',');
        first = false;
        json += "\"" + EscapeJson(preview) + "\":\"" + EscapeJson(file) + "\"";
    };

    // Proyectos
    std::wstring proot = baseDir + L"\\res\\projects\\";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((proot + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring pdir = proot + fd.cFileName;
            std::string manifest = ReadAllFile(pdir + L"\\project.json");
            if (manifest.empty()) continue;
            std::string preview = JsonGetStr(manifest, "preview");
            std::wstring pfull = pdir + L"\\";
            for (char c : preview) pfull.push_back(c == '/' ? L'\\' : (wchar_t)c);
            std::string file = JsonGetStr(manifest, "file");
            std::string fileUrl;
            if (file.size() > 2 && file[1] == ':') {
                fileUrl = FileUrl(ToWide(file));
            } else {
                std::wstring full = pdir + L"\\";
                for (char c : file) full.push_back(c == '/' ? L'\\' : (wchar_t)c);
                fileUrl = FileUrl(full);
            }
            if (GetFileAttributesW(pfull.c_str()) != INVALID_FILE_ATTRIBUTES) {
                add(FileUrl(pfull), fileUrl);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // Carpetas de vídeo
    std::wstring thumbs = baseDir + L"\\res\\cache\\thumbs\\";
    for (const auto& folder : ReadVideoSources(baseDir)) {
        HANDLE hf = FindFirstFileW((folder + L"\\*").c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            std::wstring ext = ExtLower(name);
            if (!IsVideoExt(ext) && !IsImageExt(ext)) continue;
            std::wstring src = folder + L"\\" + name;
            std::wstring base = name.substr(0, name.find_last_of(L'.'));
            std::wstring thumbBase = base;
            for (auto& c : thumbBase) {
                if (c == L' ' || c == L'(' || c == L')') c = L'_';
            }
            std::wstring thumbAnim = thumbs + thumbBase + L".webp";
            std::wstring thumb = thumbs + thumbBase + L".png";
            if (GetFileAttributesW(thumbAnim.c_str()) != INVALID_FILE_ATTRIBUTES) {
                add(FileUrl(thumbAnim), FileUrl(src));
            }
            if (GetFileAttributesW(thumb.c_str()) != INVALID_FILE_ATTRIBUTES) {
                add(FileUrl(thumb), FileUrl(src));
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }

    json.push_back('}');
    return json;
}
