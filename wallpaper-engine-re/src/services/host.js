/**
 * Servicio `host` — Bridge JS <-> Aplicación nativa (CEF).
 *
 * RECONSTRUIDO mediante ingeniería inversa con REA (rea-agents v6.8.0)
 * sobre: wallpaper_engine/ui/dist/scripts/scripts.js
 *
 * Mecanismo descubierto (servicio AngularJS "host", offset ~303129 del bundle):
 *
 *   callDeferred(objectName, methodName, ...args) -> Promise
 *     1. Registra dos callbacks globales en `window`:
 *          window["<method>Callback"]      -> resuelve la promesa
 *          window["<method>CallbackError"] -> rechaza la promesa (+ toast de error)
 *     2. Invoca el binding CEF expuesto por el host nativo:
 *          window[objectName][methodName](...args)
 *        donde objectName es "editorObject" o "ui".
 *     3. El binario nativo ejecuta la acción y responde llamando al
 *        callback global correspondiente. Los callbacks se limpian tras usarse.
 *
 *   Casos especiales de nombres (observados en el bundle original):
 *     - "genericSceneAction" + subacción  -> "<method>_​<subacción>Callback"
 *     - "modelAction" + subacción          -> "<method>_​<subacción>Callback"
 *
 * Objetos nativos expuestos vía CEF (window.*):
 *   - window.editorObject : bridge al editor nativo (openProject, saveProjectData, ...)
 *   - window.ui           : funciones UI nativas (copyToClipboard, refreshAuthToken, ...)
 *   - window.wpxGlobalData: datos globales de sesión
 *
 * Métodos callEditor observados (top por frecuencia en el bundle):
 *   genericSceneAction (70), sendSceneOrderChange, sceneMouseMove,
 *   dragRenderableItem, saveSceneGeneralData, importTextureFile,
 *   setViewTransforms, preparePublishAssets, importImage, importSound,
 *   addSound, saveProjectData, openProject, closeProject, deleteProject,
 *   checkUnsavedChanges, getProjects, getTexturePreview, ...
 *   (lista completa en ../../evidence/rea-findings.md)
 */
angular.module('wallpaperbrowserApp').service('host', [
  '$q', '$http', '$timeout', '$translate', '$httpParamSerializer', 'modals', 'utils',
  function ($q, $http, $timeout, $translate, $httpParamSerializer, modals, utils) {
    var self = this;
    var suppressedErrors = {};
    var authTicket = null;
    var authTicketPromise = null;
    var authRetryCount = 0;

    /** Suprime el toast de error para un método concreto. */
    this.setCallErrorSuppressed = function (methodName, suppress) {
      suppressedErrors[methodName + 'Callback'] = suppress;
    };

    /**
     * Llamada genérica al host nativo.
     * @param {string} objectName "editorObject" | "ui"
     * @param {string} methodName método del binding CEF
     * @returns {Promise} se resuelve con los argumentos del callback nativo
     */
    this.callDeferred = function (objectName, methodName) {
      var deferred = $q.defer();
      var args = [];
      for (var i = 2; i < arguments.length && !angular.isUndefined(arguments[i]); ++i) {
        args.push(arguments[i]);
      }

      // Sufijos especiales observados en el bundle original.
      var base = methodName;
      if (base === 'genericSceneAction' && arguments.length > 2) {
        base += '_' + arguments[2];
      } else if (base === 'modelAction' && arguments.length > 3) {
        base += '_' + arguments[3];
      }
      var okName = base + 'Callback';
      var errName = base + 'CallbackError';

      function cleanup() {
        window[okName] = undefined;
        window[errName] = undefined;
      }

      window[okName] = function () {
        cleanup();
        deferred.resolve.apply(null, arguments);
      };
      window[errName] = function () {
        cleanup();
        if (!suppressedErrors[okName] && arguments[0]) {
          utils.showError(arguments[0]);
        }
        deferred.reject.apply(null, arguments.length > 1 ? [arguments[1]] : [undefined]);
      };

      window[objectName][methodName].apply(window[objectName], args);
      return deferred.promise;
    };

    /** Atajo hacia window.editorObject. */
    this.callEditor = function (methodName) {
      var params = ['editorObject', methodName];
      for (var i = 1; i < arguments.length; ++i) {
        params.push(arguments[i]);
      }
      return this.callDeferred.apply(this, params);
    };

    /** Atajo hacia window.ui. */
    this.callUI = function (methodName) {
      var params = ['ui', methodName];
      for (var i = 1; i < arguments.length; ++i) {
        params.push(arguments[i]);
      }
      return this.callDeferred.apply(this, params);
    };

    /** Log de errores hacia el host nativo (si está disponible). */
    this.logError = function (err) {
      if (window.ui && window.ui.logError) {
        window.ui.logError(err);
      }
    };

    /** Nombres de teclas virtuales (cacheado, vía host). */
    var virtualKeyNamesPromise;
    this.getVirtualKeyNames = function () {
      if (angular.isUndefined(virtualKeyNamesPromise)) {
        virtualKeyNamesPromise = self.callDeferred('ui', 'getVirtualKeyNames');
      }
      return virtualKeyNamesPromise;
    };

    /** Petición HTTP con ticket de autenticación de Steam (reintento x3). */
    this.sendRequestWithSteamAuth = function (auth, url, payload) {
      function ensureTicket() {
        var d;
        if (authTicketPromise) {
          d = $q.defer();
          auth.listeners.push(function () { d.resolve(); });
          return d.promise;
        }
        d = $q.defer();
        auth.ticket = '';
        auth.listeners.push(function () {
          authTicketPromise = undefined;
          d.resolve();
        });
        authTicketPromise = self.callDeferred('ui', 'refreshAuthToken');
        return d.promise;
      }
      function post(deferred) {
        deferred = deferred || $q.defer();
        var query = $httpParamSerializer({ steamauthticket: auth.ticket });
        $http.post(url + '?' + query, payload).then(
          function (res) { deferred.resolve(res); },
          function (err) {
            if (!(authRetryCount < 3)) {
              deferred.reject(err);
              return;
            }
            ++authRetryCount;
            ensureTicket().then(function () { post(deferred); });
          }
        );
        return deferred.promise;
      }
      var ready = (auth.ticket && auth.ticket.length !== 0) ? $q.when() : ensureTicket();
      return ready.then(post);
    };

    /** URL de Steam Workshop para un wallpaper publicado. */
    this.getWorkshopUrl = function (wallpaper) {
      if (!wallpaper.workshopid) {
        return undefined;
      }
      return 'https://steamcommunity.com/sharedfiles/filedetails/?id=' + wallpaper.workshopid;
    };

    /** Copia la URL de Workshop al portapapeles vía host nativo. */
    var copyToastVisible = true;
    this.copyWorkshopUrl = function (wallpaper) {
      var url = self.getWorkshopUrl(wallpaper);
      if (!url) {
        return;
      }
      self.callDeferred('ui', 'copyToClipboard', url).then(function () {
        if (copyToastVisible) {
          copyToastVisible = false;
          $timeout(function () { copyToastVisible = true; }, 3000);
          $translate('ui_browse_details_toast_url_copied').then(function (msg) {
            utils.showSuccess(msg);
          });
        }
      });
    };

    // Backend público de Wallpaper Engine (observado en el bundle).
    this.wpxAPIBackendUrl = 'https://www.wallpaperengineapi.com';
    this.wpxAPISetDev = function () {
      this.wpxAPIBackendUrl = 'http://localhost:3000';
    };
  }
]);
