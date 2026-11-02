// projects_inventory.h — Inventario de wallpapers (proyectos + carpetas).
#pragma once
#include <string>

// Construye el JSON que consume la galería:
// [{file,title,type,official,status,preview,previewsmall}]
// - res/projects/*/project.json (proyectos locales)
// - carpetas de vídeo de res/config/video_sources.json (miniaturas reales)
std::string BuildProjectsJson(const std::wstring& baseDir);

// Escanea las carpetas de vídeo configuradas y genera miniaturas en
// res/cache/thumbs (idempotente; se llama al arrancar y tras importar).
void SyncVideoSources(const std::wstring& baseDir);

// Mapa JSON {"<url-miniatura>":"<url-vídeo>"} para que la UI pueda asociar
// cada tile/preview con su archivo de vídeo (scope Angular no accesible).
std::string BuildPreviewFileMap(const std::wstring& baseDir);
