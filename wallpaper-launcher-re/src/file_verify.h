// file_verify.h — Verificación de integridad de la distribución.
//
// RECONSTRUIDO con REA + Rizin desde DNXWallpaper.exe (PE64, MSVC Linker 14.51).
// Función original: fcn.140008100 (169 bloques, 5351 bytes).
// Mensajes observados en .rdata:
//   "VerifyFileHashes dst file not found: %S"
//   "VerifyFileHashes src file not found: %S"
//   "VerifyFileHashes can't open dst file: %S"
//   "VerifyFileHashes hash failed for %S (%s != %s)"
#pragma once

#include <string>
#include <vector>

// Un hash esperado por archivo (tabla embebida en el binario original:
// pares <ruta relativa, sha256 hex>. En el original la tabla incluye
// bin/libcef.dll, bin/chrome_*.pak, bin/*.dll, DNXWallpaper.Motor.exe, etc.).
struct ExpectedFileHash {
    std::wstring relativePath; // ej. L"bin/libcef.dll"
    std::string sha256Hex;     // 64 chars hex en minúsculas
};

// Calcula SHA256 de un archivo. Retorna hex en minúsculas o "" en error.
std::string ComputeFileSHA256(const std::wstring& path);

// Verifica todos los hashes. Retorna true si todo coincide.
// No depende de Steam: es verificación local contra la tabla embebida.
bool VerifyFileHashes(const std::wstring& baseDir,
                      const std::vector<ExpectedFileHash>& table,
                      std::wstring& errorOut);
