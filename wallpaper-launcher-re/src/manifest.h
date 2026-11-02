// manifest.h — Carga del manifiesto EXTERNO de hashes (res/hashes.json).
//
// El launcher original lleva la tabla ruta->sha256 embebida en .rdata.
// Aquí vive en res/hashes.json: {"files": {"ruta/relativa": "sha256hex"}}.
// Parser mínimo sin dependencias (JSON plano de un nivel).
#pragma once

#include <string>
#include <vector>

#include "file_verify.h" // ExpectedFileHash

// Carga res/hashes.json desde baseDir. Retorna la tabla (vacía si no existe
// o está vacío -> verificación omitida, modo desarrollo).
std::vector<ExpectedFileHash> LoadHashManifest(const std::wstring& baseDir);
