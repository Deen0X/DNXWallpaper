// image_load.h — Decodificación de imágenes vía WIC (parte del SO).
//
// Sin dependencias externas: Windows Imaging Component decodifica
// PNG/JPEG/BMP/GIF/TIFF a RGBA8. Se usa para fondos de imagen y previews.
#pragma once

#include <string>
#include <vector>

struct ImageRGBA {
    unsigned width = 0, height = 0;
    std::vector<unsigned char> rgba; // RGBA8, width*height*4
    bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
};

// Decodifica cualquier formato soportado por WIC. Retorna true si OK.
bool LoadImageWIC(const std::wstring& path, ImageRGBA& out);
