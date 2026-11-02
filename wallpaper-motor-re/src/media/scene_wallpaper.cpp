// scene_wallpaper.cpp — Carga mínima de scene.json (parser propio, sin deps).
#include "scene_wallpaper.h"

#include <fstream>
#include <sstream>

namespace {

std::string ReadAll(const std::wstring& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Extrae el valor string de "key" : "value" (JSON simple, una línea).
std::string GetStr(const std::string& json, const std::string& key) {
    std::string k = "\"" + key + "\"";
    size_t pos = json.find(k);
    if (pos == std::string::npos) {
        return "";
    }
    pos = json.find(':', pos + k.size());
    if (pos == std::string::npos) {
        return "";
    }
    size_t a = json.find('"', pos);
    if (a == std::string::npos) {
        return "";
    }
    size_t b = json.find('"', a + 1);
    if (b == std::string::npos) {
        return "";
    }
    return json.substr(a + 1, b - a - 1);
}

void ParseVec3(const std::string& s, float v[3]) {
    if (sscanf_s(s.c_str(), "%f %f %f", &v[0], &v[1], &v[2]) != 3) {
        v[0] = v[1] = v[2] = 0;
    }
}

} // namespace

bool SceneWallpaper::LoadSceneAndProperties(const std::wstring& scenePath) {
    std::string json = ReadAll(scenePath);
    if (json.empty()) {
        return false;
    }
    // Cámara (valores por defecto = los del proyecto beach oficial).
    std::string v;
    v = GetStr(json, "center");
    if (!v.empty()) {
        ParseVec3(v, camera_.center);
    }
    v = GetStr(json, "eye");
    if (!v.empty()) {
        ParseVec3(v, camera_.eye);
    }
    v = GetStr(json, "up");
    if (!v.empty()) {
        ParseVec3(v, camera_.up);
    }
    v = GetStr(json, "ambientcolor");
    if (!v.empty()) {
        ParseVec3(v, general_.ambientcolor);
    }
    v = GetStr(json, "clearcolor");
    if (!v.empty()) {
        ParseVec3(v, general_.clearcolor);
    }
    v = GetStr(json, "skylightcolor");
    if (!v.empty()) {
        ParseVec3(v, general_.skylightcolor);
    }
    general_.bloom = json.find("\"bloom\"") != std::string::npos &&
                     json.find("\"bloom\" : true") != std::string::npos;

    // Objetos: barrido simple por bloques {"angles"..."scale"}.
    size_t pos = 0;
    while ((pos = json.find("\"id\"", pos)) != std::string::npos) {
        SceneObject o;
        size_t end = json.find('}', pos);
        std::string block = json.substr(pos, end - pos);
        size_t p = block.find("\"id\"");
        if (p != std::string::npos) {
            o.id = atoi(block.c_str() + p + 5);
        }
        o.name = GetStr(block, "name");
        o.image = GetStr(block, "image");
        v = GetStr(block, "origin");
        if (!v.empty()) {
            ParseVec3(v, o.origin);
        }
        v = GetStr(block, "angles");
        if (!v.empty()) {
            ParseVec3(v, o.angles);
        }
        v = GetStr(block, "scale");
        if (!v.empty()) {
            ParseVec3(v, o.scale);
        } else {
            o.scale[0] = o.scale[1] = o.scale[2] = 1;
        }
        objects_.push_back(o);
        pos = end;
    }
    return true;
}
