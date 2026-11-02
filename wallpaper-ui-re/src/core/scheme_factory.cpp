// scheme_factory.cpp — Servidor de archivos locales (C++ puro, testeable).
#include "scheme_factory.h"

#include <windows.h>

LocalFileSchemeFactory::LocalFileSchemeFactory(std::wstring docRoot)
    : docRoot_(std::move(docRoot)) {}

std::string LocalFileSchemeFactory::MimeForExtension(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    std::wstring ext = (dot == std::wstring::npos) ? L"" : path.substr(dot);
    for (auto& c : ext) {
        c = (wchar_t)towlower(c);
    }
    if (ext == L".html") {
        return "text/html";
    }
    if (ext == L".js") {
        return "text/javascript";
    }
    if (ext == L".css") {
        return "text/css";
    }
    if (ext == L".json") {
        return "application/json";
    }
    if (ext == L".png") {
        return "image/png";
    }
    if (ext == L".jpg" || ext == L".jpeg") {
        return "image/jpeg";
    }
    if (ext == L".gif") {
        return "image/gif";
    }
    if (ext == L".webp") {
        return "image/webp";
    }
    if (ext == L".bmp") {
        return "image/bmp";
    }
    // Vídeo: imprescindible un MIME correcto para que <video> lo reproduzca.
    if (ext == L".mp4" || ext == L".m4v") {
        return "video/mp4";
    }
    if (ext == L".webm") {
        return "video/webm";
    }
    if (ext == L".mkv") {
        return "video/x-matroska";
    }
    if (ext == L".avi") {
        return "video/x-msvideo";
    }
    if (ext == L".mov") {
        return "video/quicktime";
    }
    if (ext == L".wmv") {
        return "video/x-ms-wmv";
    }
    return "application/octet-stream"; // observado en .rdata
}

namespace {
// %XX decoding (toLocalUrl usa encodeURIComponent; el factory original
// decodifica: "LocalFileSchemeFactory: kFilePrefix failed" si no).
std::wstring UrlDecode(const std::wstring& in) {
    std::wstring out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == L'%' && i + 2 < in.size()) {
            auto hex = [](wchar_t c) -> int {
                if (c >= L'0' && c <= L'9') {
                    return c - L'0';
                }
                if (c >= L'a' && c <= L'f') {
                    return c - L'a' + 10;
                }
                if (c >= L'A' && c <= L'F') {
                    return c - L'A' + 10;
                }
                return -1;
            };
            int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((wchar_t)((hi << 4) | lo));
                i += 3;
                continue;
            }
        }
        out.push_back(in[i] == L'\\' ? L'/' : in[i]);
        ++i;
    }
    return out;
}
} // namespace

SchemeResolveResult LocalFileSchemeFactory::Resolve(const std::wstring& url) {
    SchemeResolveResult r;
    // http://wpx.internal/__file/<ruta-absoluta-url-encoded> (toLocalUrl):
    // sirve CUALQUIER archivo local, no solo docRoot.
    static const wchar_t* kFilePrefix = L"http://wpx.internal/__file/";
    if (url.compare(0, wcslen(kFilePrefix), kFilePrefix) == 0) {
        std::wstring absPath = UrlDecode(url.substr(wcslen(kFilePrefix)));
        if (absPath.find(L"..") != std::wstring::npos) {
            r.error = L"LocalFileSchemeFactory: kFilePrefix failed: " + url;
            return r;
        }
        if (GetFileAttributesW(absPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            r.error = L"LocalFileSchemeFactory: CefStreamReader::CreateForFile failed";
            return r;
        }
        r.found = true;
        r.filePath = absPath;
        r.mimeType = MimeForExtension(absPath);
        return r;
    }
    // http://wpx.internal/<ruta> (wpx.app usa la misma raíz docRoot).
    std::wstring rel;
    {
        static const wchar_t* kAppPrefixes[] = {
            L"http://wpx.internal/", L"http://wpx.app/",
        };
        bool ok = false;
        for (auto p : kAppPrefixes) {
            if (url.compare(0, wcslen(p), p) == 0) {
                rel = url.substr(wcslen(p));
                ok = true;
                break;
            }
        }
        if (!ok) {
            r.error = L"LocalFileSchemeFactory: kFilePrefix failed: " + url;
            return r;
        }
        // Quitar query (?skinStyle=...&skinKey=...&lang=...).
        size_t q = rel.find(L'?');
        if (q != std::wstring::npos) {
            rel = rel.substr(0, q);
        }
        if (rel.empty()) {
            rel = L"/index.html"; // observado en .rdata
        }
    }
    if (rel.empty() || rel[0] != L'/') {
        rel = L"/" + rel;
    }
    // Seguridad: sin "..".
    if (rel.find(L"..") != std::wstring::npos) {
        r.error = L"LocalFileSchemeFactory: kFilePrefix failed: " + url;
        return r;
    }
    std::wstring full = docRoot_ + UrlDecode(rel);
    if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
        r.error = L"LocalFileSchemeFactory: CefStreamReader::CreateForFile failed";
        return r;
    }
    r.found = true;
    r.filePath = full;
    r.mimeType = MimeForExtension(full);
    return r;
}
