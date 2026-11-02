// material_system.cpp — Materiales y carga asíncrona (stub documentado).
#include "material_system.h"

#include <windows.h>
#include <thread>
#include <vector>

bool ReadTextureData(const std::wstring& path, TextureData& out) {
    // Stub documentado: el original soporta PNG/JPG/GIF/TGA vía
    // FreeImage + QOI experimentales (ver "Recompiling texture").
    // Aquí se valida existencia y se retorna placeholder 1x1.
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    if (out.width == 0 || out.height == 0 || out.width > 16384 || out.height > 16384) {
        wchar_t msg[256];
        swprintf_s(msg, L"Invalid texture resolution: w=%u h=%u d=%u\n",
                   out.width, out.height, out.depth);
        OutputDebugStringW(msg);
        return false;
    }
    out.path = path;
    out.rgba.assign((size_t)out.width * out.height * 4, 255);
    out.dirty = false;
    return true;
}

void AsyncTextureLoader::QueueAsyncTextureLoad(const std::wstring& path, Callback cb) {
    // Hilo de fondo por textura (el original usa un pool AsyncLoaderThread).
    std::lock_guard<std::mutex> lock(mutex_);
    std::thread([path, cb] {
        TextureData tex;
        tex.width = 256;
        tex.height = 256;
        if (ReadTextureData(path, tex)) {
            cb(tex);
        }
    }).detach();
}

void AsyncTextureLoader::WaitAll() {
    // Con hilos detached no hay join; punto de sincronización documentado
    // para una futura migración a pool con join.
}

bool MaterialSystem::ReloadDirtyMaterials() {
    for (const auto& name : dirty_) {
        wchar_t msg[512];
        swprintf_s(msg, L"Recompiling texture: %s\n", name.c_str());
        OutputDebugStringW(msg);
    }
    bool had = !dirty_.empty();
    dirty_.clear();
    return had;
}
