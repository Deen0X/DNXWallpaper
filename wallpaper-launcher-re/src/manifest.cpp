// manifest.cpp — Carga del manifiesto EXTERNO de hashes.
#include "manifest.h"

#include <windows.h>
#include <fstream>
#include <sstream>

namespace {

std::string ReadAll(const std::wstring& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n\"");
    size_t b = s.find_last_not_of(" \t\r\n\"");
    if (a == std::string::npos) {
        return "";
    }
    return s.substr(a, b - a + 1);
}

} // namespace

std::vector<ExpectedFileHash> LoadHashManifest(const std::wstring& baseDir) {
    std::vector<ExpectedFileHash> table;
    std::wstring wpath = baseDir + L"\\res\\hashes.json";
    std::string json = ReadAll(wpath);
    if (json.empty()) {
        return table; // sin manifiesto -> modo desarrollo
    }
    // Parser mínimo: pares "ruta" : "sha256" dentro de "files".
    size_t files = json.find("\"files\"");
    if (files == std::string::npos) {
        return table;
    }
    size_t pos = json.find('{', files);
    while (pos != std::string::npos) {
        size_t k1 = json.find('"', pos);
        if (k1 == std::string::npos) {
            break;
        }
        size_t k2 = json.find('"', k1 + 1);
        if (k2 == std::string::npos) {
            break;
        }
        size_t colon = json.find(':', k2);
        if (colon == std::string::npos) {
            break;
        }
        size_t v1 = json.find('"', colon);
        if (v1 == std::string::npos) {
            break;
        }
        size_t v2 = json.find('"', v1 + 1);
        if (v2 == std::string::npos) {
            break;
        }
        ExpectedFileHash e;
        std::string ks = json.substr(k1 + 1, k2 - k1 - 1);
        std::string vs = json.substr(v1 + 1, v2 - v1 - 1);
        e.relativePath.assign(ks.begin(), ks.end());
        e.sha256Hex = Trim(vs);
        if (!e.sha256Hex.empty()) { // entradas vacías = placeholder, se omiten
            for (auto& c : e.relativePath) {
                if (c == L'/') {
                    c = L'\\';
                }
            }
            table.push_back(e);
        }
        pos = v2 + 1;
        // Fin del objeto files.
        size_t nextBrace = json.find('}', pos);
        size_t nextQuote = json.find('"', pos);
        if (nextBrace != std::string::npos &&
            (nextQuote == std::string::npos || nextBrace < nextQuote)) {
            break;
        }
    }
    return table;
}
