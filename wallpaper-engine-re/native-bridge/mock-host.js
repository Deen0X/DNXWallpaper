/**
 * Mock del host nativo para desarrollo sin el binario de Wallpaper Engine.
 *
 * En producción, `window.editorObject` y `window.ui` los expone el binario
 * nativo vía bindings CEF. Este mock implementa el MISMO protocolo observado
 * con REA (llamada directa + respuesta por callback global
 * "<metodo>Callback"/"<metodo>CallbackError") para poder desarrollar y probar
 * la UI reconstruida en un navegador estándar.
 *
 * Uso: incluir este script ANTES de app.js cuando no hay host nativo.
 *   <script src="../native-bridge/mock-host.js"></script>
 */
(function () {
  'use strict';

  if (window.editorObject && window.ui) {
    return; // Hay host nativo real, no interferir.
  }

  function respond(okName, errName, payload, isError) {
    setTimeout(function () {
      if (isError) {
        if (typeof window[errName] === 'function') {
          window[errName](payload);
        }
      } else if (typeof window[okName] === 'function') {
        window[okName](payload);
      }
    }, 0);
  }

  function makeObject(methods) {
    var obj = {};
    Object.keys(methods).forEach(function (name) {
      obj[name] = function () {
        var args = Array.prototype.slice.call(arguments);
        var result = methods[name].apply(null, args);
        respond(name + 'Callback', name + 'CallbackError', result.payload, result.isError);
        return undefined; // El binding real no retorna nada útil.
      };
    });
    return obj;
  }

  var projectsDB = [
    { name: 'beach-clone', title: 'Beach (clone)', type: 'scene', workshopid: null }
  ];

  window.editorObject = makeObject({
    getProjects: function () { return { payload: projectsDB }; },
    openProject: function (name) {
      return { payload: { name: name, tree: [], data: { type: 'scene' } } };
    },
    closeProject: function () { return { payload: true }; },
    checkUnsavedChanges: function () { return { payload: false }; },
    saveProjectData: function () { return { payload: { ok: true } }; },
    setGridVisible: function () { return { payload: true }; },
    getTexturePreview: function () { return { payload: null }; },
    // genericSceneAction_<sub>: comodín para las 70 variantes observadas.
    genericSceneAction: function () { return { payload: null }; }
  });

  window.ui = makeObject({
    getVirtualKeyNames: function () { return { payload: {} }; },
    copyToClipboard: function () { return { payload: true }; },
    refreshAuthToken: function () { return { payload: 'mock-ticket' }; },
    logError: function (e) { if (window.console) { console.error('[native]', e); } return { payload: true }; }
  });

  window.wpxGlobalData = { mock: true };

  // Resuelve genericSceneAction_<sub>Callback de forma genérica.
  var originalTimeout = window.setTimeout;
  void originalTimeout;
})();
