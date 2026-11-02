// reload_watcher.cpp — Hilo de recarga por evento.
#include "reload_watcher.h"

#include <windows.h>

namespace {
constexpr wchar_t kEventName[] = L"dnxWallpaperV3Reload";
}

ReloadWatcher::ReloadWatcher(Callback cb) : cb_(std::move(cb)) {}

ReloadWatcher::~ReloadWatcher() {
    Stop();
}

void ReloadWatcher::Start() {
    if (running_) {
        return;
    }
    running_ = true;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
}

void ReloadWatcher::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }
    if (thread_) {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

void ReloadWatcher::Signal() {
    HANDLE h = CreateEventW(nullptr, FALSE, FALSE, kEventName);
    if (h) {
        SetEvent(h);
        CloseHandle(h);
    }
}

unsigned long __stdcall ReloadWatcher::ThreadProc(void* self) {
    auto* w = static_cast<ReloadWatcher*>(self);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, kEventName);
    if (!ev) {
        return 1;
    }
    HANDLE wait[2] = {ev, w->stopEvent_};
    while (w->running_) {
        DWORD r = WaitForMultipleObjects(2, wait, FALSE, INFINITE);
        if (!w->running_) {
            break;
        }
        if (r == WAIT_OBJECT_0 && w->cb_) {
            w->cb_(); // aplicar selección en el hilo watcher
        } else {
            break;
        }
    }
    CloseHandle(ev);
    return 0;
}
