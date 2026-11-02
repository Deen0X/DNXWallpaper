// method_dispatch.cpp — Dispatcher de bindings (C++ puro, testeable).
#include "method_dispatch.h"

void MethodDispatcher::Register(const std::string& object,
                                const std::string& method, MethodHandler h) {
    handlers_[{object, method}] = std::move(h);
}

bool MethodDispatcher::Has(const std::string& object, const std::string& method) const {
    return handlers_.count({object, method}) > 0;
}

std::pair<std::string, MethodResult> MethodDispatcher::Dispatch(
    const std::string& object, const std::string& method,
    const std::vector<std::string>& args) {
    std::string base = method;
    // Sufijos observados en el servicio host de la UI (host.js reconstruido).
    if ((method == "genericSceneAction" && args.size() > 0) ||
        (method == "modelAction" && args.size() > 1)) {
        base += "_" + args[method == "genericSceneAction" ? 0 : 1];
    }
    auto it = handlers_.find({object, method});
    if (it == handlers_.end()) {
        return {base + "CallbackError",
                {false, "", "unknown method " + object + "." + method}};
    }
    MethodResult r = it->second(args);
    return {base + (r.ok ? "Callback" : "CallbackError"), r};
}
