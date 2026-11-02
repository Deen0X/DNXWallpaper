// scheme_factory.h — Servidor de archivos locales (esquemas wpx.*).
//
// Original: clase LocalFileSchemeFactory en cefengine.cpp
// (D:\dev\we\windows\src\ui\src\web\cefengine.cpp).
// Esquemas observados en .rdata:
//   "wpx.internal" con prefijo "/__file/" y raíz "ui/dist"
//   ("LocalFileSchemeFactory: kFilePrefix failed: %S",
//    "LocalFileSchemeFactory: CefStreamReader::CreateForFile failed",
//    "http://wpx.internal/__file/", "Not Found", "text/plain", "html",
//    "/index.html", "application/octet-stream")
//   "wpx.app" registrado en CefEngine::OnContextInitialized
//   ("Failed registering U..." = Failed registering URL).
// Esta unidad es C++ puro (sin headers CEF): resuelve URL -> archivo +
// MIME y se prueba sin libcef. La integración CEF vive en cef/cef_engine.cpp.
#pragma once

#include <string>

// Resultado de resolver una URL wpx.*.
struct SchemeResolveResult {
    bool found = false;          // false -> responder "Not Found"
    std::wstring filePath;       // ruta local a servir
    std::string mimeType;        // text/plain, text/html, ...
    std::wstring error;          // mensaje de error ("... failed: %S")
};

class LocalFileSchemeFactory {
public:
    // docRoot: raíz local equivalente a "ui/dist".
    explicit LocalFileSchemeFactory(std::wstring docRoot);

    // Resuelve http://wpx.internal/__file/<ruta> -> docRoot/<ruta>.
    // Resuelve http://wpx.internal/<ruta> -> docRoot/<ruta> (index.html por defecto).
    SchemeResolveResult Resolve(const std::wstring& url);

    static std::string MimeForExtension(const std::wstring& path);

private:
    std::wstring docRoot_;
};
