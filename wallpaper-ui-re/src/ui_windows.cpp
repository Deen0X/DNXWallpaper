// ui_windows.cpp — Ventanas de la UI (lógica local, sin Steam).
#include "ui_windows.h"

MethodResult JSWindow::OnUiMethodCall(const std::string& method,
                                      const std::vector<std::string>& args) {
    auto [cb, r] = dispatcher_.Dispatch("ui", method, args);
    (void)cb; // el llamador CEF ejecuta window.<cb>(json)
    return r;
}

MethodResult BrowseWallpapersWindow::OnBrowseWallpaperObjectMethodCall(
    const std::string& method, const std::vector<std::string>& args) {
    auto [cb, r] = dispatcher_.Dispatch("browse", method, args);
    (void)cb;
    return r;
}

MethodResult BrowseWallpapersWindow::Update() {
    return {true, "{\"ok\":true}", ""};
}

MethodResult SettingsWindow::OnSettingsObjectMethodCall(
    const std::string& method, const std::vector<std::string>& args) {
    auto [cb, r] = dispatcher_.Dispatch("settings", method, args);
    (void)cb;
    return r;
}
