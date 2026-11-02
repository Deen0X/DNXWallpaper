// cef_engine.h — Host CEF de la UI (requiere CEF standard dist).
#pragma once

#ifdef RE_HAVE_CEF
#include "include/cef_app.h"
#endif
#include <string>

struct UiOptions {
    std::wstring language = L"en";
    std::wstring skin = L"";
    std::wstring skinsJson = L"ui/skins.json";
    bool remoteDebugging = false;
    int remoteDebuggingPort = 0;
    bool weUiMain = false;
};

// URL de arranque observada en DNXWallpaper.UI.exe:
// http://wpx.internal/index.html?skinStyle=..&skinKey=..&lang=..&...
std::wstring BuildStartUrl(const std::wstring& docRoot,
                           const UiOptions& opts);

// true si el binario se compiló con CEF (RE_HAVE_CEF).
inline bool IsCefBuild() {
#ifdef RE_HAVE_CEF
    return true;
#else
    return false;
#endif
}

class CefEngine {
public:
    bool Init(const UiOptions& opts, const std::wstring& appDir);
    int RunMessageLoop();
    void Shutdown();
    void RegisterBindings(); // (los bindings se crean en OnContextCreated)

private:
    UiOptions opts_;
    std::wstring appDir_;
    bool initialized_ = false;
};
