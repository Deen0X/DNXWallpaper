// scene_wallpaper.h — Wallpaper tipo escena (scene.json).
//
// Original: SceneWallpaper::LoadSceneAndProperties.
// Formato documentado en wallpaper-engine-re/examples/project-format.md:
//   camera{ center, eye, up }, general{ ambientcolor, skylightcolor,
//   clearcolor, bloom, norecompile, orthogonalprojection{width,height} },
//   objects[]{ id, name, image(modelo), origin"xyz", angles"xyz", scale"xyz" }.
// Propiedades generales: general.properties.* (ej. schemecolor type=color).
#pragma once

#include <string>
#include <vector>

struct SceneCamera {
    float center[3] = {0, 0, -1};
    float eye[3] = {0, 0, 0};
    float up[3] = {0, 1, 0};
};

struct SceneObject {
    int id = 0;
    std::string name;
    std::string image; // ruta al modelo (models/*.json)
    float origin[3] = {0, 0, 0};
    float angles[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
};

struct SceneGeneral {
    float ambientcolor[3] = {0.3f, 0.3f, 0.3f};
    float skylightcolor[3] = {0.3f, 0.3f, 0.3f};
    float clearcolor[3] = {0.7f, 0.7f, 0.7f};
    bool bloom = false;
    bool norecompile = true;
    int orthoWidth = 1920, orthoHeight = 1080;
};

class SceneWallpaper {
public:
    // Carga scene.json + project.json (propiedades). Retorna false en error.
    bool LoadSceneAndProperties(const std::wstring& scenePath);

    const SceneCamera& Camera() const { return camera_; }
    const SceneGeneral& General() const { return general_; }
    const std::vector<SceneObject>& Objects() const { return objects_; }

private:
    SceneCamera camera_;
    SceneGeneral general_;
    std::vector<SceneObject> objects_;
};
