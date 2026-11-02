// cef_app_impl.cpp — Aplicación CEF: esquemas + navegador + bindings.
#include <windows.h>
#include <shellapi.h>
#include "cef_app_impl.h"
#include "../net/lan_server.h"

#ifdef RE_HAVE_CEF
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_closure_task.h"
#include "cef_v8_bindings.h"
#include "../core/method_dispatch.h"
#include "projects_inventory.h"
#include "plugin_host.h"
#include "include/cef_task.h"
#include <thread>

// Dispatcher global (vive lo que el proceso browser).
static MethodDispatcher* g_dispatcher = nullptr;
static void PushCatalogToUI(CefRefPtr<CefBrowser> browser);
MethodDispatcher* WallpaperUiDispatcher();

// Utilidades de inventario (definidas en projects_inventory.cpp).
static std::wstring ExeDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

// Inventario local (definido abajo; forward para el task del provider).

// --- Scheme handler: sirve archivos vía LocalFileSchemeFactory ---
#include "../core/scheme_factory.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "../core/file_resource_handler.h"

class WpxSchemeHandlerFactory : public CefSchemeHandlerFactory {
public:
    explicit WpxSchemeHandlerFactory(std::wstring docRoot)
        : factory_(std::move(docRoot)) {}
    CefRefPtr<CefResourceHandler> Create(
        CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        const CefString& scheme_name,
        CefRefPtr<CefRequest> request) override {
        CefString url = request->GetURL();
        auto r = factory_.Resolve(url.ToWString());
        if (!r.found) {
            return nullptr; // CEF responde 404 ("Not Found" original)
        }
        // Handler propio con soporte Range (necesario para <video>).
        return new FileResourceHandler(r.filePath, r.mimeType);
    }
    IMPLEMENT_REFCOUNTING(WpxSchemeHandlerFactory);
private:
    LocalFileSchemeFactory factory_;
};
// --- fin scheme handler ---

// Icono de la ventana principal: la crea CEF con el icono de libcef, así que
// se fuerza el recurso del EXE (rc id 1) por WM_SETICON.
static void ApplyWindowIcon(HWND h) {
    if (!h) return;
    h = GetAncestor(h, GA_ROOT);
    if (!h) return;
    auto load = [](int cx, int cy) -> HICON {
        return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                                 IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    };
    HICON icoBig = load(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    HICON icoSmall =
        load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    if (icoBig) SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)icoBig);
    if (icoSmall) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)icoSmall);
}

// ¿Interfaz nueva? res\config\ui.json -> {"mode":"classic"} para volver.
static bool UseNewUi(const std::wstring& dir) {
    FILE* f = nullptr;
    _wfopen_s(&f, (dir + L"\\res\\config\\ui.json").c_str(), L"rb");
    if (!f) return true;
    char buf[1024] = {};
    size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    return std::string(buf, got).find("\"classic\"") == std::string::npos;
}

static BOOL CALLBACK FindAppWindow(HWND h, LPARAM lp) {    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h)) return TRUE;
    wchar_t title[256] = {};
    GetWindowTextW(h, title, 256);
    if (wcsstr(title, L"DNXWallpaper")) {
        *(HWND*)lp = h;
        return FALSE;
    }
    return TRUE;
}

// Host del editor de dispositivos (Fase 1): lanza device-editor.exe si está
// instalado; si no, abre la página del editor en el navegador.
static void LaunchDeviceEditor() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t k = dir.find_last_of(L"\\/");
    if (k == std::wstring::npos) return;
    dir = dir.substr(0, k);

    std::wstring tool = dir + L"\\device-editor.exe";
    if (GetFileAttributesW(tool.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ShellExecuteW(nullptr, L"open", tool.c_str(), nullptr, dir.c_str(),
                      SW_SHOWNORMAL);
        return;
    }

    int port = 8477;
    FILE* f = nullptr;
    _wfopen_s(&f, (dir + L"\\res\\config\\mobile.json").c_str(), L"rb");
    if (f) {
        char buf[2048] = {};
        size_t got = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        std::string s(buf, got);
        size_t p = s.find("\"port\"");
        if (p != std::string::npos) {
            p = s.find(':', p);
            if (p != std::string::npos) {
                int v = atoi(s.c_str() + p + 1);
                if (v > 1024 && v < 65535) port = v;
            }
        }
    }
    std::wstring url =
        L"http://127.0.0.1:" + std::to_wstring(port) + L"/editor/";
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

// Métodos expuestos por objeto (observados en DNXWallpaper.UI.exe).
static const std::vector<std::string> kUiMethods = {    "copyToClipboard", "pasteFromClipboard", "refreshAuthToken",
    "getVirtualKeyNames", "logError", "pollSteamUserInfo", "setFavorited",
    "setUserVote", "getUserVote", "subscribeWorkshopItem",
    "unsubscribeWorkshopItem", "updateWorkshopItemMetaData",
    "previewPlaylistTransition", "openDeviceEditor", "openMobilePanel",
};
static const std::vector<std::string> kBrowseMethods = {
    "selectWallpaper", "getRemoteItems", "getRemoteVersion", "getDownloadStatus", "getPluginStatus", "logDebug", "getPlaylist", "savePlaylist", "activatePlaylist", "savePlaybackMode", "savePlaybackSettings", "getLocalSettings", "saveOptimizations", "saveDisabledPlugins", "setAutostart", "getMonitors", "getLocalItems", "getPluginList", "loadPluginPage", "getLocalFileSizes", "getThumbnail", "removeWallpaper",
    "selectPlaylist", "updateLocalWallpaperProjectInfo", "openFromFile",
    "queryWorkshop", "queryWorkshopIds", "transferWallpaperProperties",
    "addLocalWallpaperToHistory", "removeLocalWallpaperFromHistory",
    "removeOfficialWallpaper", "playInWindow", "openStorePage",
    "applySingleProperty", "saveWallpaperPresets", "acceptAndClose",
    "selectFileForProperty", "discoverMobileDevices",
    "requestWallpaperProperties", "updateProfile", "openInExplorer",
    "writeSyncFile", "readSurroundDisplayInfo", "changeConfigGroup",
    "clearWorkerWindow", "checkScreensaverSelected", "installScreensaver",
    "changeConfigGroupOptions", "disableSafeMode", "reloadWallpaperConfig",
    "disableUiSeasonalTheming", "removeAutoStartCompletely", "fixAutoStart",
    "openFromFileLegacy", "cancelMobileUploads", "getWallpaperPresets",
    "cancelAndClose", "resetWallpaperLocalStorage", "setPropertyDefaults",
    "showSettingsDialog", "showEditorDialog", "openInEditor", "openDirectory",
    "convertWorkshopBackup", "preparePublishPreset",
    "openSystemWallpaperDialog", "toggleMiniMode", "changeLayout",
    "getLanInfo", "ensureThumb", "persistBrowserSettings",
    "persistSharedSettings",
    "persistUserMonitorSettings", "playlistsChanged", "profilesChanged",
    "toggleMaximize", "createInEditor", "checkEdgeWebViewAvailable",
    "installEdgeWebView", "checkFirewall", "fixFirewall", "removeMobileDevice",
    "getUserShortcutSystemHash", "testUserShortcut", "exportMobilePkg",
    "requestMobileDeviceUpload", "getScenePreviewImage",
    "getWallpaperResolutions", "checkWallpaperPKGVersions",
};
static const std::vector<std::string> kSettingsMethods = {
    "toggleWallpaperOverride", "toggleLockscreenOverride",
    "getApplicationsPlayingAudio", "restartApp", "toggleAutoStartRegistry",
    "toggleAutoStartHighPriority", "resetConfig", "openWebWallpaperDevtools",
    "languageChanged", "persistSharedSettings", "getRunningApplications",
    "getWallpaperCache", "getCurrentMediaSession", "updateManagedMonitors",
};
static const std::vector<std::string> kWelcomeMethods = {
    "toggleAutoStartRegistry", "toggleAutoStartHighPriority", "languageChanged",
};


WallpaperCefApp::WallpaperCefApp(const std::wstring& docRoot,
                                 const std::wstring& startUrl,
                                 const std::string& language)
    : docRoot_(docRoot), startUrl_(startUrl), language_(language) {}

CefRefPtr<CefBrowserProcessHandler> WallpaperCefApp::GetBrowserProcessHandler() {
    return this;
}
CefRefPtr<CefRenderProcessHandler> WallpaperCefApp::GetRenderProcessHandler() {
    return this;
}

void WallpaperCefApp::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
    // Entornos sin GPU (VM/RDP): software rendering. Evita el crash del
    // gpu-process observado (exit_code=-1) que dejaba la vista en blanco.
    (void)process_type;
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
    command_line->AppendSwitch("disable-gpu-sandbox");
    command_line->AppendSwitch("no-sandbox");
    // La vista previa de Pantallas reproduce varios vídeos a la vez. Sin esto
    // Chromium sólo deja arrancar el primero (política de autoplay) y el resto
    // se queda en el primer fotograma.
    command_line->AppendSwitchWithValue("autoplay-policy",
                                        "no-user-gesture-required");
}

bool WallpaperCefApp::OnAlreadyRunningAppRelaunch(
    CefRefPtr<CefCommandLine> command_line,
    const CefString& current_directory) {
    (void)command_line;
    (void)current_directory;
    // Instancia única gestionada por mutex en CefEngine::Init.
    return true;
}

void WallpaperCefApp::OnContextInitialized() {
    // Las URLs son http://wpx.internal/... y http://wpx.app/... (scheme
    // "http", host personalizado): la factory se registra por DOMINIO
    // sobre el scheme http (patrón CEF para hosts internos).
    CefRegisterSchemeHandlerFactory(
        "http", "wpx.internal", new WpxSchemeHandlerFactory(docRoot_));
    CefRegisterSchemeHandlerFactory(
        "http", "wpx.app", new WpxSchemeHandlerFactory(docRoot_));

    // Interfaz nueva: el catálogo y el servidor LAN se levantan AQUÍ (proceso
    // de navegador), no en el renderer. Si vivieran en el renderer, al navegar
    // a /ui2/ ese proceso se destruiría y con él el servidor: la ventana se
    // quedaba con el HTML pero sin JS (todo /api/* fallaba).
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        size_t k = dir.find_last_of(L"\\/");
        if (k != std::wstring::npos) dir = dir.substr(0, k);

        bool useNew = true;
        FILE* f = nullptr;
        _wfopen_s(&f, (dir + L"\\res\\config\\ui.json").c_str(), L"rb");
        if (f) {
            char buf[1024] = {};
            size_t got = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            if (std::string(buf, got).find("\"classic\"") != std::string::npos) {
                useNew = false;
            }
        }
        if (useNew) {
            LoadCatalogSnapshot(dir);
            // El servidor corre en este proceso: necesita la lista de plugins
            // (el escaneo lo hace el renderer y deja el snapshot en disco).
            SetKnownPlugins(DiscoverPlugins(dir));
            StartCatalogSaver(dir);
            StartLanServer(dir);
            int port = 0;
            for (int i = 0; i < 40 && port <= 0; ++i) {
                port = LanPort();
                if (port <= 0) Sleep(100);
            }
            if (port > 0) {
                startUrl_ = L"http://127.0.0.1:" + std::to_wstring(port) +
                            L"/ui2/";
            }
        }
    }

    CefWindowInfo window_info;
    window_info.SetAsPopup(nullptr, "DNXWallpaper v3");
    CefBrowserSettings settings;
    CefRefPtr<WallpaperCefClient> client(new WallpaperCefClient(this));
    CefBrowserHost::CreateBrowser(window_info, client,
                                  CefString(startUrl_), settings,
                                  nullptr, nullptr);
}


void WallpaperCefApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       CefRefPtr<CefV8Context> context) {
    // El catálogo de plugins vive en este proceso (renderer): el servidor
    // HTTP para la app móvil debe arrancar aquí. Idempotente por proceso.
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        size_t k = dir.find_last_of(L"\\/");
        if (k != std::wstring::npos) dir = dir.substr(0, k);
        // Cargar el snapshot ANTES de arrancar el servidor LAN: la app móvil
        // y la UI ven el catálogo al instante aunque el PC acabe de arrancar.
        LoadCatalogSnapshot(dir);
        // En modo "interfaz nueva" el servidor lo levanta el proceso de
        // navegador (OnContextInitialized). Si arrancáramos otro aquí, el
        // segundo encontraría el 8477 ocupado y se iría al 8478.
        if (!UseNewUi(dir)) StartLanServer(dir);
    }

    auto trace = [](const char* s) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        dir = dir.substr(0, dir.find_last_of(L"\\/"));
        FILE* f = nullptr;
        _wfopen_s(&f, (dir + L"\\js-calls.log").c_str(), L"a, ccs=UTF-8");
        if (f) {
            fwprintf(f, L"CTX %hs\n", s);
            fclose(f);
        }
    };
    trace("OnContextCreated");
    if (!g_dispatcher) {
        g_dispatcher = WallpaperUiDispatcher();
    }
    BindV8Object(context, browser, g_dispatcher, "ui", kUiMethods);
    trace("ui bound");
    BindV8Object(context, browser, g_dispatcher, "browseWallpaperObject",
                 kBrowseMethods);
    BindV8Object(context, browser, g_dispatcher, "settingsObject",
                 kSettingsMethods);
    BindV8Object(context, browser, g_dispatcher, "welcomeObject",
                 kWelcomeMethods);
    trace("browseWallpaperObject bound");
    // "Descubrir" local: al activar esa pestaña la UI consulta Workshop
    // (Steam). Sin Steam, se inyecta nuestro inventario local mediante
    // updateWallpapers, de modo que la pestaña muestra los wallpapers
    // disponibles en las carpetas configuradas.
    if (auto fr = browser ? browser->GetMainFrame() : nullptr) {
        fr->ExecuteJavaScript(R"JS(
(function(){
 if(window.__wpxCat)return; window.__wpxCat=1;
 window.__wpxPluginId='';   // '' = Todos

 // Panel "Properties / Your Presets / Workshop Presets" del original.
 // Depende de Steam Workshop (propiedades del proyecto, presets, comentarios)
 // y en este build no se usa. Se oculta con CSS porque Angular lo vuelve a
 // crear en cada digest: borrar los nodos producia parpadeos.
 (function wpxHideWorkshopPanel(){
   // OJO: este script corre en OnContextCreated, antes de que exista el
   // documento (document.head/documentElement == null). Cualquier excepcion
   // aqui abortaria TODA la inyeccion, asi que va todo protegido y diferido.
   function add(){
     try{
       if(!document.documentElement) return;
       if(document.getElementById('wpxHideCss')) return;
       var st=document.createElement('style');
       st.id='wpxHideCss';
       st.textContent=
         '.browseWallpaperGridPlaylistHeader a.disabled{pointer-events:auto !important;opacity:.6}'+
         /* Los botones de ventana del original duplican los de la barra de
            Windows: se deja sólo el de mini modo (>>). */
         '.titlebar .titlebarBtn.windowButton,.titlebar .titlebarBtn.closeButton{display:none !important}'+
         '.browseWallpaperMiniMode .browseWallpaperDetailView{min-width:0 !important;max-width:none !important}'+
         '.browseWallpaperDetailsProperties{display:none !important}'+
         '.browseWallpaperDetailsWorkshopControls{display:none !important}'+
         'a[ng-click*="openSteamWorkshopPage"]{display:none !important}'+
         'a[ng-click*="callbackShowEditor"],a[ng-click*="callbackOpenInEditor"]{display:none !important}';
       var root=document.head||document.body||document.documentElement;
       if(!root) return;
       root.appendChild(st);
     }catch(e){}
   }
   setInterval(add,400);   // reintenta hasta que exista el DOM
 })();

 function cat(){
   var c=window.browseWallpapersCtrl; if(!c) return null;
   return c;
 }
 // updateWallpapers FUSIONA; para cambiar de catálogo hay que vaciar.
 function setCatalog(list, mode){
   var c=cat(); if(!c) return;
   try{
     var L=(list||[]).slice();
     if(mode==='plugins'&&!L.length&&window.__wpxLoading) return;
     var prev=c.currentSelection;
     if(mode==='plugins'){
       // El grid en Workshop/Discover se alimenta de queryWallpapers.
       c.queryWallpapers=L;
       c.sortedWallpapers=L.slice();
       c.sortedWallpapersPrePagination=L.slice();
     }else{
       c.wallpapers=L;
       if(c.sortWallpapers) c.sortWallpapers();
     }
     if(prev){ // conservar selección entre refrescos del catálogo
       var keep=undefined;
       for(var i=0;i<L.length;i++){
         if((prev.workshopid&&L[i].workshopid===prev.workshopid)||
            (prev.file&&L[i].file===prev.file)){keep=L[i];break;}
       }
       c.currentSelection=keep;
     }
     if(c.$apply) c.$apply();
   }catch(e){}
 }
 function filtered(){
   var all=window.__wpxRemote||[];
   var id=window.__wpxPluginId||'';
   var out=all.filter(function(x){return !id||x.provider===id;});
   if(window.__wpxPasses) out=out.filter(window.__wpxPasses);
   return out;
 }
 window.__wpxSetCatalog=setCatalog;
 // Pide al host la miniatura animada de los primeros ítems visibles
 // (máx. 16, con freno de 4 s y sin repetir). El host decide si hace falta.
 window.__wpxWarmed={};
 window.__wpxWarmVisible=function(list){
   var b=window.browseWallpaperObject;
   if(!b||!b.ensureThumb) return;
   var now=Date.now();
   if(now-(window.__wpxWarmAt||0)<4000) return;
   window.__wpxWarmAt=now;
   var n=0;
   for(var i=0;i<list.length&&n<16;i++){
     var it=list[i];
     if(!it||!it.file||it.file.indexOf('wpxr:')!==0) continue;
     var k=it.file.substr(5);
     if(window.__wpxWarmed[k]) continue;
     window.__wpxWarmed[k]=1; ++n;
     try{ b.ensureThumb(k); }catch(e){}
   }
 };

 window.__wpxRefresh=function(){
   if(!window.__wpxRemote||!window.__wpxRemote.length) return;
   var c=cat();
   if(c&&c.sourceIsWorkshop){
     var list=filtered();
     setCatalog(list,'plugins');
     if(window.__wpxWarmVisible) window.__wpxWarmVisible(list);
   }
 };

 // La UI pide al "servidor" (queryWorkshop) cada vez que cambia un filtro.
 // Sin Steam esa llamada falla y la UI muestra el modal/pagina
 // "Steam Services Unavailable". Se responde en local, al instante, con el
 // catalogo ya filtrado por __wpxPasses.
 (function wpxWorkshopShim(){
   function install(){
     try{
       var b=window.browseWallpaperObject;
       if(!b||b.__wpxQueryShim) return;
       b.__wpxQueryShim=1;
       b.queryWorkshop=function(spec){
         try{
           var sp=spec;
           if(typeof sp==='string'){ try{ sp=JSON.parse(sp); }catch(e){ sp={}; } }
           var items=filtered();
           var res={ token:(sp&&sp.token)||0, isbackup:false,
                     pagecount:1, wallpapers:items };
           if(window.queryWorkshopCallback) window.queryWorkshopCallback(res);
           return true;
         }catch(e){
           if(window.queryWorkshopCallbackError)
             window.queryWorkshopCallbackError(1,String(e));
           return false;
         }
       };
     }catch(e){}
   }
   install(); setInterval(install,1000);
 })();

  function fetchRemote(){
    var b=window.browseWallpaperObject; if(!b||!b.getRemoteItems) return;
    if(window.__wpxFetching) return;
    window.__wpxFetching=1;
    window.getRemoteItemsCallback=function(list){
      window.__wpxFetching=0;
      window.__wpxRemote=list||[];
      window.__wpxRemoteAt=Date.now();
      window.__wpxVerDone=window.__wpxVer;
      var c=cat();
      if(c&&c.sourceIsWorkshop) setCatalog(filtered());
    };
    b.getRemoteItems();
  }
  // 1) Remotos: se piden al arrancar y cuando cambia la versión del
  // catálogo (miniaturas nuevas), evitando re-transferir 300 KB cada 2,5 s.
  setInterval(function(){try{
    var b=window.browseWallpaperObject; if(!b||!b.getRemoteVersion){
      if(!window.__wpxRemote) fetchRemote();
      return;
    }
    window.getRemoteVersionCallback=function(v){
      v=parseInt(v,10)||0;
      if(v===window.__wpxVer) return;
      window.__wpxVer=v;
      if(window.__wpxVerPending) return;
      window.__wpxVerPending=setTimeout(function(){
        window.__wpxVerPending=0;
        var cc=window.browseWallpapersCtrl;
        // Mientras el usuario está en Instalados no hace falta transferir el
        // catálogo remoto; se refresca al volver a Plugins.
        if(window.__wpxRemote && cc && !cc.sourceIsWorkshop) return;
        if(window.__wpxRemote) fetchRemote();
      },3000);
    };
    b.getRemoteVersion();
  }catch(e){}},2000);

  // 2) Cambio de pestaña: Instalados=locales, Plugins=remotos.
  var last='';
  setInterval(function(){try{
    var c=cat(); if(!c) return;
    // La pestaña Workshop original exige Steam (guard en setListSource y
    // muestra modal de error). Sin Steam interceptamos solo ese destino y
    // replicamos el cambio de estado para que el grid use queryWallpapers.
    if(!c.__wpxPatched && c.setListSource){
      c.__wpxPatched=true;
      var origSet=c.setListSource;
      c.setListSource=function(e){
        if(e==='explore') return;
        if(e==='workshop'){
          if(c.source===e) return;
          try{
            c.queryWallpapers=filtered();
            c.sortedWallpapers=[];
            c.source=e;
            c.sourceIsWorkshop=true;
            c.sourceIsExplore=false;
            c.sourceIsInstalled=false;
            c.queryActive=false;
            c.pagination={current:1,count:1};
            if(c.$apply) c.$apply();
          }catch(_){}
          return;
        }
        return origSet.apply(c,arguments);
      };
    }
    var m=c.sourceIsWorkshop?'plugins':'installed';
   var desired = (m==='plugins') ? filtered() : (window.__wpxLocal||[]);
    // Cambio de pestaña o catálogo contaminado por otra escritura: rehacer.
    var cur = (m==='plugins') ? (c.queryWallpapers||[]) : (c.wallpapers||[]);
    var need = (m!==last) || (cur.length!==desired.length);
   last=m;
   if(need && desired.length) setCatalog(desired,m);
   if(m==='plugins' && ((!window.__wpxRemote || !window.__wpxRemote.length) ||
       (window.__wpxVer && window.__wpxVer!==window.__wpxVerDone))){
     fetchRemote();
   }else if(m==='installed' && (!window.__wpxLocal||!window.__wpxLocal.length)){
     var b2=window.browseWallpaperObject;
     if(b2){ window.getLocalItemsCallback=function(list){
       window.__wpxLocal=list||[];
       if(cat() && !cat().sourceIsWorkshop) setCatalog(window.__wpxLocal);};
       b2.getLocalItems(); }
   }
 }catch(e){}},600);

 // 3) Renombrar "Workshop" -> "Plugins" y sustituir el icono de Steam.
 setInterval(function(){try{
   var links=document.querySelectorAll('a');
   for(var j=0;j<links.length;j++){var a=links[j];
     var ng=a.getAttribute('ng-click')||'';
     if(ng.indexOf('workshop')<0) continue;
     var sp=a.querySelector('span[ng-class="providerIcon"]');
     if(sp) sp.className='fas fa-plug';
     var txt=a.querySelector('span[translate]');
     if(txt && (txt.textContent||'').trim()==='Workshop') txt.textContent='Plugins';
   }
   if(window.__wpxRenamed)return;
   var all=[].slice.call(document.querySelectorAll('span,div,a,button'));
   for(var i=0;i<all.length;i++){var e=all[i];
     if(e.children.length===0&&(e.textContent||'').trim()==='Workshop'){
       e.textContent='Plugins';window.__wpxRenamed=1;break;}}
 }catch(e){}},700);

 // 4) Selector de plugin (izquierda del buscador), solo en Plugins.
 function buildSelector(){
   var c=cat(); if(!c) return;
   var inp=document.querySelector('input[placeholder]');
   if(!inp) return;
   var bar=document.getElementById('wpxPlugSel');
   if(!bar){
     if(!inp.parentNode) return;
     var sel=document.createElement('select');
     sel.id='wpxPlugSel';
     sel.style.cssText='margin-right:8px;min-width:150px;height:28px;'+
       'background:#242428;color:#eee;border:1px solid #555;border-radius:4px;';
     sel.onchange=function(){
       window.__wpxPluginId=this.value||'';
       setCatalog(filtered(),'plugins');
     };
     inp.parentNode.insertBefore(sel, inp);
     bar=sel;
     var b=window.browseWallpaperObject;
     window.getPluginListCallback=function(list){
       sel.innerHTML='';
       var o=document.createElement('option');o.value='';o.textContent='Todos los plugins';
       sel.appendChild(o);
       (list||[]).forEach(function(p){
         var x=document.createElement('option');x.value=p.id;x.textContent=p.name||p.id;
         sel.appendChild(x);
       });
       sel.value=window.__wpxPluginId||'';
       if(!window.__wpxPluginId){sel.selectedIndex=0;}
     };
     if(b&&b.getPluginList) b.getPluginList();
   }
   bar.style.display=c.sourceIsWorkshop?'inline-block':'none';
 }
 setInterval(buildSelector,900);
})();
)JS", fr->GetURL(), 0);
    }
    // ================= Fase 0/1/2: UI local (descargas, filtros, ----------
    // settings, playlist, metadatos, monitores y modo mini) -----------------
    if (auto fr = browser ? browser->GetMainFrame() : nullptr) {
        fr->ExecuteJavaScript(R"JS(
(function(){
 if(window.__wpxP2)return; window.__wpxP2=1;
 function ctrl(){return window.browseWallpapersCtrl;}
 function applyNow(){var c=ctrl();
   if(!c||!c.$apply) return;
   try{ if(c.$root&&c.$root.$$phase) return; if(c.$$phase) return; c.$apply(); }catch(e){}
 }
 // Callbacks de bridges que llamamos sin esperar respuesta.
 ['savePlaylistCallback','activatePlaylistCallback','saveOptimizationsCallback',
  'saveDisabledPluginsCallback','setAutostartCallback','savePlaybackModeCallback',
  'persistSharedSettingsCallback','copyToClipboardCallback'].forEach(function(n){
   if(!window[n]) window[n]=function(){};
 });
 function txt(el){return (el&&el.textContent||'').trim();}
 function isRemote(it){return it&&((it.workshopid)||(it.file||'').indexOf('wpxr:')===0);}

 // ---------- 1) Descargas: estado + barra en la miniatura ----------
 function bindKey(it){
   var tiles=document.querySelectorAll('.browseWallpaperImage');
   var title=(it.title||'').replace(/\s*\[Animado\]\s*$/,'');
   for(var i=0;i<tiles.length;i++){
     var t=tiles[i];
     if(it.workshopid && t.getAttribute('data-wpxkey')===it.workshopid) return t;
     if(title && txt(t).indexOf(title)>=0){ t.setAttribute('data-wpxkey', it.workshopid||it.file||''); return t; }
   }
   return null;
 }
 function barFor(tile,key){
   var id='wpxbar_'+(key||'').replace(/[^a-z0-9]/gi,'_');
   var bar=document.getElementById(id);
   if(!bar){
     bar=document.createElement('div'); bar.id=id;
     bar.style.cssText='position:absolute;left:0;bottom:0;height:5px;width:100%;'+
       'background:rgba(0,0,0,.45);z-index:9;display:none;';
     var f=document.createElement('div'); f.className='wpxfill';
     f.style.cssText='height:100%;width:0%;background:#2f7cf6;transition:width .25s;';
     bar.appendChild(f);
     tile.style.position='relative';
     tile.appendChild(bar);
   }
   return bar;
 }
 function renderBars(){
   var list=window.__wpxRemote||[];
   for(var i=0;i<list.length;i++){
     var it=list[i], dl=it.__dl;
     if(!dl) continue;
     if(!dl.active){
       if(!dl.__ts) dl.__ts=Date.now();
       if(Date.now()-dl.__ts>4000){ continue; }
     } else { dl.__ts=0; }
     var tile=bindKey(it); if(!tile) continue;
     var key=it.workshopid||it.file||'';
     var bar=barFor(tile,key), fill=bar.firstChild;
     bar.style.display='block';
     if(dl.error){ fill.style.background='#c0392b'; fill.style.width='100%'; bar.title=dl.message||'Error'; }
     else if(dl.done){ fill.style.background='#27ae60'; fill.style.width='100%'; bar.title='Completado';
       if(!bar.__hide){ bar.__hide=1; setTimeout(function(){ try{bar.remove();}catch(e){} },1500); } }
     else {
       var pct=dl.total>0?Math.min(100,Math.round(dl.bytes*100/dl.total)):0;
       fill.style.background='#2f7cf6';
       fill.style.width=(pct>0?pct:8)+'%';
       bar.title=dl.total>0?(pct+'%'):(Math.round(dl.bytes/1048576)+' MB');
     }
   }
 }
 setInterval(function(){
   var b=window.browseWallpaperObject; if(!b||!b.getDownloadStatus)return;
   window.getDownloadStatusCallback=function(arr){
     arr=arr||[]; var list=window.__wpxRemote||[]; var map={};
     for(var i=0;i<arr.length;i++) map[arr[i].key]=arr[i];
     var changed=false;
     for(var j=0;j<list.length;j++){
       var it=list[j], s=map[it.workshopid];
       if(!s) continue;
       if(s.active||s.done||s.error){ it.__dl=s;
         if(s.done && it.status!=='installed'){ it.status='installed'; changed=true; } }
     }
     if(changed) applyNow();
     renderBars();
   };
   b.getDownloadStatus();
 },700);

 // ---------- 2) Filtros: datos + filtrado en cliente ----------
 function resTag(w,h){return {value:w+'x'+h,label:w+' x '+h};}
 function initFilters(){
   try{
     if(window.__wpxFiltersInit) return true;
     var el=document.querySelector('[ng-app]'); if(!el) return false;
     var inj=angular.element(el).injector(); if(!inj) return false;
     var H=inj.get('utils'); if(!H||!H.setAvailableTags) return false;
     var root=inj.get('$rootScope');
     var res=[];
     [[1280,720],[1366,768],[1600,900],[1920,1080],[2560,1080],[2560,1440],
      [3440,1440],[3840,1080],[3840,1600],[3840,2160],[5120,1440],[5760,1080],
      [1080,1920],[1440,2560],[2160,3840],[1024,768],[1280,1024],[1440,1080],
      [1920,1200],[2560,1600],[1920,1440]].forEach(function(s){res.push(resTag(s[0],s[1]));});
     H.setAvailableTags(['Anime','Girls','Guys','Music','MMD','Nature','Cars',
       'Abstract','Unspecified'], ['Approved','Age Restricted'], res);
     root.$broadcast('onCppInit', false);
     window.__wpxFiltersInit=1;
     setTimeout(function(){
       try{
         var c=ctrl(); if(!c||!c.filterTypeTags) return;
         var has=false; for(var i=0;i<c.filterTypeTags.length;i++) if(c.filterTypeTags[i].value==='Image') has=true;
         if(!has) c.filterTypeTags.push({label:'Imagen',value:'Image'});
         if(c.filter&&c.filter.typetags&&c.filter.typetags.Image===undefined) c.filter.typetags.Image=true;
       }catch(e){}
     },600);
     return true;
   }catch(e){return false;}
 }
 setInterval(function(){ initFilters(); },600);

 window.__wpxPasses=function(it){
   var c=ctrl(); if(!c||!c.filter) return true;
   var f=c.filter;
   function passesTags(map,key){
     if(!map) return true;
     var any=false;
     for(var k in map){ if(map[k]){ any=true; break; } }
     if(!any) return true;              // nada marcado = sin restriccion
     return map[key]===true;
   }
   if(f.typetags){
     if(f.typetags[it.type]===false) return false;
     if(it.type==='Image'&&f.typetags.Image===false) return false;
     if(!passesTags(f.typetags, it.type)) return false;
   }
   if(f.categorytags&&f.categorytags.Wallpaper===false) return false;
   if(f.sourcetags&&f.sourcetags.Local===false&&f.sourcetags.Workshop===false) return false;
   if(f.resolutiontags&&it.resolution){
     var k=it.resolution.width+'x'+it.resolution.height;
     if(f.resolutiontags[k]===false) return false;
     // Con casillas marcadas, sólo pasan esas resoluciones.
     if(!passesTags(f.resolutiontags, k)) return false;
   }
   if(f.text){
     var q=(f.text||'').toLowerCase().trim();
     if(q&&(it.title||'').toLowerCase().indexOf(q)<0) return false;
   }
   return true;
 };
 var lastSig='';
 setInterval(function(){
   var c=ctrl(); if(!c||!c.filter||!c.sourceIsWorkshop) return;
   var f=c.filter;
   var sig=JSON.stringify([f.typetags,f.categorytags,f.sourcetags,f.resolutiontags,f.text]);
   if(sig!==lastSig){ lastSig=sig; if(window.__wpxRefresh) window.__wpxRefresh(); }
 },900);

 // Buscador: filtrar en cliente (sin Steam no hay consulta remota).
 document.addEventListener('input',function(e){
   var t=e.target; if(!t||t.tagName!=='INPUT')return;
   var ph=(t.getAttribute('placeholder')||'').toLowerCase();
   if(ph.indexOf('search')<0&&ph.indexOf('buscar')<0)return;
   e.stopImmediatePropagation();
   var v=t.value||'';
   // Debounce: re-renderizar 1578 items en cada tecla hacia que el texto
   // se pintara mal y con retardo.
   clearTimeout(window.__wpxSearchT);
   window.__wpxSearchT=setTimeout(function(){
     var c=ctrl(); if(c&&c.filter){ c.filter.text=v;
       if(window.__wpxRefresh)window.__wpxRefresh(); }
   },250);
 },true);

 // ---------- 3) Carga de plugins ----------
 function ensureLoading(){
   var el=document.getElementById('wpxLoading');
   if(!el){ el=document.createElement('div'); el.id='wpxLoading';
     el.style.cssText='position:absolute;left:50%;top:120px;transform:translateX(-50%);'+
       'z-index:20;color:#bbb;font:14px "Segoe UI",sans-serif;text-align:center;'+
       'background:rgba(20,20,22,.82);padding:10px 18px;border-radius:6px;display:none;';
     document.body.appendChild(el);
   }
   return el;
 }
 setInterval(function(){
   var b=window.browseWallpaperObject; if(!b||!b.getPluginStatus)return;
   window.getPluginStatusCallback=function(s){
     var el=ensureLoading();
     var was=window.__wpxWasLoading;
     window.__wpxWasLoading=!!(s&&s.loading);
     if(s&&s.loading){
       var c=ctrl();
       var empty=!c||!c.queryWallpapers||!c.queryWallpapers.length;
       window.__wpxLoading=true;
       if(empty){
         el.textContent='Cargando plugins… ('+(s.done||0)+'/'+(s.total||0)+')';
         el.style.display='block';
       } else { el.style.display='none'; }
     } else {
       window.__wpxLoading=false;
       el.style.display='none';
       if(was && window.__wpxRefresh) window.__wpxRefresh();
     }
   };
   b.getPluginStatus();
 },1200);

 // ---------- 4) Preview: metadatos + ocultar bloques Steam ----------
 var stealOpen=false;
 function hideSteamBlocks(scope){
   // Todo lo de Steam (propiedades, presets, comentarios) lo oculta la hoja
   // de estilo 'wpxHideCss' de forma permanente. Aqui no se toca el DOM:
   // Angular recrea esos nodos y borrarlos provocaba parpadeos.
   return;
 }
 function metaHtml(it){
   var type=it.type==='Image'?'Imagen':'Vídeo';
   return '<div style="margin-top:14px;font:12px \'Segoe UI\',sans-serif;color:#333;line-height:1.7;">'+
     '<div style="font-weight:600;color:#111;margin-bottom:6px;">Información</div>'+
     '<div>Tipo: <b id="wpxType">'+type+'</b></div>'+
     '<div>Resolución: <b id="wpxRes">—</b></div>'+
     '<div>Aspect ratio: <b id="wpxAspect">—</b></div>'+
     '<div>Tamaño: <b id="wpxSize">—</b></div>'+
     '<div>Plugin: <b id="wpxPlugin">—</b></div>'+
     '<div style="margin-top:10px;">Ajuste de imagen:</div>'+
     '<select id="wpxMode" style="width:100%;height:26px;background:#fff;color:#222;border:1px solid #bbb;border-radius:4px;">'+
     '<option value="fill">Rellenar (recortar)</option>'+
     '<option value="fit">Ajustar (barras)</option>'+
     '<option value="stretch">Estirar</option></select>'+
     '<button id="wpxDel" style="margin-top:10px;width:100%;height:28px;background:#8e2f2f;color:#fff;border:0;border-radius:4px;">Borrar descarga</button>'+
     '<div style="margin-top:16px;font-weight:600;color:#111;">Enviar al móvil</div>'+
     '<div id="wpxQr" style="margin-top:6px;"></div>'+
     '<div id="wpxQrUrl" style="font:10px Consolas,monospace;color:#666;word-break:break-all;margin-top:4px;"></div>'+
     '</div>';
 }
 // Refresca sólo los textos (nunca reconstruye el HTML: si se reemplaza el
 // <select> mientras su desplegable está abierto, Windows lo cierra al
 // instante y el combo parece "no abrirse").
 function metaFill(it){
   var res=it.resolution? (it.resolution.width+' x '+it.resolution.height):'—';
   var aspect=it.resolution? (it.resolution.width/it.resolution.height).toFixed(2):'—';
   var size='—';
   if(it.__dl&&it.__dl.bytes) size=(it.__dl.bytes/1048576).toFixed(1)+' MB';
   else if(it.sizeBytes) size=(it.sizeBytes/1048576).toFixed(1)+' MB';
   else if(it.__dl&&it.__dl.total) size='~'+(it.__dl.total/1048576).toFixed(1)+' MB';
   function set(id,v){ var e=document.getElementById(id); if(e&&e.textContent!==v) e.textContent=v; }
   set('wpxType', it.type==='Image'?'Imagen':'Vídeo');
   set('wpxRes', res); set('wpxAspect', aspect);
   set('wpxSize', size); set('wpxPlugin', it.provider||'—');
 }
 // ---------- QR para la app móvil ----------
 window.__wpxLan=null;
 (function wpxLanFetch(){
   function ask(){
     var b=window.browseWallpaperObject;
     if(!b||!b.getLanInfo) return;
     window.getLanInfoCallback=function(o){ window.__wpxLan=o||null;
       if(window.__wpxRefreshQr) window.__wpxRefreshQr(); };
     try{ b.getLanInfo(); }catch(e){}
   }
   setTimeout(ask,1500);
   setInterval(ask,30000);
 })();

 function ensureQrLib(cb){
   if(window.QRCode) { cb(); return; }
   if(window.__wpxQrLoading){ setTimeout(function(){ ensureQrLib(cb); },200); return; }
   window.__wpxQrLoading=1;
   var s=document.createElement('script');
   s.src='http://wpx.internal/scripts/qrcode.min.js';
   s.onload=function(){ window.__wpxQrLoading=0; cb(); };
   s.onerror=function(){ window.__wpxQrLoading=0; };
   (document.head||document.documentElement).appendChild(s);
 }

 function qrUrlFor(sel){
   if(!sel||!sel.file||sel.file.indexOf('wpxr:')!==0) return '';
   var li=window.__wpxLan;
   if(!li||!li.ip||!li.enabled) return '';
   return 'http://'+li.ip+':'+li.port+'/dl/'+encodeURIComponent(sel.file.substr(5));
 }

 function renderQr(sel){
   var box=document.getElementById('wpxQr');
   var txt=document.getElementById('wpxQrUrl');
   if(!box) return;
   var url=qrUrlFor(sel);
   if(!url){
     box.innerHTML='';
     if(txt) txt.textContent = window.__wpxLan
       ? '' : 'Iniciando servicio…';
     return;
   }
   if(box.getAttribute('data-url')===url) return;
   box.setAttribute('data-url',url);
   box.innerHTML='<div id="wpxQrCanvas" style="background:#fff;padding:6px;border-radius:6px;display:inline-block;"></div>';
   if(txt) txt.textContent=url;
   ensureQrLib(function(){
     try{
       new QRCode(document.getElementById('wpxQrCanvas'),{
         text:url, width:132, height:132,
         correctLevel: QRCode.CorrectLevel.M
       });
     }catch(e){}
   });
 }

 function syncPreview(){
   var c=ctrl(); if(!c) return;
   var host=document.querySelector('.browseWallpaperDetailView .details');
   if(!host) return;
   var box=document.getElementById('wpxMeta');
   if(!box){
     box=document.createElement('div'); box.id='wpxMeta';
     host.appendChild(box); box.__key=null;
   }
   var sel=c.currentSelection;
   if(!isRemote(sel)){ box.style.display='none'; return; }
   if(!stealOpen){ hideSteamBlocks(host); }
   var key=(sel.workshopid||sel.file||sel.title||'');
   if(box.__key!==key){
     box.innerHTML=metaHtml(sel);
     box.__key=key;
     var m=document.getElementById('wpxMode');
     if(m){
       m.value=(window.__wpxPlaybackMode||'fill');
       m.onchange=function(){
         window.__wpxPlaybackMode=this.value;
         var b=window.browseWallpaperObject;
         if(b&&b.savePlaybackMode) b.savePlaybackMode(this.value);
       };
     }
   }
   metaFill(sel);
   box.style.display='block';
   if(!window.__wpxRefreshQr) window.__wpxRefreshQr=function(){ renderQr(sel); };
   renderQr(sel);
 }
 setInterval(syncPreview,800);

 // ---------- 5) Modales: settings / playlist / displays ----------
 function modal(title,html,wide){
   var ov=document.getElementById('wpxModal');
   if(ov) ov.remove();
   ov=document.createElement('div'); ov.id='wpxModal';
   ov.style.cssText='position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:9999;display:flex;'+
     'align-items:center;justify-content:center;font:13px "Segoe UI",sans-serif;';
   var box=document.createElement('div');
   box.style.cssText='background:#1d1d20;color:#e8e8e8;border:1px solid #3a3a40;border-radius:8px;'+
     'width:'+(wide?'620px':'460px')+';max-height:82vh;overflow:auto;padding:18px 20px;box-shadow:0 12px 40px rgba(0,0,0,.5);';
   box.innerHTML='<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;">'+
     '<div style="font-size:15px;font-weight:600;">'+title+'</div>'+
     '<div id="wpxModalX" style="cursor:pointer;font-size:18px;color:#aaa;">✕</div></div>'+
     '<div id="wpxModalBody">'+html+'</div>';
   ov.appendChild(box); document.body.appendChild(ov);
   document.getElementById('wpxModalX').onclick=function(){ov.remove();};
   ov.onclick=function(e){if(e.target===ov)ov.remove();};
   return box;
 }
 function fieldRow(label,inner){return '<div style="display:flex;justify-content:space-between;align-items:center;margin:8px 0;">'+
   '<span>'+label+'</span><span>'+inner+'</span></div>';}
 function checkbox(id,val){return '<input id="'+id+'" type="checkbox"'+(val?' checked':'')+'>';}

 function openSettings(){
   modal('Configuración','<div id="wpxSetBody">Cargando…</div>',true);
   var b=window.browseWallpaperObject;
   window.getLocalSettingsCallback=function(s){
     var body=document.getElementById('wpxSetBody'); if(!body) return;
     s=s||{}; var o=s.optimizations||{}, st=s.settings||{}, pl=s.playback||{};
     o.fps=o.fps||30;
     var dis=(s.plugins&&s.plugins.disabled)||[];
     var html='<div style="font-weight:600;margin:6px 0 4px;">General</div>'+
       fieldRow('Iniciar con Windows',checkbox('wpxAuto',s.autostart))+
       fieldRow('Idioma (UI)', '<select id="wpxLang" style="height:24px;background:#242428;color:#eee;"><option>es</option><option>en</option></select>')+
       '<div style="font-weight:600;margin:14px 0 4px;">Rendimiento</div>'+
       fieldRow('FPS del fondo','<input id="wpxFps" type="number" min="5" max="144" value="'+o.fps+'" style="width:70px;background:#242428;color:#eee;border:1px solid #555;">')+
       fieldRow('Pausar al ocultar',checkbox('wpxP1',o.pauseWhenHidden!==false))+
       fieldRow('Pausar en pantalla completa',checkbox('wpxP2',o.pauseOnFullscreen!==false))+
       fieldRow('Pausar maximizado',checkbox('wpxP3',!!o.pauseOnMaximized))+
       fieldRow('Pausar en batería',checkbox('wpxP4',!!o.pauseOnBattery))+
       fieldRow('Pausar al bloquear',checkbox('wpxP5',o.pauseOnSessionLock!==false))+
       fieldRow('Calidad','<select id="wpxQuality" style="background:#242428;color:#eee;">'+
         '<option value="auto">Automática (nativa)</option>'+
         '<option value="medium">Media (máx. 1080p)</option>'+
         '<option value="low">Baja (máx. 720p)</option></select>')+
       fieldRow('HDR (si el monitor lo soporta)',checkbox('wpxHdr',o.hdr!==false))+
       '<div style="font-weight:600;margin:14px 0 4px;">Reproducción</div>'+
       fieldRow('Ajuste','<select id="wpxPlay" style="background:#242428;color:#eee;"><option value="fill">Rellenar</option><option value="fit">Ajustar</option><option value="stretch">Estirar</option></select>')+
       fieldRow('Audio del vídeo',checkbox('wpxAudio',!!pl.audio))+
       '<div style="font-weight:600;margin:14px 0 4px;">Plugins</div><div id="wpxPlugList">Cargando…</div>'+
       '<div style="margin-top:16px;text-align:right;">'+
       '<button id="wpxSetSave" style="height:30px;padding:0 18px;background:#2f7cf6;color:#fff;border:0;border-radius:4px;">Guardar</button></div>';
     document.getElementById('wpxSetBody').innerHTML=html;
     document.getElementById('wpxPlay').value=pl.mode||'fill';
     document.getElementById('wpxQuality').value=o.quality||'auto';
     window.getPluginListCallback=function(list){
       var box=document.getElementById('wpxPlugList'); if(!box)return;
       box.innerHTML=(list||[]).map(function(p){
         return '<label style="display:block;margin:4px 0;"><input type="checkbox" data-pid="'+p.id+'"'+
           (dis.indexOf(p.id)<0?' checked':'')+'> '+(p.name||p.id)+'</label>';
       }).join('');
     };
     window.browseWallpaperObject.getPluginList();
     document.getElementById('wpxSetSave').onclick=function(){
       var opt={fps:parseInt(document.getElementById('wpxFps').value,10)||30,
         pauseWhenHidden:document.getElementById('wpxP1').checked,
         pauseOnFullscreen:document.getElementById('wpxP2').checked,
         pauseOnMaximized:document.getElementById('wpxP3').checked,
         pauseOnBattery:document.getElementById('wpxP4').checked,
         pauseOnSessionLock:document.getElementById('wpxP5').checked,
         quality:document.getElementById('wpxQuality').value,
         hdr:document.getElementById('wpxHdr').checked};
       var off=[]; var boxes=document.querySelectorAll('#wpxPlugList input[data-pid]');
       for(var i=0;i<boxes.length;i++) if(!boxes[i].checked) off.push(boxes[i].getAttribute('data-pid'));
       var b2=window.browseWallpaperObject;
       b2.saveOptimizations(JSON.stringify(opt));
       b2.saveDisabledPlugins(JSON.stringify(off));
       b2.savePlaybackSettings(JSON.stringify({mode:document.getElementById('wpxPlay').value,
         audio:document.getElementById('wpxAudio').checked}));
       b2.setAutostart(document.getElementById('wpxAuto').checked?'1':'0');
       window.settingsObject.persistSharedSettings(JSON.stringify({language:document.getElementById('wpxLang').value}));
       var m=document.getElementById('wpxModal'); if(m)m.remove();
     };
   };
   b.getLocalSettings();
 }

 // Mini modo: el botón del título pasa por sendToggleMiniMode -> $broadcast
 // -> Ya(), que se queda bloqueado ($a) y no llega a llamar al bridge.
 // Se intercepta el clic y se llama directamente al método nativo, que
 // redimensiona la ventana y aplica la clase CSS.
 (function wpxMiniHook(){
   function bind(){
     try{
       if(!document.querySelectorAll) return;
       // El icono (chevron doble) identifica el botón de mini modo sea
       // cual sea la skin/clase del contenedor.
       var icons=document.querySelectorAll(
         '.fa-chevron-double-right,.fa-chevron-double-left,.miniModeBtn i,.miniModeBtn');
       for(var i=0;i<icons.length;i++){
         var el=icons[i];
         var host=el.closest?el.closest('button,a,[ng-click]'):null;
         if(!host) host=el.parentElement;
         if(!host||host.__wpxMiniBound) continue;
         host.__wpxMiniBound=1;
         host.addEventListener('click',function(ev){
           ev.preventDefault();
           ev.stopImmediatePropagation();
           var b=window.browseWallpaperObject;
           if(!b||!b.toggleMiniMode) return;
           window.__wpxMini=!window.__wpxMini;
           b.toggleMiniMode(!!window.__wpxMini);
         },true);
       }
     }catch(e){}
   }
   bind(); setInterval(bind,900);
 })();

 // "Add wallpaper" / "Remove" de la barra de playlist: el original añade
 // el objeto remoto (file="wpxr:..."), que el motor no sabe abrir. Se usa
 // el fichero local descargado y se guarda en res/config/playlist.json.
 function wpxPlaylistToggle(sel){
   var c=ctrl();
   sel=sel||(c&&c.currentSelection);
   if(!sel) return;
   var p=localPathOf(sel);
   if(!p){ alert('Descarga el wallpaper (OK / doble clic) antes de añadirlo a la playlist.'); return; }
   var b=window.browseWallpaperObject;
   window.getPlaylistCallback=function(pl){
     pl=pl||{}; var items=(pl.items||[]).slice();
     var i=items.indexOf(p);
     if(i>=0) items.splice(i,1); else items.push(p);
     window.browseWallpaperObject.savePlaylist(JSON.stringify({
       intervalSeconds:pl.intervalSeconds||45,
       transitionSeconds:(pl.transitionSeconds===undefined?2:pl.transitionSeconds),
       items:items}));
     // Refrescar el contador/estado de la barra.
     try{
       if(c&&c.selectedMonitor){
         c.selectedMonitor.playlist=c.selectedMonitor.playlist||{items:[]};
         c.selectedMonitor.playlist.items=items.map(function(x){return {wallpaper:{file:x}};});
         c.selectedMonitor.playlistFlags=c.selectedMonitor.playlistFlags||{};
         c.selectedMonitor.playlistFlags[sel.file]=i<0;
         if(c.$apply) c.$apply();
       }
     }catch(e){}
     if(window.__wpxRefresh) window.__wpxRefresh();
   };
   b.getPlaylist();
 }

 function baseName(p){var i=Math.max(p.lastIndexOf('\\'),p.lastIndexOf('/'));return i>=0?p.substr(i+1):p;}
 function openPlaylist(){
   modal('Editor de playlist','<div id="wpxPlBody">Cargando…</div>',true);
   var b=window.browseWallpaperObject;
   window.getPlaylistCallback=function(pl){
     var pbody=document.getElementById('wpxPlBody'); if(!pbody) return;
     pl=pl||{}; var items=pl.items||[];
     var html='<div id="wpxPlItems" style="max-height:210px;overflow:auto;border:1px solid #333;border-radius:4px;padding:6px;"></div>'+
       '<div style="display:flex;gap:8px;margin:10px 0;">'+
       '<button id="wpxPlAdd" style="height:28px;padding:0 12px;background:#2f7cf6;color:#fff;border:0;border-radius:4px;">+ Añadir el actual</button>'+
       '<button id="wpxPlStop" style="height:28px;padding:0 12px;background:#5a5a60;color:#fff;border:0;border-radius:4px;">Detener playlist</button></div>'+
       fieldRow('Intervalo (s)','<input id="wpxPlInt" type="number" min="5" value="'+(pl.intervalSeconds||45)+'" style="width:70px;background:#242428;color:#eee;">')+
       fieldRow('Transición (s)','<input id="wpxPlTr" type="number" min="0" value="'+(pl.transitionSeconds||2)+'" style="width:70px;background:#242428;color:#eee;">')+
       '<div style="margin-top:14px;text-align:right;">'+
       '<button id="wpxPlSave" style="height:30px;padding:0 14px;background:#2f7cf6;color:#fff;border:0;border-radius:4px;margin-right:8px;">Guardar</button>'+
       '<button id="wpxPlAct" style="height:30px;padding:0 14px;background:#27ae60;color:#fff;border:0;border-radius:4px;">Activar</button></div>';
     document.getElementById('wpxPlBody').innerHTML=html;
     function render(){
       var box=document.getElementById('wpxPlItems'); if(!box)return;
       box.innerHTML=items.length?items.map(function(p,i){
         return '<div style="display:flex;justify-content:space-between;align-items:center;padding:3px 4px;border-bottom:1px solid #2a2a2e;">'+
           '<span style="overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:390px;" title="'+p+'">'+baseName(p)+'</span>'+
           '<span><button data-up="'+i+'" style="background:#3a3a40;color:#ddd;border:0;border-radius:3px;margin-right:4px;">↑</button>'+
           '<button data-down="'+i+'" style="background:#3a3a40;color:#ddd;border:0;border-radius:3px;margin-right:4px;">↓</button>'+
           '<button data-del="'+i+'" style="background:#8e2f2f;color:#fff;border:0;border-radius:3px;">✕</button></span></div>';
       }).join(''):'<div style="color:#888;padding:6px;">(vacía)</div>';
       box.querySelectorAll('button[data-up]').forEach(function(btn){
         btn.onclick=function(){var i=+btn.getAttribute('data-up');if(i>0){var t=items[i-1];items[i-1]=items[i];items[i]=t;render();}};});
       box.querySelectorAll('button[data-down]').forEach(function(btn){
         btn.onclick=function(){var i=+btn.getAttribute('data-down');if(i<items.length-1){var t=items[i+1];items[i+1]=items[i];items[i]=t;render();}};});
       box.querySelectorAll('button[data-del]').forEach(function(btn){
         btn.onclick=function(){var i=+btn.getAttribute('data-del');items.splice(i,1);render();};});
     }
     render();
     document.getElementById('wpxPlAdd').onclick=function(){
       var c=ctrl(); var sel=c&&c.currentSelection; if(!sel)return;
       var p=localPathOf(sel);
       if(!p){ alert('Descarga el wallpaper antes de añadirlo a la playlist.'); return; }
       if(items.indexOf(p)<0) items.push(p);
       try{
         if(c.selectedMonitor&&c.selectedMonitor.playlist){
           var has=c.selectedMonitor.playlist.items.some(function(e){
             return ((e.wallpaper||e).file||'')===sel.file; });
           if(!has) c.selectedMonitor.playlist.items.push({wallpaper:sel});
           applyNow();
         }
       }catch(e){}
       render();
     };
     document.getElementById('wpxPlStop').onclick=function(){
       var c=ctrl(); var sel=c&&c.currentSelection;
       var f=sel?(sel.__dl&&sel.__dl.path||sel.file||''):'';
       window.browseWallpaperObject.activatePlaylist('0', f||'');
       document.getElementById('wpxModal').remove();
     };
     function save(){
       var j={intervalSeconds:parseInt(document.getElementById('wpxPlInt').value,10)||45,
         transitionSeconds:parseInt(document.getElementById('wpxPlTr').value,10)||0,
         items:items};
       window.browseWallpaperObject.savePlaylist(JSON.stringify(j));
       return j;
     }
     document.getElementById('wpxPlSave').onclick=function(){save();};
     document.getElementById('wpxPlAct').onclick=function(){save();
       window.browseWallpaperObject.activatePlaylist('1');
       document.getElementById('wpxModal').remove();};
   };
   b.getPlaylist();
 }

 function openDisplays(){
   modal('Pantallas','<div id="wpxMonBody">Cargando…</div>');
   window.getMonitorsCallback=function(list){
     var body=document.getElementById('wpxMonBody'); if(!body)return;
     body.innerHTML=(list||[]).map(function(m){
       return '<div style="display:flex;justify-content:space-between;padding:8px 6px;border-bottom:1px solid #2a2a2e;">'+
         '<span>Monitor '+(m.index+1)+(m.primary?' (principal)':'')+'</span>'+
         '<span>'+m.width+' x '+m.height+'</span></div>';
     }).join('')+
     '<div style="margin-top:14px;color:#9a9a9a;font-size:12px;">El motor aplica el fondo a todos los monitores (escritorio virtual). La asignación independiente por monitor queda para la siguiente fase.</div>';
   };
   window.browseWallpaperObject.getMonitors();
 }


 // Descubrir (Steam) no está implementado: ocultar su pestaña.
 setInterval(function(){
   var els=document.querySelectorAll('div,span,a,button');
   for(var i=0;i<els.length;i++){
     var e=els[i];
     if(e.children.length===0 && (e.textContent||'').trim()==='Discover'){
       var p=e.closest('div,button,a');
       if(p&&p!==e){ p.style.display='none'; }
       else { e.style.display='none'; }
     }
   }
   var c=ctrl();
   if(c&&c.sourceIsExplore){ c.setListSource('installed'); }
 },900);

 // Interceptar botones por delegación (iconos incluidos).
 document.addEventListener('click', function(e){
   // La interfaz nueva (/ui2/) tiene su propio editor de pantallas: esta
   // interceptación era de la UI clásica y se comía el clic en "Pantallas".
   if (location.pathname.indexOf('/ui2') === 0) return;
   var el=e.target;
   for(var d=0; d<5 && el; d++, el=el.parentElement){
     if(el.id==='wpxModal'||el.id==='wpxModalBody') return;
     var t=(el.textContent||'').trim();
     if(t==='Settings'||t==='Configuración'){
       e.preventDefault(); e.stopPropagation(); openSettings(); return;
     }
     if(t==='Displays'||t==='Pantallas'){
       e.preventDefault(); e.stopPropagation(); openDisplays(); return;
     }
     if(t==='Wallpaper Editor'||t==='Editor de fondos'){
       e.preventDefault(); e.stopPropagation(); openEditor(); return;
     }
   }
 }, true);


 // ---------- 5b) Playlist: usar la barra original de la UI ----------
 function localPathOf(item){
   if(!item) return '';
   var f=item.file||'';
   if(f.indexOf('wpxr:')===0){
     if(item.localpath) return item.localpath;   // ya estaba descargado
     var dl=item.__dl||{};
     return (dl.done&&dl.path)?dl.path:'';
   }
   return f;
 }
 function savePlaylistNow(silent){
   var c=ctrl(); var items=[];
   if(c&&c.selectedMonitor&&c.selectedMonitor.playlist){
     var arr=c.selectedMonitor.playlist.items||[];
     for(var i=0;i<arr.length;i++){
       var w=arr[i].wallpaper||arr[i];
       var p=localPathOf(w);
       if(p) items.push(p);
     }
   }
   var cfg=window.__wpxPlCfg||{intervalSeconds:45,transitionSeconds:2};
   if(!items.length){
     var b=window.browseWallpaperObject;
     window.getPlaylistCallback=function(pl){
       pl=pl||{};
       cfg={intervalSeconds:pl.intervalSeconds||45,transitionSeconds:pl.transitionSeconds||2};
       window.__wpxPlCfg=cfg;
       b.savePlaylist(JSON.stringify({intervalSeconds:cfg.intervalSeconds,
         transitionSeconds:cfg.transitionSeconds,items:items}));
       if(!silent) alert('La playlist está vacía. Añade wallpapers con el botón +.');
     };
     b.getPlaylist();
     return;
   }
   window.browseWallpaperObject.savePlaylist(JSON.stringify({
     intervalSeconds:cfg.intervalSeconds,transitionSeconds:cfg.transitionSeconds,
     items:items}));
 }
 function hookPlaylist(){
   var c=ctrl(); if(!c||c.__wpxPlHook) return;
   c.__wpxPlHook=1;
   c.callbackShowPlaylistSettings=function(){ openPlaylist(); };
   c.callbackShowPlaylists=function(save){ if(save) savePlaylistNow(false); else openPlaylist(); };
   c.callbackToggleAllWallpapersInCurrentMonitorPlaylist=function(sel){
     wpxPlaylistToggle(sel);
   };
   c.callbackClearPlaylist=function(){
     try{
       if(c.selectedMonitor&&c.selectedMonitor.playlist){ c.selectedMonitor.playlist.items=[]; }
       savePlaylistNow(true);
       applyNow();
     }catch(e){}
   };
 }
 setInterval(hookPlaylist,800);


 // ---------- 5c) Accesos propios (la barra nativa requiere Steam) ----------
 function addCurrentToPlaylist(){
   var c=ctrl(); var sel=c&&c.currentSelection; if(!sel){alert('Selecciona un wallpaper primero.');return;}
   var p=localPathOf(sel);
   if(!p){ alert('Descarga el wallpaper antes de añadirlo a la playlist.'); return; }
   try{
     if(!c.selectedMonitor) c.selectedMonitor={location:'0',playlist:{items:[],settings:{}},playlistFlags:{}};
     if(!c.selectedMonitor.playlist) c.selectedMonitor.playlist={items:[],settings:{}};
     var has=c.selectedMonitor.playlist.items.some(function(e){return ((e.wallpaper||e).file||'')===sel.file;});
     if(!has) c.selectedMonitor.playlist.items.push({wallpaper:sel});
     applyNow();
     savePlaylistNow(true);
   }catch(e){}
 }
 function buildToolbar(){}



 // logDebugCallback: el host nativo responde a logDebug() llamando aquí.
 // Sin esta definición, un error de página -> onerror -> logDebug -> callback
 // inexistente -> nuevo error -> bucle infinito que tumba el renderer.
 window.logDebugCallback = window.logDebugCallback || function(){};
 window.__wpxErrLast = 0;
 window.__wpxErrCount = 0;
 // Diagnóstico: por qué el grid queda vacío (sin bucles: máx. 3 por segundo).
 window.addEventListener('error', function(ev){
   try{
     var now=Date.now();
     if(now-window.__wpxErrLast<1000){ if(++window.__wpxErrCount>3) return; }
     else { window.__wpxErrCount=0; }
     window.__wpxErrLast=now;
     if(window.browseWallpaperObject&&window.browseWallpaperObject.logDebug)
       window.browseWallpaperObject.logDebug('JSERR '+ev.message+' @'+(ev.filename||'')+':'+ev.lineno);
   }catch(e){}
 });


 // Sin Steam: el estado del Workshop debe permanecer "ok" y oculto, y el
 // modal "Steam Services Unavailable" no debe aparecer nunca.
 // La UI lo abre una sola vez (bandera hasShownSteamServiceError) cuando falla
 // queryWorkshop; con la bandera ya en true, no llega a mostrarse.
 setInterval(function(){
   try{
     var c=ctrl(); if(!c) return;
     c.hasShownSteamServiceError=true;
     if(c.steamWorkshopStatus){
       c.steamWorkshopStatus={complete:true,hidden:true,error:false};
     }
     // Red de seguridad: si aun así apareciera, se cierra solo.
     var els=document.querySelectorAll('div,span,p,h1,h2,h3,h4,h5');
     for(var i=0;i<els.length;i++){
       var e=els[i];
       if(e.children.length!==0) continue;
       var t=(e.textContent||'');
       if(t.indexOf('Steam Services Unavailable')<0 &&
          t.indexOf('Servicios de Steam')<0 &&
          t.indexOf('Steam Workshop servers are')<0) continue;
       var dlg=e;
       while(dlg.parentElement && !dlg.classList.contains('modal') &&
             dlg.parentElement!==document.body){ dlg=dlg.parentElement; }
       if(dlg===document.body) continue;
       var ok=dlg.querySelector('button');
       if(ok) ok.click();
       var bd=document.querySelector('.modal-backdrop');
       if(bd && bd.parentNode) bd.parentNode.removeChild(bd);
       if(window.__wpxRefresh) window.__wpxRefresh();
     }
   }catch(e){}
 },700);


 // ---------- 5d) Editor básico (Fase 5, Tier 1 mínimo) ----------
 function openEditor(){
   var c=ctrl(); var sel=c&&c.currentSelection;
   var p=sel?localPathOf(sel):'';
   var info=sel?('<div>Título: <b>'+(sel.title||'—')+'</b></div>'+
     '<div>Archivo: <b style="word-break:break-all;">'+(p||sel.file||'—')+'</b></div>'+
     '<div>Plugin: <b>'+(sel.provider||'local')+'</b></div>'):'<div>Selecciona un wallpaper.</div>';
   modal('Editor de wallpaper', info+
     '<div style="margin-top:14px;color:#9a9a9a;font-size:12px;">'+
     'Editor Tier 1: ajuste de reproducción, audio, apertura de carpeta y regeneración de miniatura. '+
     'El editor de escenas (Tier 2) no está incluido.</div>'+
     fieldRow('Ajuste','<select id="wpxEdMode" style="background:#242428;color:#eee;">'+
       '<option value="fill">Rellenar</option><option value="fit">Ajustar</option>'+
       '<option value="stretch">Estirar</option></select>')+
     fieldRow('Audio',checkbox('wpxEdAudio',!!(window.__wpxAudio)))+
     '<div style="display:flex;gap:8px;margin-top:16px;">'+
     '<button id="wpxEdApply" style="height:30px;padding:0 14px;background:#2f7cf6;color:#fff;border:0;border-radius:4px;">Guardar ajustes</button>'+
     '<button id="wpxEdOpen" style="height:30px;padding:0 14px;background:#5a5a60;color:#fff;border:0;border-radius:4px;">Abrir carpeta</button>'+
     '<button id="wpxEdDel" style="height:30px;padding:0 14px;background:#8e2f2f;color:#fff;border:0;border-radius:4px;">Borrar descarga</button>'+
     '</div>', true);
   var m=document.getElementById('wpxEdMode'); if(m) m.value=window.__wpxPlaybackMode||'fill';
   var ap=document.getElementById('wpxEdApply');
   if(ap) ap.onclick=function(){
     var mode=document.getElementById('wpxEdMode').value;
     var audio=document.getElementById('wpxEdAudio').checked;
     window.__wpxPlaybackMode=mode; window.__wpxAudio=audio;
     window.browseWallpaperObject.savePlaybackSettings(JSON.stringify({mode:mode,audio:audio}));
     var mm=document.getElementById('wpxModal'); if(mm) mm.remove();
   };
   var op=document.getElementById('wpxEdOpen');
   if(op) op.onclick=function(){
     var dir=p? p.substring(0,p.lastIndexOf('\\')) : '';
     if(dir) window.browseWallpaperObject.openDirectory(dir);
   };
   var del=document.getElementById('wpxEdDel');
   if(del) del.onclick=function(){
     var b=window.browseWallpaperObject; if(b.removeWallpaper&&sel) b.removeWallpaper({location:'',playlist:false,file:sel.file});
     var mm=document.getElementById('wpxModal'); if(mm) mm.remove();
   };
 }

 // ---------- 6) Mini mode: CSS compacto ----------
 var st=document.createElement('style');
 st.textContent='.browseWallpaperMiniMode .browseTopBar,.browseWallpaperMiniMode .titlebar,'+
   '.browseWallpaperMiniMode .browseFilterBar,.browseWallpaperMiniMode .browseWallpaperDetailView,'+
   '.browseWallpaperMiniMode .playlistBar{display:none!important;}'+
   '.browseWallpaperMiniMode .browseWallpaperList{width:100%!important;}'+
   '#wpxModal input[type=checkbox]{accent-color:#2f7cf6;}';
 document.head.appendChild(st);
})();
)JS", fr->GetURL(), 0);
    }
    // QA opcional: WALLPX_AUTOTEST=prov1,prov2 inicia descargas de prueba.
    if (const char* at = getenv("WALLPX_AUTOTEST")) {
        if (auto fr = browser ? browser->GetMainFrame() : nullptr) {
            std::string prov = at;
            std::string js = R"JS(
(function(){
 var P='__PROV__';
 setTimeout(function(){try{var c=window.browseWallpapersCtrl;
   if(c&&c.setListSource)c.setListSource('workshop');}catch(e){}},5000);
 function startOne(prov){
   try{var c=window.browseWallpapersCtrl;
     var L=c.queryWallpapers||[],it=null;
     for(var i=0;i<L.length;i++){if(prov&&L[i].provider===prov){it=L[i];break;}}
     if(!it&&L.length)it=L[0];
     if(!it)return;
     c.currentSelection=it; if(c.$apply)c.$apply();
     c.callbackOk(true);
   }catch(e){}
 }
 var provs=P.split(',').filter(function(x){return !!x;});
 provs.forEach(function(pr,ix){ setTimeout(function(){startOne(pr);},11000+ix*3000); });
 if(!provs.length) setTimeout(function(){startOne('');},11000);
})();
)JS";
            size_t k = js.find("__PROV__");
            if (k != std::string::npos) js.replace(k, 8, prov);
            fr->ExecuteJavaScript(js, fr->GetURL(), 0);
        }
    }
    // FX de vídeo en la UI (equivalente al original):
    //   - miniaturas animadas: los tiles de vídeo reproducen el archivo
    //   - preview animada: el panel derecho reproduce el vídeo seleccionado
    // Script persistente (intervalo) para no depender del momento del digest.
    if (auto fr = browser ? browser->GetMainFrame() : nullptr) {
        // Sin Steam: forzar el apagado del overlay "Connecting to Steam"
        // de forma continua (el bundle solo lo enciende; lo apagaba el host
        // nativo original vía onOSProviderConnectionChanged).
        fr->ExecuteJavaScript(
            "setInterval(function(){try{"
            "var el=document.querySelector('[ng-app]')||document.body;"
            "var inj=window.angular&&angular.element(el).injector();if(!inj)return;"
            "var root=inj.get('$rootScope'),st=[root],ch=false;"
            "while(st.length){var s=st.pop();"
            "if(s&&s.hasOwnProperty&&s.hasOwnProperty('osProviderIsConnecting')&&s.osProviderIsConnecting){"
            "s.osProviderIsConnecting=false;ch=true;}"
            "if(s&&s.$$childHead){var c=s.$$childHead;while(c){st.push(c);c=c.$$nextSibling;}}}"
            "if(ch)root.$apply();}catch(e){}},800);",
            fr->GetURL(), 0);
        {
            std::string mapJson = BuildPreviewFileMap(ExeDir());
            std::string mapJs = "window.__wpxMap=" + mapJson + ";";
            fr->ExecuteJavaScript(mapJs, fr->GetURL(), 0);
        }
    }
    // Descubrimiento de plugins (catálogo externo) en segundo plano.
    {
        CefRefPtr<CefBrowser> b = browser;
        std::thread([b]() {
            std::wstring dir = ExeDir();
            auto plugins = DiscoverPlugins(dir);
            SetKnownPlugins(plugins);
            // Cuantos items pedir por plugin (paginacion del contrato v1:
            // 100 por pagina). Configurable en res/config/plugins.json.
            int maxPerPlugin = 600;
            {
                FILE* pf = nullptr;
                fopen_s(&pf, "res\\config\\plugins.json", "rb");
                if (pf) {
                    char buf[4096] = {};
                    size_t got = fread(buf, 1, sizeof(buf) - 1, pf);
                    fclose(pf);
                    if (got > 0) {
                        const char* k = strstr(buf, "\"maxItemsPerPlugin\"");
                        if (k) {
                            const char* c = strchr(k, ':');
                            if (c) {
                                int v = atoi(c + 1);
                                if (v >= 100 && v <= 5000) maxPerPlugin = v;
                            }
                        }
                        const char* t = strstr(buf, "\"cacheTtlHours\"");
                        if (t) {
                            const char* c = strchr(t, ':');
                            if (c) {
                                int v = atoi(c + 1);
                                if (v >= 0 && v <= 24 * 30)
                                    SetPluginCacheTtlHours(v);
                            }
                        }
                    }
                }
            }
            SetPluginProgress(true, 0, (int)plugins.size(), 0);
            std::atomic<int> doneCount{0};
            // Un hilo por plugin; cada página se publica al instante.
            std::vector<std::thread> workers;
            for (const auto& p : plugins) {
                workers.emplace_back([p, &doneCount, &plugins, maxPerPlugin]() {
                    ListPluginItemsPaged(
                        p, maxPerPlugin,
                        [](const std::vector<RemoteItem>& page) {
                            MergeRemoteItems(page);
                        });
                    int d = ++doneCount;
                    SetPluginProgress(true, d, (int)plugins.size(),
                                      (int)GetRemoteRegistryCopy().size());
                });
            }
            for (auto& t : workers) t.join();
            {
                // Quitar del catálogo los plugins deshabilitados/eliminados.
                std::vector<std::string> ids;
                for (const auto& p : plugins) ids.push_back(p.id);
                // "uploads" no es un plugin: son los videos que los clientes
                // han subido y decidido conservar.
                ids.push_back("uploads");
                // "optimized": escritorios compuestos y exportaciones para el
                // móvil. No vienen de un plugin; si se podan, los fondos
                // aplicados y las asignaciones por monitor quedan sin medio.
                ids.push_back("optimized");
                PruneProviders(ids);
            }
            std::vector<RemoteItem> all = GetRemoteRegistryCopy();
            // Snapshot para el siguiente arranque (respuesta instantánea).
            SaveCatalogSnapshot(dir, all);
            {
                char b[160];
                sprintf_s(b, "plugins: %d encontrados, %d items remotos",
                          (int)plugins.size(), (int)all.size());
                FILE* f = nullptr;
                fopen_s(&f, "res\\dnxwallpaper.log", "a");
                if (f) { fprintf(f, "%s\n", b); fclose(f); }
            }
            // Miniaturas locales en segundo plano (estáticas + animadas),
            // para que el grid no dependa de la red al navegar.
            SetPluginProgress(false, (int)plugins.size(), (int)plugins.size(),
                              (int)all.size());
            WarmRemoteThumbs(dir, 240, 120);
            // El push se hace desde JS vía browseWallpaperObject.getRemoteItems
            // (evita cruzar procesos browser/render).
        }).detach();
    }
    // window.global (inyectado por el host nativo en el original).
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    BindGlobalObject(context, browser, dir + L"\\locales", language_);
}


bool WallpaperCefClient::OnKeyEvent(CefRefPtr<CefBrowser> browser,
                                    const CefKeyEvent& event,
                                    CefEventHandle os_event) {
    (void)os_event;
    if (event.type != KEYEVENT_RAWKEYDOWN) return false;
    const int key = event.windows_key_code;
    const bool f5 = (key == VK_F5);
    const bool ctrlR = (key == 'R') &&
                       (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
    if ((f5 || ctrlR) && browser) {
        browser->Reload();
        return true;
    }
    return false;
}

void WallpaperCefClient::OnRenderProcessTerminated(    CefRefPtr<CefBrowser> browser, CefRequestHandler::TerminationStatus status) {
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) {
        fprintf(f, "cef: render process terminated (%d), reloading\n",
                (int)status);
        fclose(f);
    }
    if (browser) {
        browser->Reload();
    }
}

bool WallpaperCefClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
    if (message->GetName().ToString() == "wpx-mini") {
        // El proceso renderer no puede redimensionar la ventana del browser:
        // se pide aquí, donde GetWindowHandle() sí es válido.
        const bool on = message->GetArgumentList()->GetBool(0);
        HWND h = browser && browser->GetHost()
                     ? browser->GetHost()->GetWindowHandle()
                     : nullptr;
        if (h) h = GetAncestor(h, GA_ROOT);
        static RECT normal{0, 0, 1440, 860};
        static bool miniOn = false;
        if (h) {
            RECT r{};
            GetWindowRect(h, &r);
            if (on == miniOn) {
                // Ya está en ese estado: no tocar (evita guardar 560x780
                // como tamaño "normal" si llega el aviso dos veces).
            } else if (on) {
                normal = r;
                miniOn = true;
                SetWindowPos(h, nullptr, r.left, r.top, 420, 820,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            } else {
                miniOn = false;
                int w = normal.right - normal.left;
                int hh = normal.bottom - normal.top;
                if (w <= 620 || hh <= 800) {
                    // Nunca restaurar al tamaño mini (pasó si el aviso llegó
                    // dos veces): se usa el tamaño por defecto.
                    w = 1440;
                    hh = 830;
                }
                SetWindowPos(h, nullptr, normal.left, normal.top, w, hh,
                             SWP_NOZORDER | SWP_NOACTIVATE |
                                 SWP_FRAMECHANGED);
            }
        }
        return true;
    }
    if (message->GetName().ToString() == "wpx-provider-connected") {
        // El render avisa (vía jsInit) de que el provider conectó. Tras 3s
        // (UI ya arrancada) se apaga el overlay "Connecting to Steam..."
        // en toda la jerarquía de scopes AngularJS.
    // El render avisa (vía jsInit) de que el provider conectó. Aquí, en el
    // proceso browser: 1) apaga el overlay "Connecting to Steam..." en
    // toda la jerarquía de scopes; 2) empuja el inventario local a la
    // galería (el browse NO pide la lista: espera
    // window.browseWallpapersCtrl.updateWallpapers(lista, [])).
    class ProviderReadyTask : public CefTask {
    public:
        explicit ProviderReadyTask(CefRefPtr<CefBrowser> b) : browser_(b) {}
        void Execute() override {
            auto frame = browser_ ? browser_->GetMainFrame() : nullptr;
            if (!frame) {
                return;
            }
            frame->ExecuteJavaScript(
                "(function(){try{var el=document.querySelector("
                "'[ng-app]')||document.body;var inj=angular."
                "element(el).injector();if(!inj)return;var "
                "root=inj.get('$rootScope');var stack=[root];"
                "while(stack.length){var s=stack.pop();"
                "if(s&&s.hasOwnProperty&&s.hasOwnProperty("
                "'osProviderIsConnecting')){s."
                "osProviderIsConnecting=false;}"
                "if(s&&s.$$childHead){var c=s.$$childHead;"
                "while(c){stack.push(c);c=c.$$nextSibling;}}}"
                "root.$apply();}catch(e){}})()",
                frame->GetURL(), 0);
            SyncVideoSources(ExeDir());
            {
                std::string local = BuildProjectsJson(ExeDir());
                std::string esc;
                for (char c : local) {
                    if (c == '\'') esc += "\\'";
                    else if (c == '\\') esc += "\\\\";
                    else esc += c;
                }
                frame->ExecuteJavaScript(
                    "(function(list){var n=0;(function go(){try{"
                    "var c=window.browseWallpapersCtrl;"
                    "if(!c||!c.updateWallpapers){if(++n<60)return setTimeout(go,250);return;}"
                    "window.__wpxLocal=list;c.wallpapers=[];c.currentSelection=undefined;"
                    "c.updateWallpapers(list,[]);}catch(e){if(++n<60)return setTimeout(go,250);}})();})"
                    "(JSON.parse('" + esc + "'))",
                    frame->GetURL(), 0);
            }
        }
        IMPLEMENT_REFCOUNTING(ProviderReadyTask);
    private:
        CefRefPtr<CefBrowser> browser_;
    };
    CefPostDelayedTask(TID_UI, new ProviderReadyTask(browser), 3000);
    return true;
    }
    (void)frame;
    (void)source_process;
    return false;
}

void WallpaperCefApp::OnRegisterCustomPreferences(
    cef_preferences_type_t type, CefRawPtr<CefPreferenceRegistrar> registrar) {
    (void)type;
    (void)registrar;
}

bool WallpaperCefApp::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                       cef_log_severity_t level,
                                       const CefString& message,
                                       const CefString& source, int line) {
    (void)browser;
    (void)level;
    // Vuelca la consola JS de la UI a js-console.log junto al exe.
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    FILE* f = nullptr;
    _wfopen_s(&f, (dir + L"\\js-console.log").c_str(), L"a, ccs=UTF-8");
    if (f) {
        fwprintf(f, L"[%s:%d] %s\n", source.ToWString().c_str(), line,
                 message.ToWString().c_str());
        fclose(f);
    }
    return false;
}

// Navega la ventana principal a la interfaz nueva (servidor LAN) desde el
// hilo de UI. Llamar a LoadURL desde OnContextCreated (renderer) mataba el
// proceso de renderizado en bucle.
class NavigateTask : public CefTask {
  public:
    NavigateTask(CefRefPtr<CefBrowser> browser, const std::string& url)
        : browser_(browser), url_(url) {}
    void Execute() override {
        if (browser_ && browser_->GetMainFrame()) {
            browser_->GetMainFrame()->LoadURL(url_);
        }
    }
    IMPLEMENT_REFCOUNTING(NavigateTask);

  private:
    CefRefPtr<CefBrowser> browser_;
    std::string url_;
};

void WallpaperCefApp::OnAfterCreated(CefRefPtr<CefBrowser> browser) {    // La ventana la crea CEF y usa el icono de libcef (página morada). Se
    // fuerza el icono del producto (rc id 1 = assets/icons/ui.ico).
    HWND h = (browser && browser->GetHost())
                 ? browser->GetHost()->GetWindowHandle()
                 : nullptr;
    ApplyWindowIcon(h);
    if (!h) {
        // La ventana puede no estar lista aún: reintento en segundo plano.
        std::thread([] {
            for (int i = 0; i < 10; ++i) {
                Sleep(300);
                HWND found = nullptr;
                EnumWindows(FindAppWindow, (LPARAM)&found);
                if (found) {
                    ApplyWindowIcon(found);
                    return;
                }
            }
        }).detach();
    }

    // La interfaz nueva se carga como URL inicial (OnContextInitialized), así
    // que aquí no hay que navegar: hacerlo destruía el renderer que servía el
    // catálogo y la ventana se quedaba sin JS.
}

void WallpaperCefApp::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    // Al cerrar la última ventana se termina el proceso (libera el mutex de
    // instancia única). Sin esto quedaban UIs "fantasma" que impedían abrir
    // la aplicación de nuevo.
    (void)browser;
    CefQuitMessageLoop();
}

bool WallpaperCefApp::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& target_url,
                                    const CefString& target_frame_name,
                                    WindowOpenDisposition target_disposition,
                                    bool user_gesture,
                                    const CefPopupFeatures& popupFeatures,
                                    CefWindowInfo& windowInfo,
                                    CefRefPtr<CefClient>& client,
                                    CefBrowserSettings& settings,
                                    CefRefPtr<CefDictionaryValue>& extra_info,
                                    bool* no_javascript_access) {
    (void)browser;
    (void)frame;
    (void)target_disposition;
    (void)user_gesture;
    (void)popupFeatures;
    (void)target_frame_name;
    (void)windowInfo;
    (void)client;
    (void)settings;
    (void)extra_info;
    (void)no_javascript_access;
    // Sin popups: la UI original no los usa.
    (void)target_url;
    return true;
}

// Fusiona dos arrays JSON ("[...]" + "[...]") en uno.
static std::string MergeJsonArrays(const std::string& a, const std::string& b) {
    auto inner = [](const std::string& s) {
        if (s.size() >= 2) return s.substr(1, s.size() - 2);
        return std::string();
    };
    std::string ia = inner(a), ib = inner(b);
    if (ia.empty()) return "[" + ib + "]";
    if (ib.empty()) return "[" + ia + "]";
    return "[" + ia + "," + ib + "]";
}

// Puja (push) del catálogo combinado a la galería.
static void PushCatalogToUI(CefRefPtr<CefBrowser> browser) {
    if (!browser) return;
    auto frame = browser->GetMainFrame();
    if (!frame) return;
    std::wstring dir = ExeDir();
    std::string remote =
        BuildRemoteItemsJson(GetRemoteRegistryCopy());
    std::string json = MergeJsonArrays(BuildProjectsJson(dir), remote);
    auto escJs = [](const std::string& in) {
        std::string out;
        for (char c : in) {
            if (c == '\'') out += "\\'";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    };
    std::string esc = escJs(json);
    std::string escRemote = escJs(remote);
    frame->ExecuteJavaScript(
        "(function(list,remote){window.__wpxRemote=remote;var n=0;(function go(){"
        "try{var c=window.browseWallpapersCtrl;"
        "if(!c||!c.updateWallpapers){if(++n<60)return setTimeout(go,250);return;}"
        "c.updateWallpapers(list,[]);console.log('[cat] pushed',list.length,"
        "'remote',remote.length);}"
        "catch(e){if(n===0)console.log('[cat] err',e.message);if(++n<60)return setTimeout(go,250);}})();})"
        "(JSON.parse('" + esc + "'),JSON.parse('" + escRemote + "'))",
        frame->GetURL(), 0);
}

// Inventario local: ver projects_inventory.{h,cpp}.
MethodDispatcher* WallpaperUiDispatcher() {
    static MethodDispatcher d;
    static bool registered = false;
    if (!registered) {
        registered = true;
        // Stubs válidos para todo el inventario del bridge: la UI original
        // invoca muchos métodos que no implementamos aún; sin ellos, una
        // llamada desconocida lanza excepción JS y rompe su flujo.
        auto stub = [](const std::vector<std::string>&) -> MethodResult {
            return {true, "null", ""};
        };
        for (const auto& m : kUiMethods) d.Register("ui", m, stub);
        for (const auto& m : kBrowseMethods)
            d.Register("browseWallpaperObject", m, stub);
        for (const auto& m : kSettingsMethods)
            d.Register("settingsObject", m, stub);
        for (const auto& m : kWelcomeMethods)
            d.Register("welcomeObject", m, stub);
        // --- Ajustes persistentes (settingsObject) ---
        d.Register("settingsObject", "persistSharedSettings",
            [](const std::vector<std::string>& a) -> MethodResult {
                wchar_t exe[MAX_PATH];
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                std::wstring dir = exe;
                dir = dir.substr(0, dir.find_last_of(L"\\/"));
                FILE* f = nullptr;
                _wfopen_s(&f, (dir + L"\\res\\config\\settings.json").c_str(),
                           L"w, ccs=UTF-8");
                if (!f) return {false, "", "cannot write settings"};
                std::string json = a.empty() ? "{}" : a[0];
                fwrite(json.data(), 1, json.size(), f);
                fclose(f);
                return {true, "true", ""};
            });
        d.Register("settingsObject", "toggleAutoStartRegistry",
            [](const std::vector<std::string>& a) -> MethodResult {
                bool enable = !a.empty() && (a[0] == "true" || a[0] == "1");
                wchar_t exe[MAX_PATH];
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                std::wstring dir = exe;
                dir = dir.substr(0, dir.find_last_of(L"\\/"));
                std::wstring cmd = L"\"" + dir + L"\\DNXWallpaper.exe\"";
                HKEY k = nullptr;
                if (RegOpenKeyExW(HKEY_CURRENT_USER,
                                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) {
                    return {false, "", "registry open failed"};
                }
                LONG rc;
                if (enable) {
                    rc = RegSetValueExW(k, L"DNXWallpaperV3", 0, REG_SZ,
                                        (const BYTE*)cmd.c_str(),
                                        (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
                } else {
                    rc = RegDeleteValueW(k, L"DNXWallpaperV3");
                    if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
                }
                RegCloseKey(k);
                return {rc == ERROR_SUCCESS, rc == ERROR_SUCCESS ? "true" : "false", ""};
            });
        d.Register("settingsObject", "resetConfig",
            [](const std::vector<std::string>&) -> MethodResult {
                wchar_t exe[MAX_PATH];
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                std::wstring dir = exe;
                dir = dir.substr(0, dir.find_last_of(L"\\/"));
                DeleteFileW((dir + L"\\res\\config\\settings.json").c_str());
                return {true, "true", ""};
            });
        d.Register("settingsObject", "getWallpaperCache",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "[]", ""};
            });
        d.Register("settingsObject", "getRunningApplications",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "[]", ""};
            });
        d.Register("settingsObject", "getApplicationsPlayingAudio",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "[]", ""};
            });
        d.Register("settingsObject", "getCurrentMediaSession",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "null", ""};
            });
        // --- Portapapeles (ui.pasteFromClipboard) ---
        d.Register("ui", "pasteFromClipboard",
            [](const std::vector<std::string>&) -> MethodResult {
                std::string text;
                if (OpenClipboard(nullptr)) {
                    HANDLE h = GetClipboardData(CF_UNICODETEXT);
                    if (h) {
                        auto* w = (const wchar_t*)GlobalLock(h);
                        if (w) {
                            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1,
                                                        nullptr, 0, nullptr,
                                                        nullptr);
                            text.resize((size_t)(n > 0 ? n - 1 : 0));
                            if (n > 0) {
                                WideCharToMultiByte(CP_UTF8, 0, w, -1,
                                                    text.data(), n, nullptr,
                                                    nullptr);
                            }
                            GlobalUnlock(h);
                        }
                    }
                    CloseClipboard();
                }
                std::string esc;
                for (char c : text) {
                    if (c == '"' || c == '\\') esc.push_back('\\');
                    if (c == '\n') { esc += "\\n"; continue; }
                    esc.push_back(c);
                }
                return {true, "\"" + esc + "\"", ""};
            });
        // Estado de descarga (barra de progreso en la ficha).
        d.Register("browseWallpaperObject", "getDownloadStatus",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, GetDownloadStatusJson(), ""};
            });
        // Sólo locales (pestaña Instalados).
        d.Register("browseWallpaperObject", "getLocalItems",
            [](const std::vector<std::string>&) -> MethodResult {
                SyncVideoSources(ExeDir());
                std::string j = BuildProjectsJson(ExeDir());
                FILE* f = nullptr;
                fopen_s(&f, "res\\dnxwallpaper.log", "a");
                if (f) {
                    fprintf(f, "bridge: getLocalItems (%d bytes)\n", (int)j.size());
                    fclose(f);
                }
                return {true, j, ""};
            });
        // Selector de plugins.
        d.Register("browseWallpaperObject", "getPluginList",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, BuildPluginListJson(), ""};
            });
        // Carga/paginación: args[0] = "plugin|page|query"
        d.Register("browseWallpaperObject", "loadPluginPage",
            [](const std::vector<std::string>& a) -> MethodResult {
                std::string spec = a.empty() ? "" : a[0];
                size_t p1 = spec.find('|');
                std::string plugin = p1 == std::string::npos ? spec : spec.substr(0, p1);
                int page = 1;
                std::string query;
                if (p1 != std::string::npos) {
                    size_t p2 = spec.find('|', p1 + 1);
                    std::string pg = spec.substr(p1 + 1, p2 == std::string::npos
                                                           ? std::string::npos
                                                           : p2 - p1 - 1);
                    page = atoi(pg.c_str());
                    if (page < 1) page = 1;
                    if (p2 != std::string::npos) query = spec.substr(p2 + 1);
                }
                return {true, LoadPluginPageJson(ExeDir(), plugin, page, query, 60), ""};
            });
        d.Register("browseWallpaperObject", "getLanInfo",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, LanInfoJson(), ""};
            });
        d.Register("browseWallpaperObject", "ensureThumb",
            [](const std::vector<std::string>& a) -> MethodResult {
                if (!a.empty() && !a[0].empty()) {
                    EnsureRemoteThumbAsync(ExeDir(), a[0]);
                }
                return {true, "true", ""};
            });
        // Ítems de plugins (mismo proceso que el escaneo): la UI los pide
        // periódicamente para poblar la pestaña "Plugins".
        d.Register("browseWallpaperObject", "getRemoteItems",
            [](const std::vector<std::string>&) -> MethodResult {
                std::vector<RemoteItem> items = GetRemoteRegistryCopy();
                std::string j = BuildRemoteItemsJson(items);
                FILE* f = nullptr;
                fopen_s(&f, "res\\dnxwallpaper.log", "a");
                if (f) {
                    fprintf(f, "bridge: getRemoteItems -> %d items (%d bytes)\n",
                            (int)items.size(), (int)j.size());
                    fclose(f);
                }
                return {true, j, ""};
            });
        // Versión del catálogo (barata): la UI sólo re-pide el JSON cuando
        // cambia (ítems nuevos o miniaturas recién cacheadas).
        d.Register("browseWallpaperObject", "logDebug",
            [](const std::vector<std::string>& a) -> MethodResult {
                FILE* f = nullptr;
                fopen_s(&f, "res\\ui-debug.log", "a");
                if (f) {
                    fprintf(f, "%s\n", a.empty() ? "" : a[0].c_str());
                    fclose(f);
                }
                return {true, "true", ""};
            });
        d.Register("browseWallpaperObject", "getPluginStatus",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, GetPluginStatusJson(), ""};
            });
        d.Register("browseWallpaperObject", "getRemoteVersion",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, std::to_string(GetRemoteVersion()), ""};
            });
        // OK / doble clic sobre un ítem remoto: subscribe = descargar y aplicar.
        d.Register("ui", "openDeviceEditor",
            [](const std::vector<std::string>&) -> MethodResult {
                LaunchDeviceEditor();
                return {true, "{}", ""};
            });
        d.Register("ui", "openMobilePanel",
            [](const std::vector<std::string>&) -> MethodResult {
                LaunchDeviceEditor();
                return {true, "{}", ""};
            });
        d.Register("ui", "subscribeWorkshopItem",
            [](const std::vector<std::string>& a) -> MethodResult {
                std::string raw = a.empty() ? "" : a[0];
                std::vector<std::string> keys;
                size_t p = 0;
                while ((p = raw.find('"', p)) != std::string::npos) {
                    size_t e = raw.find('"', p + 1);
                    if (e == std::string::npos) break;
                    keys.push_back(raw.substr(p + 1, e - p - 1));
                    p = e + 1;
                }
                if (keys.empty() && !raw.empty()) {
                    size_t start = 0;
                    while (start <= raw.size()) {
                        size_t comma = raw.find(',', start);
                        std::string part = raw.substr(
                            start, comma == std::string::npos
                                       ? std::string::npos
                                       : comma - start);
                        if (!part.empty() && part.front() != '[' &&
                            part.front() != '{') {
                            keys.push_back(part);
                        }
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }
                }
                int started = 0;
                for (const auto& k : keys) {
                    const RemoteItem* ri = FindRemoteItem(k);
                    if (!ri) continue;
                    RemoteItem copy = *ri;
                    if (StartRemoteDownload(ExeDir(), copy)) ++started;
                }
                return {true, "{\"started\":" + std::to_string(started) + "}",
                        ""};
            });
        // ---------------- Fase 1/2: playlist, settings, monitores ----------------
        auto readFileUtf8 = [](const std::wstring& path) {
            std::string out;
            FILE* f = nullptr;
            _wfopen_s(&f, path.c_str(), L"rb");
            if (!f) return out;
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            out.assign((size_t)(n > 0 ? n : 0), '\0');
            if (n > 0) fread(out.data(), 1, (size_t)n, f);
            fclose(f);
            return out;
        };
        auto writeFileUtf8 = [](const std::wstring& path, const std::string& data) {
            FILE* f = nullptr;
            _wfopen_s(&f, path.c_str(), L"wb");
            if (!f) return false;
            if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
            fclose(f);
            return true;
        };
        auto jsonEsc = [](const std::string& in) {
            std::string o;
            for (char c : in) {
                if (c == '\\' || c == '"') o.push_back('\\');
                if (c == '\n') { o += "\\n"; continue; }
                o.push_back(c);
            }
            return o;
        };
        // Playlist actual (o plantilla por defecto).
        d.Register("browseWallpaperObject", "getPlaylist",
            [readFileUtf8](const std::vector<std::string>&) -> MethodResult {
                std::string j = readFileUtf8(ExeDir() + L"\\res\\config\\playlist.json");
                if (j.empty()) {
                    j = "{\"intervalSeconds\":45,\"transitionSeconds\":2,\"items\":[]}";
                }
                return {true, j, ""};
            });
        d.Register("browseWallpaperObject", "savePlaylist",
            [writeFileUtf8](const std::vector<std::string>& a) -> MethodResult {
                std::string j = a.empty() ? "" : a[0];
                if (j.empty()) return {false, "", "empty"};
                return {writeFileUtf8(ExeDir() + L"\\res\\config\\playlist.json", j)
                            ? true : false, "true", ""};
            });
        // activatePlaylist("1", fileOpcional) / ("0", archivoActual)
        d.Register("browseWallpaperObject", "activatePlaylist",
            [readFileUtf8, writeFileUtf8, jsonEsc](
                const std::vector<std::string>& a) -> MethodResult {
                bool on = !a.empty() && (a[0] == "1" || a[0] == "true");
                std::wstring cur = ExeDir() + L"\\res\\current.json";
                if (on) {
                    std::string pj = readFileUtf8(ExeDir() + L"\\res\\config\\playlist.json");
                    size_t i = pj.find("\"items\"");
                    size_t q = i == std::string::npos ? i : pj.find('"', pj.find('[', i));
                    size_t e = q == std::string::npos ? q : pj.find('"', q + 1);
                    if (q == std::string::npos || e == std::string::npos) {
                        return {false, "", "playlist vacia"};
                    }
                    std::string first = pj.substr(q + 1, e - q - 1);
                    std::string body = "{\"project\":\"" + first +
                                       "\",\"playlist\":true}";
                    writeFileUtf8(cur, body);
                } else {
                    std::string f = a.size() > 1 ? a[1] : "";
                    if (f.empty()) return {false, "", "sin archivo"};
                    writeFileUtf8(cur, "{\"project\":\"" + jsonEsc(f) + "\"}");
                }
                HANDLE ev = CreateEventW(nullptr, FALSE, FALSE,
                                         L"dnxWallpaperV3Reload");
                if (ev) { SetEvent(ev); CloseHandle(ev); }
                return {true, "true", ""};
            });
        d.Register("browseWallpaperObject", "savePlaybackMode",
            [writeFileUtf8](const std::vector<std::string>& a) -> MethodResult {
                std::string mode = a.empty() ? "fill" : a[0];
                if (mode != "fill" && mode != "fit" && mode != "stretch") {
                    mode = "fill";
                }
                std::string body = "{\"mode\":\"" + mode + "\"}";
                return {writeFileUtf8(ExeDir() + L"\\res\\config\\playback.json", body),
                        "true", ""};
            });
        d.Register("browseWallpaperObject", "savePlaybackSettings",
            [writeFileUtf8](const std::vector<std::string>& a) -> MethodResult {
                std::string j = a.empty() ? "{}" : a[0];
                return {writeFileUtf8(ExeDir() + L"\\res\\config\\playback.json", j),
                        "true", ""};
            });
        // Ajustes locales agregados (motor + UI + plugins).
        d.Register("browseWallpaperObject", "getLocalSettings",
            [readFileUtf8](const std::vector<std::string>&) -> MethodResult {
                std::string opt = readFileUtf8(ExeDir() + L"\\res\\config\\optimizations.json");
                if (opt.empty()) opt = "{}";
                std::string set = readFileUtf8(ExeDir() + L"\\res\\config\\settings.json");
                if (set.empty()) set = "{}";
                std::string pl = readFileUtf8(ExeDir() + L"\\res\\config\\playback.json");
                if (pl.empty()) pl = "{\"mode\":\"fill\"}";
                std::string plug = readFileUtf8(ExeDir() + L"\\res\\config\\plugins.json");
                if (plug.empty()) plug = "{\"disabled\":[]}";
                bool autostart = false;
                HKEY k = nullptr;
                if (RegOpenKeyExW(HKEY_CURRENT_USER,
                                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  0, KEY_READ, &k) == ERROR_SUCCESS) {
                    wchar_t v[512];
                    DWORD sz = sizeof(v);
                    autostart = RegQueryValueExW(k, L"DNXWallpaperV3", nullptr,
                                                 nullptr, (LPBYTE)v, &sz) == ERROR_SUCCESS;
                    RegCloseKey(k);
                }
                std::string j = "{\"autostart\":";
                j += autostart ? "true" : "false";
                j += ",\"optimizations\":" + opt + ",\"settings\":" + set +
                     ",\"playback\":" + pl + ",\"plugins\":" + plug + "}";
                return {true, j, ""};
            });
        d.Register("browseWallpaperObject", "saveOptimizations",
            [writeFileUtf8](const std::vector<std::string>& a) -> MethodResult {
                std::string j = a.empty() ? "{}" : a[0];
                return {writeFileUtf8(ExeDir() + L"\\res\\config\\optimizations.json", j),
                        "true", ""};
            });
        d.Register("browseWallpaperObject", "saveDisabledPlugins",
            [writeFileUtf8, readFileUtf8](
                const std::vector<std::string>& a) -> MethodResult {
                std::string arr = a.empty() ? "[]" : a[0];
                // Conservar maxItemsPerPlugin si ya estaba configurado.
                std::string extra;
                {
                    std::string cur = readFileUtf8(
                        ExeDir() + L"\\res\\config\\plugins.json");
                    size_t k = cur.find("\"maxItemsPerPlugin\"");
                    if (k != std::string::npos) {
                        size_t c = cur.find(':', k);
                        size_t e = cur.find_first_of(",}", c);
                        if (c != std::string::npos && e != std::string::npos) {
                            extra = ",\"maxItemsPerPlugin\":" +
                                    cur.substr(c + 1, e - c - 1);
                        }
                    }
                }
                std::string body = "{\"disabled\":" + arr + extra + "}";
                return {writeFileUtf8(ExeDir() + L"\\res\\config\\plugins.json", body),
                        "true", ""};
            });
        d.Register("browseWallpaperObject", "setAutostart",
            [](const std::vector<std::string>& a) -> MethodResult {
                bool on = !a.empty() && (a[0] == "1" || a[0] == "true");
                HKEY k = nullptr;
                if (RegOpenKeyExW(HKEY_CURRENT_USER,
                                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) {
                    return {false, "", "registry"};
                }
                if (on) {
                    std::wstring exe = ExeDir() + L"\\DNXWallpaper.exe";
                    RegSetValueExW(k, L"DNXWallpaperV3", 0, REG_SZ,
                                   (const BYTE*)exe.c_str(),
                                   (DWORD)((exe.size() + 1) * sizeof(wchar_t)));
                } else {
                    RegDeleteValueW(k, L"DNXWallpaperV3");
                }
                RegCloseKey(k);
                return {true, "true", ""};
            });
        d.Register("browseWallpaperObject", "getMonitors",
            [](const std::vector<std::string>&) -> MethodResult {
                struct Ctx { std::string j; int n = 0; };
                Ctx ctx;
                ctx.j = "[";
                EnumDisplayMonitors(
                    nullptr, nullptr,
                    [](HMONITOR h, HDC, LPRECT, LPARAM l) -> BOOL {
                        auto* c = reinterpret_cast<Ctx*>(l);
                        MONITORINFO mi{};
                        mi.cbSize = sizeof(mi);
                        if (!GetMonitorInfoW(h, &mi)) return TRUE;
                        char buf[220];
                        sprintf_s(buf,
                                  "%s{\"index\":%d,\"x\":%ld,\"y\":%ld,"
                                  "\"width\":%ld,\"height\":%ld,\"primary\":%s}",
                                  c->n ? "," : "", c->n, mi.rcMonitor.left,
                                  mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                                  mi.rcMonitor.bottom - mi.rcMonitor.top,
                                  (mi.dwFlags & MONITORINFOF_PRIMARY) ? "true" : "false");
                        c->j += buf;
                        ++c->n;
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(&ctx));
                ctx.j += "]";
                return {true, ctx.j, ""};
            });
        d.Register("ui", "copyToClipboard",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "true", ""};
            });
        // Stubs con formato válido (la UI los tolera; fase 2: reales).
        d.Register("browseWallpaperObject", "getLocalFileSizes",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "[]", ""};
            });
        d.Register("browseWallpaperObject", "getThumbnail",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "{}", ""};
            });
        d.Register("browseWallpaperObject", "removeWallpaper",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "true", ""};
            });
        d.Register("browseWallpaperObject", "selectPlaylist",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "true", ""};
            });
        d.Register("browseWallpaperObject", "updateLocalWallpaperProjectInfo",
            [](const std::vector<std::string>&) -> MethodResult {
                return {true, "[]", ""};
            });
        // browseWallpaperObject.selectWallpaper(file, location, overridetype):
        // la UI confirma el fondo con OK. Se persiste la selección en
        // res/current.json y se señaliza al motor (evento
        // dnxWallpaperV3Reload) que aplica la escena/vídeo de verdad.
        d.Register("browseWallpaperObject", "selectWallpaper",
            [](const std::vector<std::string>& a) -> MethodResult {
                std::string file = a.empty() ? "" : a[0];
                if (file.empty()) {
                    return {false, "", "no wallpaper"};
                }
                // Ítem remoto de un plugin: "wpxr:<provider>:<id>".
                if (file.rfind("wpxr:", 0) == 0) {
                    std::string key = file.substr(5);
                    const RemoteItem* ri = FindRemoteItem(key);
                    if (!ri) {
                        return {false, "", "remote item not found"};
                    }
                    // Si el medio es local (http://wpx.internal/__file/...),
                    // se aplica sin descargar. Si no, descarga en segundo
                    // plano con progreso (barra en la ficha).
                    std::wstring local = LocalPathFromWpxUrl(ri->mediaUrl);
                    std::wstring baseDir = ExeDir();
                    // Preview animado del ítem seleccionado (si aún no lo tiene).
                    EnsureRemoteThumbAsync(baseDir, key);
                    if (local.empty() ||
                        GetFileAttributesW(local.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        StartRemoteDownload(baseDir, *ri);
                        return {true, "{\"downloading\":true}", ""};
                    }
                    // Persistir selección y señalizar al motor.
                    {
                        std::string pathUtf8;
                        int n = WideCharToMultiByte(CP_UTF8, 0, local.c_str(),
                                                    -1, nullptr, 0, nullptr,
                                                    nullptr);
                        pathUtf8.resize((size_t)(n > 0 ? n - 1 : 0));
                        if (n > 0) {
                            WideCharToMultiByte(CP_UTF8, 0, local.c_str(), -1,
                                                pathUtf8.data(), n, nullptr,
                                                nullptr);
                        }
                        std::string esc;
                        for (char c : pathUtf8) {
                            if (c == '\\' || c == '"') esc.push_back('\\');
                            esc.push_back(c);
                        }
                        FILE* f = nullptr;
                        _wfopen_s(&f,
                                  (baseDir + L"\\res\\current.json").c_str(),
                                  L"w, ccs=UTF-8");
                        if (f) {
                            fwprintf(f, L"{\"project\": \"%hs\"}", esc.c_str());
                            fclose(f);
                        }
                        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE,
                                                 L"dnxWallpaperV3Reload");
                        if (ev) {
                            SetEvent(ev);
                            CloseHandle(ev);
                        }
                    }
                    return {true, "{\"selected\":\"" + file + "\"}", ""};
                }
                wchar_t exe[MAX_PATH];
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                std::wstring dir = exe;
                dir = dir.substr(0, dir.find_last_of(L"\\/"));
                FILE* f = nullptr;
                _wfopen_s(&f, (dir + L"\\res\\current.json").c_str(),
                           L"w, ccs=UTF-8");
                if (!f) {
                    return {false, "", "cannot persist selection"};
                }
                // Escapar barras invertidas para JSON válido.
                std::string fileJson;
                for (char c : file) {
                    if (c == '\\' || c == '"') {
                        fileJson.push_back('\\');
                    }
                    fileJson.push_back(c);
                }
                fwprintf(f, L"{\"project\": \"%hs\"}", fileJson.c_str());
                fclose(f);
                HANDLE ev = CreateEventW(nullptr, FALSE, FALSE,
                                         L"dnxWallpaperV3Reload");
                if (ev) {
                    SetEvent(ev);
                    CloseHandle(ev);
                }
                return {true, "{\"selected\":\"" + file + "\"}", ""};
            });
    }
    return &d;
}
#endif
