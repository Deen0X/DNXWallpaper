// editor_export.h — exportación de wallpapers para dispositivos (Fase 1).
#pragma once

#include <string>

// Resuelve una ruta de origen permitida. Por defecto debe estar bajo res/;
// allowExternal habilita vídeos/imágenes locales de cualquier ruta (sólo se
// usa para peticiones desde 127.0.0.1, p. ej. el host device-editor.exe).
bool EditorResolveSource(const std::wstring& baseDir,
                         const std::string& file, std::wstring& full,
                         bool allowExternal = false);

// Sondea un vídeo: dimensiones, duración y HDR.
std::string EditorProbeJson(const std::wstring& baseDir, const std::string& file,
                            bool allowExternal = false);

// Sondea un fichero local (subida del móvil) y genera un póster JPEG.
// Devuelve JSON: {"ok":true,"width":..,"height":..,"duration":..}
bool EditorProbeFile(const std::wstring& file, std::wstring& posterOut,
                     int& width, int& height, double& duration);

// Dimensiones y duración de un medio (sin generar póster).
bool EditorProbeDims(const std::wstring& file, int& width, int& height,
                     double& duration);

// Ruta del ffmpeg que usa el exportador (vacío si no se encuentra).
std::wstring EditorFfmpegPath();

// Devuelve el fondo actual (res/current.json) como {"ok":true,"file":"..."}.
std::string EditorCurrentJson(const std::wstring& baseDir);

// Inicia una exportación. bodyJson: {file,title,device,rect:{x,y,w,h},
// target,quality,fade,square}. Devuelve {"ok":true,"id":"..."} o error.
std::string StartEditorExport(const std::wstring& baseDir,
                              const std::string& bodyJson,
                              bool allowExternal = false);

// Estado del job: {ok,state,percent,error,id,width,height,size,duration}
std::string EditorExportStatusJson(const std::string& id);

// Cancela un job en curso.
bool CancelEditorExport(const std::string& id);
