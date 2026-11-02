// cef_v8_bindings.h — Binding V8 genérico editorObject/ui.
//
// Un único CefV8Handler recibe (object, method, args...) desde JS:
//   window.editorObject.<metodo>(...args) -> handler.Execute con
//   nombre "<object>.<metodo>". Delega en MethodDispatcher y responde
//   ejecutando window.<callback>(<json>) en el mismo frame.
// Requiere CEF (compila solo con RE_HAVE_CEF).
#pragma once

#include <windows.h>
#ifdef RE_HAVE_CEF
#include "include/cef_v8.h"
#endif
#include <string>
#include "../core/method_dispatch.h"

#ifdef RE_HAVE_CEF
class V8BridgeHandler : public CefV8Handler {
public:
    V8BridgeHandler(CefRefPtr<CefBrowser> browser, MethodDispatcher* disp,
                    const std::string& object);
    bool Execute(const CefString& name,
                 CefRefPtr<CefV8Value> object,
                 const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>& retval,
                 CefString& exception) override;
    IMPLEMENT_REFCOUNTING(V8BridgeHandler);
private:
    CefRefPtr<CefBrowser> browser_;
    MethodDispatcher* dispatcher_;
    std::string object_;
};

// Crea window.<object> con una función V8 por cada método registrado.
void BindV8Object(CefRefPtr<CefV8Context> context, CefRefPtr<CefBrowser> browser,
                  MethodDispatcher* dispatcher, const std::string& object,
                  const std::vector<std::string>& methods);

// Crea window.global con _wpxLocale(key) y jsInit() (los inyecta el host
// nativo en el original; vendor.js falla sin ellos). Lee textos de
// <localesDir>/core_<lang>.json + ui_<lang>.json (externos, formato
// {"clave":"texto"}).
void BindGlobalObject(CefRefPtr<CefV8Context> context,
                      CefRefPtr<CefBrowser> browser,
                      const std::wstring& localesDir,
                      const std::string& language);

// Emite updateWallpapers(lista fresca) a la galería (push del host).
void PushProjectsToUI(CefRefPtr<CefBrowser> browser);
#endif
