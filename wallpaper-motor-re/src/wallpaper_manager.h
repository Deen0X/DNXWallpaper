// wallpaper_manager.h — Gestor principal del motor.
//
// RECONSTRUIDO con REA + Rizin desde distribution/DNXWallpaper.Motor.exe
// (PE64, 5.11 MB, MSVC 14.51). Clase original: WallpaperManager::Init.
// Ventanas: "DNXWallpaperV3Parent" / "DNXWallpaperV3Wallpaper" (.rdata).
// Mutex global: "dnxWallpaperV3GlobalMutex". SIN Steam: sin
// "-op steamscreenshot", sin lectura de Software\Valve\Steam\Users,
// sin WriteSteamMiniDump/steammdmp64.
#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <map>

enum class WallpaperType {
    Unknown,
    Scene,        // scene.json — SceneWallpaper::LoadSceneAndProperties
    Video,        // VideoWallpaper::StartVideoWithNewPlayer
    Web,         // via webwallpaper64 (proceso aparte)
    Application, // via applicationwallpaperinject (proceso aparte)
};

struct MonitorTarget {
    int index = 0;
    int x = 0, y = 0, width = 1920, height = 1080;
};

struct EngineOptions {
    WallpaperType type = WallpaperType::Scene;
    std::wstring file;        // -file / ruta scene.json o vídeo
    std::wstring location;    // -location
    std::wstring preset;      // -preset
    std::wstring playlist;    // -playlist
    std::wstring profile;     // -profile
    std::wstring properties;  // -properties (JSON user props)
    std::wstring language = L"en";
    int monitor = -1;         // -monitor (-1 = todos)
    bool safe = false;        // -safe
    bool disableFx = false;   // -disablefx
    bool disableGifs = false; // -disablegifs
    bool noWallpapers = false;// -nowallpapers
    bool silent = false;      // -silent
    bool borderless = false;  // -borderless
    bool activate = false;    // -activate
};

class WallpaperManager {
public:
    static WallpaperManager& Instance();

    // Original: WallpaperManager::Init. Crea D3D11, attach DWM, carga escena.
    bool Init(const EngineOptions& opts);
    void Shutdown();

    // Original: Main::LoadScene — resuelve tipo y delega.
    bool LoadScene(const std::wstring& path, WallpaperType type);

    // Muestra la ventana (tras DWM) cuando hay contenido que presentar.
    // Sin escena: la ventana permanece invisible (sin velo en el escritorio).
    void ShowContent();

    // Presenta un frame (clearcolor de la escena). Game loop mínimo.
    void RenderFrame();

    // Aplica la selección de res/current.json {"project":"<carpeta>"}:
    // lee su project.json y carga la escena/vídeo. Retorna true si aplicó.
    // Lo invoca el watcher (UI) y el arranque (persistencia).
    bool ApplySelection();

    // Aplica un archivo concreto (imagen o vídeo), con ruta absoluta.
    bool ApplyFile(const std::wstring& path);

    // Playlist: carga res/config/playlist.json y avanza con crossfade.
    void LoadPlaylistConfig();
    void UpdatePlaylist();   // llamado cada frame
    bool PlaylistEnabled() const;
    void SetPlaylistEnabled(bool on);

    // Modo de ajuste (fill/fit/stretch) desde res/config/playback.json.
    void LoadPlaybackSettings();

    // Captura el frame actual a PNG (bandeja: "Hacer captura de pantalla").
    bool CaptureScreenshot(const std::wstring& path);

    // Modo salvapantallas: ventana topmost a pantalla completa, sale con
    // cualquier entrada. Bloquea hasta que el usuario interactúa.
    int RunScreensaverMode();

    bool HasScene() const;

    // HWND del contenedor de fondo (para optimizaciones de visibilidad).
    HWND ParentHwnd() const;
    // Pausa/reanuda la decodificación de vídeo.
    void SetVideoPaused(bool paused);
    // Relee res/config/optimizations.json (calidad/HDR) y reinicia el
    // decoder si cambió el tope de resolución.
    void ReloadDisplaySettings();
    // Silencia/activa el audio del vídeo.
    void SetVideoMuted(bool muted);

    const std::vector<MonitorTarget>& Monitors() const { return monitors_; }

private:
    WallpaperManager() = default;
    bool EnumerateMonitors();
    bool EnsureSingleInstance();

    EngineOptions opts_;
    std::vector<MonitorTarget> monitors_;
    void* instanceMutex_ = nullptr;
};
