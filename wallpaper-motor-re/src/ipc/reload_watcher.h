// reload_watcher.h — Recarga de escena bajo demanda (sin Steam).
//
// El host UI escribe res/current.json {"project":"<carpeta>"} y señaliza
// el evento global "dnxWallpaperV3Reload". Este watcher (hilo propio)
// despierta al motor, que aplica la escena (LoadScene + ShowContent +
// game loop). Sin pipes ni red: solo filesystem + evento con nombre.
#pragma once

#include <functional>

class ReloadWatcher {
public:
    using Callback = std::function<void()>;
    explicit ReloadWatcher(Callback cb);
    ~ReloadWatcher();

    void Start();
    void Stop();

    // Lado UI: señalizar recarga (abre/crea el evento y lo activa).
    static void Signal();

private:
    Callback cb_;
    void* thread_ = nullptr;
    void* stopEvent_ = nullptr;
    bool running_ = false;
    static unsigned long __stdcall ThreadProc(void* self);
};
