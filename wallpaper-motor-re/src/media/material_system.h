// material_system.h + texture_cache.h — Materiales y caché de texturas.
//
// Original: MaterialSystem::ReloadDirtyMaterials,
// DataCache::AddSharedRemoveListenerToLayer,
// Texture::ReadTextureData,
// DesktopMediaExtensions::QueueAsyncTextureLoad (+ StartMediaControl),
// PropertySystem::LoadMediaIntegrationOnDemand /
//   LoadShellThumbnailIntegrationOnDemand.
// Mensajes: "Failed loading effect: %s", "Recompiling texture: %S",
// "Invalid texture resolution: w=%u h=%u d=%u", "Shader data overflow %s".
// Efectos: FADEEFFECT, SHADERVERSION, g_TextureN (uniform sampler regex
// "uniform[\s]+(sampler[\w]*)[\s]+g_Texture([\d]+)").
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <map>

// Una textura cargada (RGBA8 en RAM; subida a D3D11 bajo demanda).
struct TextureData {
    std::wstring path;
    unsigned width = 0, height = 0, depth = 1;
    std::vector<unsigned char> rgba;
    bool dirty = true;
};

// Original: Texture::ReadTextureData. Lee el archivo a RGBA (vía WIC/QOI
// simple según extensión; aquí: validación + stub documentado).
bool ReadTextureData(const std::wstring& path, TextureData& out);

// Original: DesktopMediaExtensions::QueueAsyncTextureLoad — cola de carga
// asíncrona con hilos de fondo (AsyncLoaderThread en símbolos).
class AsyncTextureLoader {
public:
    using Callback = std::function<void(TextureData&)>;
    void QueueAsyncTextureLoad(const std::wstring& path, Callback cb);
    void WaitAll(); // espera a la cola (para shutdown limpio)

private:
    std::mutex mutex_;
};

// Original: MaterialSystem::ReloadDirtyMaterials — recompila materiales
// cuyas texturas cambiaron ("Recompiling texture: %S").
class MaterialSystem {
public:
    bool ReloadDirtyMaterials();
    void MarkDirty(const std::wstring& effectName) {
        dirty_.push_back(effectName);
    }

private:
    std::vector<std::wstring> dirty_;
};
