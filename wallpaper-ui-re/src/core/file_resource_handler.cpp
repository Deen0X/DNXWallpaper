// file_resource_handler.cpp — Archivos locales con Range (vídeo/audio).
#include "file_resource_handler.h"

#ifdef RE_HAVE_CEF
#include <windows.h>
#include <cstdio>
#include <cstring>

static void ResLog(const char* m, long long a, long long b) {
    char buf[256];
    sprintf_s(buf, "srv: %s (%lld, %lld)", m, a, b);
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

FileResourceHandler::FileResourceHandler(std::wstring path, std::string mime)
    : path_(std::move(path)), mime_(std::move(mime)) {}

bool FileResourceHandler::Open(CefRefPtr<CefRequest> request,
                               bool& handle_request,
                               CefRefPtr<CefCallback> callback) {
    handle_request = true;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &fad)) {
        status_ = 404;
        return true;
    }
    size_ = ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    range_start_ = 0;
    range_end_ = size_ > 0 ? size_ - 1 : 0;
    status_ = 200;

    // Range: bytes=start-end
    CefString range;
    range = request->GetHeaderByName("Range");
    std::string r = range.ToString();
    size_t eq = r.find("bytes=");
    if (eq != std::string::npos && size_ > 0) {
        size_t dash = r.find('-', eq + 6);
        if (dash != std::string::npos) {
            std::string s = r.substr(eq + 6, dash - (eq + 6));
            std::string e = r.substr(dash + 1);
            long long start = s.empty() ? 0 : _strtoi64(s.c_str(), nullptr, 10);
            long long end = e.empty() ? size_ - 1 : _strtoi64(e.c_str(), nullptr, 10);
            if (start < 0) start = 0;
            if (end >= size_) end = size_ - 1;
            if (start <= end) {
                range_start_ = start;
                range_end_ = end;
                status_ = 206;
            }
        }
    }
    offset_ = range_start_;
    remaining_ = range_end_ - range_start_ + 1;
    if (remaining_ < 0) remaining_ = 0;
    return true;
}

void FileResourceHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                             int64_t& length,
                                             CefString& redirectUrl) {
    (void)redirectUrl;
    response->SetStatus(status_);
    response->SetMimeType(mime_);
    CefResponse::HeaderMap headers;
    headers.insert(std::make_pair("Accept-Ranges", "bytes"));
    headers.insert(std::make_pair("Cache-Control", "no-cache"));
    if (status_ == 206) {
        char cr[128];
        sprintf_s(cr, "bytes %lld-%lld/%lld", range_start_, range_end_, size_);
        headers.insert(std::make_pair("Content-Range", std::string(cr)));
    }
    response->SetHeaderMap(headers);
    length = remaining_;
}

bool FileResourceHandler::Read(void* data_out, int bytes_to_read,
                               int& bytes_read,
                               CefRefPtr<CefResourceReadCallback> callback) {
    (void)callback;
    bytes_read = 0;
    if (remaining_ <= 0) {
        return false;
    }
    FILE* f = nullptr;
    _wfopen_s(&f, path_.c_str(), L"rb");
    if (!f) {
        return false;
    }
    _fseeki64(f, offset_, SEEK_SET);
    int to_read = (int)((bytes_to_read < remaining_) ? bytes_to_read : remaining_);
    size_t got = fread(data_out, 1, (size_t)to_read, f);
    fclose(f);
    if (got == 0) {
        return false;
    }
    offset_ += (long long)got;
    remaining_ -= (long long)got;
    bytes_read = (int)got;
    return true;
}

void FileResourceHandler::Cancel() {}
#endif
