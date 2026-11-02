// plugin_host.h — Host de plugins de catálogo (contrato v1).
//
// Descubre dist\plugins\<id>\plugin.json, ejecuta comandos CLI y convierte
// los Item en entradas de la galería. Los ítems remotos se marcan como
// descargables ("downloadable") hasta que el usuario los aplica.
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

struct PluginInfo {
    std::string id;
    std::string name;
    std::wstring exePath;     // ruta absoluta del ejecutable/entry
    std::string attribution;
    bool python = false;      // runtime "python" (contrato v2)
};

struct RemoteItem {
    std::string key;          // "<provider>:<id>"
    std::string provider;
    std::string id;
    std::string title;
    std::string mediaType;    // video|image
    std::string staticThumb;  // URL (o ruta local cacheada vía wpx.internal)
    std::string animatedThumb;// URL (vacío si el sitio no la ofrece)
    // El plugin marca true cuando su póster/preview no conserva la proporción
    // del medio; en ese caso se prefiere `thumb` del plugin.
    bool staticCropped = false;
    bool animatedCropped = false;
    std::string mediaUrl;     // URL
    bool animated = false;
    std::string tags;         // lista separada por comas
    int width = 0, height = 0;
    std::string publishedAt;
    double rating = -1.0;     // <0 = sin dato
    long long sizeBytes = 0;  // tamaño esperado del medio (si el plugin lo da)
    bool downloadable = true;
};

// Recarga cada plugin en una página aleatoria (barajado).
std::string RandomizeLibraryJson(const std::wstring& baseDir,
                                 int perPlugin, int maxPage);

// Ids de los plugins conocidos (para barajar el catálogo).
std::vector<std::string> KnownPluginIds();

// Elimina del catálogo los items de un proveedor.
void RemoveProvider(const std::string& provider);

// Elimina del catálogo los items de plugins que ya no existen.
void PruneProviders(const std::vector<std::string>& keep);

// TTL (horas) de la caché por plugin+página (0 = sin caché).
void SetPluginCacheTtlHours(int hours);

// Descubre plugins instalados en <baseDir>\plugins.
std::vector<PluginInfo> DiscoverPlugins(const std::wstring& baseDir);

// Ejecuta un comando del plugin y devuelve stdout (vacío en error).
std::string RunPluginCommand(const PluginInfo& plugin,
                             const std::wstring& args, int timeoutMs,
                             int* exitCode = nullptr);

// Lista ítems de un plugin (comando `list --limit N`).
std::vector<RemoteItem> ListPluginItems(const PluginInfo& plugin, int limit);

// Carga hasta maxItems recorriendo páginas de 100 (contrato: limit <= 100).
std::vector<RemoteItem> ListPluginItemsPaged(
    const PluginInfo& plugin, int maxItems,
    const std::function<void(const std::vector<RemoteItem>&)>& onPage = {});

// Recarga profunda del catálogo: pide `pages` páginas a cada plugin y guarda el
// snapshot. Bloquea: llamar desde un hilo aparte.
int RefreshCatalogDeep(const std::wstring& baseDir, int pages);
bool DeepScanRunning();
std::string DeepScanStatusJson();

// Fusiona ítems en el registro global (dedupe por key) y devuelve copia.
void MergeRemoteItems(const std::vector<RemoteItem>& items);
std::vector<RemoteItem> GetRemoteRegistryCopy();

// Actualiza las dimensiones de un item si no las tenía (al descargarlo).
void UpdateRemoteItemDims(const std::string& key, int width, int height);

// Guarda el snapshot periódicamente si el catálogo cambió (dimensiones, etc.).
void StartCatalogSaver(const std::wstring& baseDir);

// Servidor LAN (app móvil): ruta del medio original y de la miniatura local.
std::wstring RemoteMediaPath(const std::wstring& baseDir, const std::string& key);
std::wstring RemoteThumbPath(const std::wstring& baseDir, const std::string& key);

// Descarga sincrona del medio (servidor LAN / QR). true si queda en disco.
// Versión 1080p H.264 para el móvil (la genera si falta).
std::wstring EnsureMobileProxy(const std::wstring& baseDir,
                               const std::string& key);

// Variante asíncrona: arranca/reutiliza un job y espera hasta timeoutMs.
std::wstring EnsureMobileProxyAsync(const std::wstring& baseDir,
                                    const std::string& key, int timeoutMs);

// Ruta del proxy ya generado (sin lanzar nada). Vacío si no existe.
std::wstring MobileProxyReadyPath(const std::wstring& baseDir,
                                  const std::string& key);

// Estado del job: {state, percent, size, path}
std::string MobileProxyStatusJson(const std::string& key);

// Cancela el job en curso (si existe).
bool CancelMobileProxy(const std::string& key);

// Metadatos (JSON) de un ítem, sin descargar el medio.
std::string RemoteItemJson(const std::string& key);

bool FetchRemoteMediaSync(const std::wstring& baseDir, const std::string& key,
                          int timeoutMs);

// Genera la miniatura animada de un ítem concreto (bajo demanda).
void EnsureRemoteThumbAsync(const std::wstring& baseDir, const std::string& key);

// Encola varias miniaturas al principio de la cola de su proveedor.
// No bloquea: el servidor LAN responde 200 inmediatamente. Devuelve cuántas
// claves aceptó (existentes o ya en cola).
int PrefetchRemoteThumbsAsync(const std::wstring& baseDir,
                              const std::vector<std::string>& keys);

// Construye el JSON de la galería con los ítems remotos.
// file = "wpxr:<provider>:<id>"; preview = staticThumb.
std::string BuildRemoteItemsJson(const std::vector<RemoteItem>& items);

// Registro global de ítems remotos conocidos (para resolver al aplicar).
void SetRemoteRegistry(const std::vector<RemoteItem>& items);
const RemoteItem* FindRemoteItem(const std::string& key);

// Descarga un ítem remoto a res\providers\<provider>\media\<id>.<ext>.
// Devuelve la ruta local o "" en error. Ejecuta el plugin `fetch`.
std::wstring FetchRemoteItem(const std::wstring& baseDir, const RemoteItem& item);

// Igual que FetchRemoteItem pero eligiendo qué pide al plugin:
//   "media"   -> el wallpaper (respaldo: preview si el MP4 está protegido)
//   "preview" -> el clip ligero para la vista previa animada
std::wstring FetchRemoteItemKind(const std::wstring& baseDir,
                                 const RemoteItem& item,
                                 const std::string& kind);

// Clip de preview resuelto por el plugin (o por HTTP como último recurso).
std::wstring FetchRemotePreviewSync(const std::wstring& baseDir,
                                    const std::string& key, int timeoutMs);

// Convierte una URL http://wpx.internal/__file/<ruta> a ruta local.
std::wstring LocalPathFromWpxUrl(const std::string& url);

// --- Descargas asíncronas concurrentes (máx. 3) ---
// Inicia la descarga de un ítem remoto. Devuelve false si ya está activa.
bool StartRemoteDownload(const std::wstring& baseDir, const RemoteItem& item);

// Estado de todas las descargas conocidas (JSON array):
// [{key,active,done,error,bytes,total,message}]
std::string GetDownloadStatusJson();

// Progreso del escaneo inicial (lo actualiza el host mientras lista).
void SetPluginProgress(bool loading, int done, int total, int items);

// Estado del escaneo inicial de plugins: {loading,done,total,items}
std::string GetPluginStatusJson();

// Plugins conocidos (para el selector de la pestaña Plugins).
void SetKnownPlugins(const std::vector<PluginInfo>& plugins);
std::string BuildPluginListJson();

// Carga una página de un plugin (list --page N --limit M [--query q]),
// reemplaza sus ítems en el catálogo y devuelve el catálogo completo.
std::string LoadPluginPageJson(const std::wstring& baseDir,
                               const std::string& pluginId, int page,
                               const std::string& query, int limit);

// Búsqueda en vivo en un plugin (`list --query`). Devuelve sólo metadatos y no
// modifica el catálogo.
std::vector<RemoteItem> SearchPluginItems(const std::wstring& baseDir,
                                          const std::string& pluginId,
                                          const std::string& query, int limit);

// --- Snapshot del catálogo (arranque instantáneo) ---
// Guarda/carga res\cache\catalog.json para que la UI y /api/library no
// esperen a que los plugins respondan tras un arranque en frío.
bool LoadCatalogSnapshot(const std::wstring& baseDir);
void SaveCatalogSnapshot(const std::wstring& baseDir,
                         const std::vector<RemoteItem>& items);

// --- Miniaturas locales (caché en disco) ---
// Versión del catálogo: cambia cuando hay ítems nuevos o miniaturas
// recién cacheadas. La UI la consulta para no re-transferir el JSON.
int GetRemoteVersion();

// Descarga en segundo plano miniaturas (estáticas y animadas) a
// res\cache\thumbs\remote\<provider>\ y actualiza el registro con URLs
// locales. maxStatic/maxAnimated limitan el trabajo por llamada.
void WarmRemoteThumbs(const std::wstring& baseDir, int maxStatic,
                      int maxAnimated);
