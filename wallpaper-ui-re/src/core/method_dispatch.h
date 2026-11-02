// method_dispatch.h — Dispatcher de los bindings editorObject / ui.
//
// Protocolo observado (JS en ui/dist/scripts/scripts.js + tabla de métodos
// en DNXWallpaper.UI.exe .rdata junto a "editorObject"):
//   JS: window.editorObject.<metodo>(...args)  [binding V8 creado en
//         CefEngine::OnContextCreated]
//       Nativo: ejecuta y responde window.<metodo>Callback(...) /
//         window.<metodo>CallbackError(...)
//   Casos especiales: "genericSceneAction_<sub>", "modelAction_<sub>".
// Métodos editorObject observados en el binario (adyacentes a "editorObject"):
//   updateExportStatus, exportStatusCtrl, updateTexturePreview,
//   navigateBackwards, navigateForwards, previewSizeChanged,
//   previewToggleVisibility, restartPreview, saveEditorSettings,
//   importFile, getImportFileList, ... (+ "methodName", "dstPath").
// Esta unidad es C++ puro: registra handlers por nombre y construye el
// nombre del callback de respuesta. La integración V8 vive en cef/cef_engine.cpp.
#pragma once

#include <string>
#include <functional>
#include <map>
#include <vector>

// Resultado de ejecutar un método del bridge.
struct MethodResult {
    bool ok = true;
    std::string json; // payload serializado para el Callback
    std::string error;
};

using MethodHandler = std::function<MethodResult(const std::vector<std::string>& args)>;

class MethodDispatcher {
public:
    void Register(const std::string& object, const std::string& method, MethodHandler h);

    // Ejecuta object.method(args). Retorna el NOMBRE del callback a invocar
    // en JS ("<metodo>Callback" / "<metodo>CallbackError") y el resultado.
    // Aplica los sufijos genericSceneAction_<sub> / modelAction_<sub>.
    std::pair<std::string, MethodResult> Dispatch(
        const std::string& object, const std::string& method,
        const std::vector<std::string>& args);

    bool Has(const std::string& object, const std::string& method) const;

private:
    std::map<std::pair<std::string, std::string>, MethodHandler> handlers_;
};
