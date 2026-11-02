// cef_v8_bindings.cpp — Implementación del bridge V8 <-> nativo.
#include <windows.h>
#include "cef_v8_bindings.h"
#include <map>
#include <cstdarg>
#ifdef RE_HAVE_CEF
#include "include/cef_task.h"
#include "include/cef_process_message.h"
#include "projects_inventory.h"
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

namespace {
} // namespace

namespace {

// "Open Wallpaper": GetOpenFileName (imágenes+vídeo) -> res/projects/<base>/
// con project.json (type por extensión; preview = el archivo si es imagen).
// Retorna el nombre del proyecto o "" si se canceló.
std::string ImportFileViaDialog() {
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter =
        L"Wallpapers\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.mp4;*.avi;*.mkv;*.webm;*.wmv\0"
        L"Imágenes\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0"
        L"Vídeos\0*.mp4;*.avi;*.mkv;*.webm;*.wmv\0"
        L"Todo\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = _countof(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return "";
    }
    std::wstring src = file;
    size_t dot = src.find_last_of(L".");
    std::wstring ext = (dot == std::wstring::npos) ? L"" : src.substr(dot);
    for (auto& c : ext) {
        c = (wchar_t)towlower(c);
    }
    bool isVideo = ext == L".mp4" || ext == L".avi" || ext == L".mkv" ||
                   ext == L".webm" || ext == L".wmv";
    bool isImage = ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
                   ext == L".bmp" || ext == L".gif";
    if (!isVideo && !isImage) {
        return "";
    }
    size_t sep = src.find_last_of(L"\\/");
    std::wstring base = (sep == std::wstring::npos) ? src : src.substr(sep + 1);
    size_t dext = base.find_last_of(L'.');
    if (dext != std::wstring::npos) {
        base = base.substr(0, dext);
    }
    for (auto& c : base) {
        if (c == L' ') {
            c = L'_';
        }
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring pdir = dir + L"\\res\\projects\\" + base;
    CreateDirectoryW(pdir.c_str(), nullptr);
    // Copiar el archivo tal cual.
    std::wstring dst = pdir + src.substr(src.find_last_of(L"\\/"));
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        return "";
    }
    std::wstring fname = dst.substr(dst.find_last_of(L"\\/") + 1);
    char narrow[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, fname.c_str(), -1, narrow, MAX_PATH,
                        nullptr, nullptr);
    char nbase[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, base.c_str(), -1, nbase, MAX_PATH,
                        nullptr, nullptr);
    std::string type = isVideo ? "video" : "image";
    std::string preview = isImage ? std::string(narrow) : "";
    std::string manifest = std::string("{\"file\": \"") + narrow +
                           "\", \"title\": \"" + nbase +
                           "\", \"type\": \"" + type +
                           "\", \"preview\": \"" + preview +
                           "\", \"visibility\": \"public\", \"official\": false}";
    FILE* f = nullptr;
    _wfopen_s(&f, (pdir + L"\\project.json").c_str(), L"w, ccs=UTF-8");
    if (!f) {
        return "";
    }
    fwrite(manifest.data(), 1, manifest.size(), f);
    fclose(f);
    return nbase;
}

} // namespace
#endif

#ifdef RE_HAVE_CEF
#include "include/cef_parser.h"


// Ejecutar JavaScript desde dentro de un handler V8 provoca reentrancia en
// V8 y termina en acceso a memoria inválido (pantalla blanca). Se difiere a
// la task runner del frame.
class DeferredJsTask : public CefTask {
public:
    DeferredJsTask(CefRefPtr<CefFrame> frame, std::string js)
        : frame_(frame), js_(std::move(js)) {}
    void Execute() override {
        if (frame_) {
            frame_->ExecuteJavaScript(js_, frame_->GetURL(), 0);
        }
    }
private:
    CefRefPtr<CefFrame> frame_;
    std::string js_;
    IMPLEMENT_REFCOUNTING(DeferredJsTask);
};

void RunJsDeferred(CefRefPtr<CefFrame> frame, std::string js) {
    if (!frame) return;
    CefRefPtr<CefTask> task = new DeferredJsTask(frame, std::move(js));
    // TID_RENDERER: misma hebra del handler V8, pero ejecutado después de
    // que Execute() retorne (sin reentrancia).
    CefPostTask(TID_RENDERER, task);
}

V8BridgeHandler::V8BridgeHandler(CefRefPtr<CefBrowser> browser,
                                 MethodDispatcher* disp,
                                 const std::string& object)
    : browser_(browser), dispatcher_(disp), object_(object) {}

bool V8BridgeHandler::Execute(const CefString& name,
                              CefRefPtr<CefV8Value> object,
                              const CefV8ValueList& arguments,
                              CefRefPtr<CefV8Value>& retval,
                              CefString& exception) {
    // name = "<object>.<metodo>"
    std::string full = name.ToString();
    size_t dot = full.find('.');
    std::string method = (dot == std::string::npos) ? full : full.substr(dot + 1);
    // "Open Wallpaper" (browseWallpaperObject.openFromFile): file picker
    // nativo -> importa a res/projects/<nombre>/ -> refresca la galería.
    // Se intercepta aquí (necesita el browser para el push posterior).
    // Botón "Aceptar" de la galería: la UI llama acceptAndClose sin
    // argumentos. Se toma la selección actual del scope Angular y se
    // reenvía a selectWallpaper (persiste current.json + señaliza motor).
    if (object_ == "browseWallpaperObject" && method == "toggleMiniMode") {
        static bool mini = false;
        static RECT normal{0, 0, 1440, 860};
        // OJO: este handler se ejecuta en el proceso RENDERER, donde
        // CefBrowserHost::GetWindowHandle() no devuelve la ventana (sólo es
        // válido en el browser). Se localiza por título como respaldo; el
        // renderer corre con --no-sandbox y puede usar user32.
        HWND hwnd = nullptr;
        if (browser_ && browser_->GetHost()) {
            hwnd = browser_->GetHost()->GetWindowHandle();
        }
        if (!hwnd || !IsWindow(hwnd)) {
            hwnd = FindWindowW(nullptr, L"DNXWallpaper v3");
        }
        if (hwnd && IsWindow(hwnd)) {
            HWND top = GetAncestor(hwnd, GA_ROOT);
            if (top) hwnd = top;
            if (!mini) {
                GetWindowRect(hwnd, &normal);
                SetWindowPos(hwnd, nullptr, normal.left, normal.top, 560, 780,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                mini = true;
            } else {
                SetWindowPos(hwnd, nullptr, normal.left, normal.top,
                             normal.right - normal.left,
                             normal.bottom - normal.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                mini = false;
            }
        }
        // El resize real lo hace el proceso browser (aquí no tiene efecto
        // porque la ventana pertenece a otro proceso).
        if (browser_ && browser_->GetMainFrame()) {
            CefRefPtr<CefProcessMessage> msg =
                CefProcessMessage::Create("wpx-mini");
            msg->GetArgumentList()->SetBool(0, mini);
            browser_->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
        }
        // La UI espera la respuesta por callback (callDeferred); además se
        // aplica la clase CSS aquí para no depender de su promesa.
        if (browser_) {
            std::string js =
                "try{document.body.classList.";
            js += mini ? "add" : "remove";
            js += "('browseWallpaperMiniMode');var c=window.browseWallpapersCtrl;";
            // En modo mini el original oculta la cabecera y deja sólo el
            // panel de detalle: sin selección se ve una página vacía.
            js += "if(c&&!c.currentSelection&&c.sortedWallpapers&&";
            js += "c.sortedWallpapers.length){c.currentSelection=c.sortedWallpapers[0];";
            js += "if(c.$apply)c.$apply();}";
            js += "if(c){try{c.isMiniMode=";
            js += mini ? "true" : "false";
            js += ";}catch(e){}}if(window.toggleMiniModeCallback)";
            js += "window.toggleMiniModeCallback(";
            js += mini ? "true" : "false";
            js += ");}catch(e){}";
            RunJsDeferred(browser_->GetMainFrame(), js);
        }
        retval = CefV8Value::CreateBool(mini);
        return true;
    }
    if (object_ == "browseWallpaperObject" && method == "acceptAndClose") {
        if (auto frame = browser_ ? browser_->GetMainFrame() : nullptr) {
            RunJsDeferred(
                frame,
                "(function(){try{var c=window.browseWallpapersCtrl;"
                "if(!c||!c.currentSelection)return;"
                "var s=c.currentSelection;"
                "var loc=(c.selectedMonitor&&c.selectedMonitor.location)||'';"
                "if(window.browseWallpaperObject&&"
                "window.browseWallpaperObject.selectWallpaper){"
                "window.browseWallpaperObject.selectWallpaper("
                "s.file||'',loc,s.overridetype);}"
                "}catch(e){}})()");
        }
        retval = CefV8Value::CreateBool(true);
        return true;
    }
    if (object_ == "browseWallpaperObject" && method == "openFromFile") {
        std::string imported = ImportFileViaDialog();
        if (!imported.empty()) {
            PushProjectsToUI(browser_);
        }
        retval = CefV8Value::CreateBool(!imported.empty());
        return true;
    }
    std::vector<std::string> args;
    for (auto& a : arguments) {
        if (a->IsString()) {
            args.push_back(a->GetStringValue().ToString());
        } else if (a->IsBool()) {
            args.push_back(a->GetBoolValue() ? "true" : "false");
        } else if (a->IsInt()) {
            args.push_back(std::to_string(a->GetIntValue()));
        } else if (a->IsDouble()) {
            args.push_back(std::to_string(a->GetDoubleValue()));
        } else if (a->IsArray()) {
            // Arrays (p. ej. ui.subscribeWorkshopItem([item,...])): se
            // serializan como JSON con los identificadores relevantes.
            std::string arr = "[";
            bool first = true;
            int n = a->GetArrayLength();
            for (int i = 0; i < n; ++i) {
                auto el = a->GetValue(i);
                std::string v;
                if (el && el->IsString()) {
                    v = el->GetStringValue().ToString();
                } else if (el && el->IsObject()) {
                    auto wid = el->GetValue("workshopid");
                    auto fil = el->GetValue("file");
                    if (wid && wid->IsString()) v = wid->GetStringValue().ToString();
                    else if (fil && fil->IsString()) v = fil->GetStringValue().ToString();
                }
                if (v.empty()) continue;
                if (!first) arr += ",";
                first = false;
                arr += "\"" + v + "\"";
            }
            arr += "]";
            args.push_back(arr);
        } else {
            args.push_back("");
        }
    }
    auto [callback, result] = dispatcher_->Dispatch(object_, method, args);
    // Traza de llamadas (diagnóstico: dist/js-calls.log). Se omiten los
    // sondeos periódicos para no inundar el archivo.
    if (method != "getRemoteVersion" && method != "getDownloadStatus" &&
        method != "getRemoteItems" && method != "getLocalItems") {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        dir = dir.substr(0, dir.find_last_of(L"\\/"));
        FILE* f = nullptr;
        _wfopen_s(&f, (dir + L"\\js-calls.log").c_str(), L"a, ccs=UTF-8");
        if (f) {
            fwprintf(f, L"%hs.%hs(%d args) -> %hs\n", object_.c_str(),
                     method.c_str(), (int)args.size(), callback.c_str());
            fclose(f);
        }
    }
    std::string payload = result.ok ? result.json
                                    : std::string("{\"error\":\"") + result.error + "\"}";
    std::string js = "window." + callback + "(" + payload + ");";
    if (browser_) {
        RunJsDeferred(browser_->GetMainFrame(), js);
    }
    retval = CefV8Value::CreateBool(result.ok);
    return true;
}

void BindV8Object(CefRefPtr<CefV8Context> context, CefRefPtr<CefBrowser> browser,
                  MethodDispatcher* dispatcher, const std::string& object,
                  const std::vector<std::string>& methods) {
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    CefRefPtr<CefV8Value> obj = CefV8Value::CreateObject(nullptr, nullptr);
    CefRefPtr<V8BridgeHandler> handler =
        new V8BridgeHandler(browser, dispatcher, object);
    for (const auto& m : methods) {
        CefRefPtr<CefV8Value> fn =
            CefV8Value::CreateFunction(object + "." + m, handler);
        obj->SetValue(m, fn, V8_PROPERTY_ATTRIBUTE_NONE);
    }
    global->SetValue(object, obj, V8_PROPERTY_ATTRIBUTE_NONE);
}

// --- window.global: _wpxLocale(key) + jsInit() ---
namespace {

// Mapeo idioma UI -> sufijo de fichero (observado: core_es-es.json, ...).
std::string LocaleSuffix(const std::string& lang) {
    if (lang == "es") {
        return "es-es";
    }
    if (lang == "en") {
        return "en-us";
    }
    if (lang == "de") {
        return "de-de";
    }
    if (lang == "fr") {
        return "fr-fr";
    }
    if (lang == "pt") {
        return "pt-br";
    }
    if (lang == "it") {
        return "it-it";
    }
    if (lang == "zh") {
        return "zh-cn";
    }
    return "en-us";
}

// Decodifica \uXXXX a UTF-8 (los locale JSON usan escapes).
std::string DecodeEscapes(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            char c = in[i + 1];
            if (c == 'u' && i + 5 < in.size()) {
                unsigned cp = 0;
                for (int k = 0; k < 4; ++k) {
                    char h = in[i + 2 + k];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') {
                        cp += h - '0';
                    } else if (h >= 'a' && h <= 'f') {
                        cp += h - 'a' + 10;
                    } else if (h >= 'A' && h <= 'F') {
                        cp += h - 'A' + 10;
                    }
                }
                if (cp < 0x80) {
                    out.push_back((char)cp);
                } else if (cp < 0x800) {
                    out.push_back((char)(0xC0 | (cp >> 6)));
                    out.push_back((char)(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back((char)(0xE0 | (cp >> 12)));
                    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back((char)(0x80 | (cp & 0x3F)));
                }
                i += 6;
                continue;
            }
            if (c == 'n') {
                out.push_back('\n');
                i += 2;
                continue;
            }
            if (c == '"') {
                out.push_back('"');
                i += 2;
                continue;
            }
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

// Tabla {clave: texto} cargada de core_<s>.json + ui_<s>.json.
std::map<std::string, std::string> LoadLocale(const std::wstring& dir,
                                              const std::string& suffix) {
    std::map<std::string, std::string> table;
    for (const char* prefix : {"core_", "ui_"}) {
        char path[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, path, MAX_PATH,
                            nullptr, nullptr);
        std::string p = std::string(path) + "\\" + prefix + suffix + ".json";
        FILE* f = nullptr;
        fopen_s(&f, p.c_str(), "rb");
        if (!f) {
            continue;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string json((size_t)n, '\0');
        fread(json.data(), 1, (size_t)n, f);
        fclose(f);
        // Parser mínimo: "clave" : "valor" (valores sin comillas internas
        // salvo \" ya escapadas).
        size_t pos = 0;
        while (pos < json.size()) {
            size_t k1 = json.find('"', pos);
            if (k1 == std::string::npos) {
                break;
            }
            size_t k2 = json.find('"', k1 + 1);
            if (k2 == std::string::npos) {
                break;
            }
            size_t colon = json.find(':', k2);
            if (colon == std::string::npos) {
                break;
            }
            size_t v1 = json.find('"', colon);
            if (v1 == std::string::npos) {
                break;
            }
            // Valor con posibles \" escapadas.
            std::string val;
            size_t i = v1 + 1;
            while (i < json.size()) {
                if (json[i] == '\\' && i + 1 < json.size()) {
                    val.push_back(json[i]);
                    val.push_back(json[i + 1]);
                    i += 2;
                } else if (json[i] == '"') {
                    break;
                } else {
                    val.push_back(json[i]);
                    ++i;
                }
            }
            table[json.substr(k1 + 1, k2 - k1 - 1)] = DecodeEscapes(val);
            pos = i + 1;
        }
    }
    return table;
}

class GlobalHandler : public CefV8Handler {
public:
    GlobalHandler(CefRefPtr<CefBrowser> browser, std::wstring localesDir,
                  std::string lang)
        : browser_(browser), tableDir_(localesDir), langLoaded_(LocaleSuffix(lang)),
          table_(LoadLocale(localesDir, LocaleSuffix(lang))) {}
    bool Execute(const CefString& name, CefRefPtr<CefV8Value> object,
                 const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>& retval,
                 CefString& exception) override {
        (void)object;
        std::string full = name.ToString();
        if (full == "global.jsInit") {
            // El host original notifica aquí que el provider conectó; la UI
            // apaga entonces el overlay "connecting". Emulación: el render
            // pide al browser (único que puede ejecutar JS en el frame)
            // que apague osProviderIsConnecting en toda la jerarquía.
            // (Evidencia: onOSProviderConnectionChanged solo se llama con
            // true desde el bundle; el apagado lo hace el host nativo.)
            CefRefPtr<CefProcessMessage> msg =
                CefProcessMessage::Create("wpx-provider-connected");
            if (auto frame = browser_ ? browser_->GetMainFrame() : nullptr) {
                frame->SendProcessMessage(PID_BROWSER, msg);
            }
            retval = CefV8Value::CreateBool(true);
            return true;
        }
        if (full == "global._wpxLocale") {
            // Contrato observado (localeLoader factory): el argumento es el
            // ID de idioma ("en-us"); retorna {success, translations} donde
            // translations es el JSON completo fusionado.
            std::string key =
                !arguments.empty() && arguments[0]->IsString()
                    ? arguments[0]->GetStringValue().ToString()
                    : "";
            // Normalizar: "es" -> "es-es", "en-us" tal cual.
            std::string suffix = key;
            if (suffix.find('-') == std::string::npos) {
                suffix = LocaleSuffix(suffix);
            } else {
                for (auto& c : suffix) {
                    c = (char)tolower(c);
                }
            }
            std::wstring wdir;
            {
                int n = (int)tableDir_.size() + 1;
                std::vector<wchar_t> buf((size_t)n + 64);
                // tableDir_ ya es wstring: reconstruir no necesario.
                wdir = tableDir_;
            }
            // Recargar para el idioma pedido (caché simple por sufijo).
            std::map<std::string, std::string> t = table_;
            if (langLoaded_ != suffix) {
                t = LoadLocale(wdir, suffix);
            }
            CefRefPtr<CefV8Value> res = CefV8Value::CreateObject(nullptr, nullptr);
            // Serializar fusión a JSON.
            std::string json = "{";
            bool first = true;
            auto emit = [&](const std::map<std::string, std::string>& m) {
                for (const auto& kv : m) {
                    if (!first) {
                        json.push_back(',');
                    }
                    first = false;
                    json.push_back('"');
                    json += kv.first;
                    json += "\":\"";
                    for (char c : kv.second) {
                        if (c == '"') {
                            json += "\\\"";
                        } else if (c == '\\') {
                            json += "\\\\";
                        } else if (c == '\n') {
                            json += "\\n";
                        } else {
                            json.push_back(c);
                        }
                    }
                    json.push_back('"');
                }
            };
            emit(table_);
            if (langLoaded_ != suffix) {
                emit(t);
            }
            json.push_back('}');
            res->SetValue("success", CefV8Value::CreateBool(true),
                          V8_PROPERTY_ATTRIBUTE_NONE);
            res->SetValue("translations", CefV8Value::CreateString(json),
                          V8_PROPERTY_ATTRIBUTE_NONE);
            retval = res;
            return true;
        }
        exception = "unknown global method";
        return false;
    }
    IMPLEMENT_REFCOUNTING(GlobalHandler);
private:
    CefRefPtr<CefBrowser> browser_;
    std::wstring tableDir_;
    std::string langLoaded_;
    std::map<std::string, std::string> table_;
};

} // namespace

void BindGlobalObject(CefRefPtr<CefV8Context> context,
                      CefRefPtr<CefBrowser> browser,
                      const std::wstring& localesDir,
                      const std::string& language) {    (void)browser;
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    CefRefPtr<CefV8Value> obj = CefV8Value::CreateObject(nullptr, nullptr);
    CefRefPtr<GlobalHandler> handler =
        new GlobalHandler(browser, localesDir, language);
    for (const char* m : {"_wpxLocale", "jsInit"}) {
        obj->SetValue(m,
                      CefV8Value::CreateFunction(std::string("global.") + m,
                                               handler),
                      V8_PROPERTY_ATTRIBUTE_NONE);
    }
    global->SetValue("global", obj, V8_PROPERTY_ATTRIBUTE_NONE);
}

// Emite updateWallpapers(inventario fresco) a la galería (tras importar).
void PushProjectsToUI(CefRefPtr<CefBrowser> browser) {
    if (!browser) {
        return;
    }
    auto frame = browser->GetMainFrame();
    if (!frame) {
        return;
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::string json = BuildProjectsJson(dir);
    std::string esc;
    for (char c : json) {
        if (c == '\'') {
            esc += "\\'";
        } else if (c == '\\') {
            esc += "\\\\";
        } else {
            esc += c;
        }
    }
    RunJsDeferred(frame,
        "(function(){try{var c=window.browseWallpapersCtrl;"
        "if(c&&c.updateWallpapers){c.updateWallpapers(JSON.parse('" +
            esc + "'),[]);}}catch(e){}})()");
}
#endif
