// test_core.cpp — Prueba del núcleo sin CEF (scheme + dispatch).
#include <cstdio>
#include <windows.h>
#include "core/scheme_factory.h"
#include "core/method_dispatch.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int wmain() {
    // Scheme factory (docRoot = res/ui_skel de este proyecto).
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    // El test corre en D:\Temp\opencode\build; docRoot pasado por argv alternativo:
    LocalFileSchemeFactory f(L"D:\\Develop-AI-OC_bk\\REA_Implementation\\wallpaper-ui-re\\res\\ui_skel");

    auto r1 = f.Resolve(L"http://wpx.internal/__file/index.html");
    CHECK(r1.found);
    CHECK(r1.mimeType == "text/html");
    auto r2 = f.Resolve(L"http://wpx.internal/index.html?skinStyle=x&lang=es");
    CHECK(r2.found);
    auto r3 = f.Resolve(L"http://wpx.internal/__file/../../secret.txt");
    CHECK(!r3.found); // anti path-traversal
    auto r4 = f.Resolve(L"http://wpx.internal/__file/noexiste.xyz");
    CHECK(!r4.found);
    CHECK(LocalFileSchemeFactory::MimeForExtension(L"a.json") == "application/json");
    CHECK(LocalFileSchemeFactory::MimeForExtension(L"a.bin") == "application/octet-stream");

    // Dispatcher.
    MethodDispatcher d;
    d.Register("editorObject", "getProjects",
               [](const std::vector<std::string>&) -> MethodResult {
                   return {true, "[{\"name\":\"demo\"}]", ""};
               });
    auto [cb1, res1] = d.Dispatch("editorObject", "getProjects", {});
    CHECK(cb1 == "getProjectsCallback");
    CHECK(res1.ok);
    auto [cb2, res2] = d.Dispatch("editorObject", "genericSceneAction", {"rotate"});
    CHECK(cb2 == "genericSceneAction_rotateCallbackError" || cb2 == "genericSceneAction_rotateCallback");
    CHECK(!res2.ok); // no registrado -> CallbackError
    auto [cb3, res3] = d.Dispatch("editorObject", "nope", {});
    CHECK(cb3 == "nopeCallbackError");

    if (failures == 0) {
        printf("CORE TESTS OK\n");
        return 0;
    }
    printf("FAILURES: %d\n", failures);
    return 1;
}
