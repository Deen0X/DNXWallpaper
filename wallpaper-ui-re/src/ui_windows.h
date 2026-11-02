// ui_windows.h — Ventanas de la UI de DNXWallpaper v3.
// La UI es la original adaptada; el bridge V8 expone los objetos
// browseWallpaperObject, ui, settingsObject y welcomeObject.
#pragma once

#include <string>
#include "core/method_dispatch.h"

// Ventana base: aloja un navegador CEF a pantalla completa.
class JSWindow {
public:
    virtual ~JSWindow() = default;
    // Original: JSWindow::OnUiMethodCall — dispatch de window.ui.*.
    virtual MethodResult OnUiMethodCall(const std::string& method,
                                        const std::vector<std::string>& args);
    MethodDispatcher& Dispatcher() { return dispatcher_; }

protected:
    MethodDispatcher dispatcher_;
};

class BrowseWallpapersWindow : public JSWindow {
public:
    // Original: OnBrowseWallpaperObjectMethodCall — dispatch window.* de browse.
    MethodResult OnBrowseWallpaperObjectMethodCall(const std::string& method,
                                                   const std::vector<std::string>& args);
    MethodResult Update();
};


class SettingsWindow : public JSWindow {
public:
    // Original: OnSettingsObjectMethodCall — dispatch window.* de settings.
    MethodResult OnSettingsObjectMethodCall(const std::string& method,
                                            const std::vector<std::string>& args);
};
