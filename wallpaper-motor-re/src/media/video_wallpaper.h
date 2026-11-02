// video_wallpaper.h — Wallpaper de vídeo (Media Foundation + pumping).
//
// Original: VideoWallpaper::StartVideoWithNewPlayer.
// Hilo lector: IMFSourceReader con salida RGB32, convierte a RGBA y
// expone el último frame (thread-safe). El motor lo sube a D3D11.
#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>

struct VideoFrame {
    unsigned width = 0, height = 0;
    std::vector<unsigned char> rgba; // RGBA8
    long long timestamp100ns = 0;
    bool valid = false;
};

class VideoWallpaper {
public:
    VideoWallpaper() = default;
    ~VideoWallpaper() { Stop(); }

    // Inicia el reproductor y el hilo de lectura. Retorna false en error
    // ("Failed creating video player (%s): %x").
    bool StartVideoWithNewPlayer(const std::wstring& path);
    void Stop();

    // Copia el último frame disponible (no bloquea). Retorna false si no
    // hay frame nuevo desde la última llamada.
    bool TakeFrame(VideoFrame& out);

    void SetMuted(bool muted) { muted_ = muted; }
    // Calidad: tope de altura de decodificacion (0 = nativa).
    // Se aplica al crear el siguiente lector (no en caliente).
    void SetMaxHeight(int px) { maxHeight_ = px; }
    int MaxHeight() const { return maxHeight_; }
    // Pausa la decodificación (optimización: wallpaper oculto/ventana a pantalla
    // completa). El último frame permanece.
    void SetPaused(bool paused) { paused_ = paused; }
    bool IsPaused() const { return paused_; }
    bool IsPlaying() const { return playing_; }

private:
    static unsigned long __stdcall ReadThread(void* self);
    void ReadLoop();

    bool playing_ = false;
    bool muted_ = true; // por defecto: "Media Foundation (muted)"
    int maxHeight_ = 0; // 0 = resolucion nativa
    bool stopRequested_ = false;
    volatile bool paused_ = false;
    bool frameFresh_ = false;
    std::wstring path_;
    void* reader_ = nullptr; // IMFSourceReader* (opaco aquí)
    void* thread_ = nullptr;
    std::mutex mutex_;
    VideoFrame last_;
};
