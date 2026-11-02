// file_resource_handler.h — Sirve archivos locales a CEF con soporte Range.
//
// Necesario para <video>: el elemento multimedia exige Content-Type correcto
// y peticiones Range (206 Partial Content). CefStreamResourceHandler no
// gestiona rangos, por eso los vídeos fallaban con MEDIA_ERR_SRC_NOT_SUPPORTED.
#pragma once

#ifdef RE_HAVE_CEF
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/cef_request.h"
#endif
#include <string>

#ifdef RE_HAVE_CEF
class FileResourceHandler : public CefResourceHandler {
public:
    FileResourceHandler(std::wstring path, std::string mime);
    bool Open(CefRefPtr<CefRequest> request, bool& handle_request,
              CefRefPtr<CefCallback> callback) override;
    void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t& length,
                            CefString& redirectUrl) override;
    bool Read(void* data_out, int bytes_to_read, int& bytes_read,
              CefRefPtr<CefResourceReadCallback> callback) override;
    void Cancel() override;
    IMPLEMENT_REFCOUNTING(FileResourceHandler);
private:
    std::wstring path_;
    std::string mime_;
    long long size_ = 0;
    long long offset_ = 0;
    long long remaining_ = 0;
    int status_ = 200;
    long long range_start_ = 0;
    long long range_end_ = 0;
};
#endif
