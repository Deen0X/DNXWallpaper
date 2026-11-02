// cef_app_impl.h — Aplicación CEF (wrapper C++, requiere CEF standard dist).
//
// Se compila solo con RE_HAVE_CEF + CEF_ROOT configurado.
// Procesos: el mismo exe actúa como browser + render (cef_execute_process
// deriva a subprocesos). Esquemas wpx.internal / wpx.app registrados aquí.
// Bindings V8 editorObject/ui creados en OnContextCreated y servidos por
// MethodDispatcher (core). Respuestas vía ExecuteJavaScript con
// window.<metodo>Callback(<json>).
#pragma once

#include <windows.h>
#ifdef RE_HAVE_CEF
#include "include/cef_app.h"
#include "include/cef_request_handler.h"
#include "include/cef_client.h"
#include "include/cef_browser.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_helpers.h"
#endif

#include <string>

class WallpaperCefApp
#ifdef RE_HAVE_CEF
    : public CefApp,
      public CefBrowserProcessHandler,
      public CefRenderProcessHandler,
      public CefDisplayHandler,
      public CefLifeSpanHandler
#endif
{
public:
    WallpaperCefApp(const std::wstring& docRoot,
                    const std::wstring& startUrl,
                    const std::string& language);
#ifdef RE_HAVE_CEF
    // CefApp.
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override;
    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override;
    void OnContextInitialized() override;
    bool OnAlreadyRunningAppRelaunch(
        CefRefPtr<CefCommandLine> command_line,
        const CefString& current_directory) override;
    void OnRegisterCustomPreferences(
        cef_preferences_type_t type,
        CefRawPtr<CefPreferenceRegistrar> registrar) override;
    // CefRenderProcessHandler.
    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override;
    // CefDisplayHandler: vuelca console.log/error de la UI a js-console.log.
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level, const CefString& message,
                          const CefString& source, int line) override;
    // CefLifeSpanHandler (ventanas/popup de la UI).
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& target_url,
                       const CefString& target_frame_name,
                       WindowOpenDisposition target_disposition,
                       bool user_gesture,
                       const CefPopupFeatures& popupFeatures,
                       CefWindowInfo& windowInfo, CefRefPtr<CefClient>& client,
                       CefBrowserSettings& settings,
                       CefRefPtr<CefDictionaryValue>& extra_info,
                       bool* no_javascript_access) override;
    IMPLEMENT_REFCOUNTING(WallpaperCefApp);
#endif
private:
    std::wstring docRoot_;
    std::wstring startUrl_;
    std::string language_;
};

#ifdef RE_HAVE_CEF
#include "include/cef_client.h"

// Cliente del navegador: display (consola JS), lifespan (popups) y
// recepción del aviso "wpx-provider-connected" del render.
class WallpaperCefClient : public CefClient,
                           public CefRequestHandler,
                           public CefKeyboardHandler {
public:
    explicit WallpaperCefClient(CefRefPtr<WallpaperCefApp> app) : app_(app) {}
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
        return app_.get();
    }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return app_.get();
    }
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
    // F5 / Ctrl+R recargan la interfaz (es web y se sirve en vivo).
    bool OnKeyEvent(CefRefPtr<CefBrowser> browser,
                    const CefKeyEvent& event,
                    CefEventHandle os_event) override;
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;
    // Recuperación ante caída del proceso render (pantalla blanca).
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                   TerminationStatus status) override;
    IMPLEMENT_REFCOUNTING(WallpaperCefClient);
private:
    CefRefPtr<WallpaperCefApp> app_;
};
#endif
