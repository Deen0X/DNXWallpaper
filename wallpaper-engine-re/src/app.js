/**
 * Módulo principal `wallpaperbrowserApp` + tabla de rutas.
 *
 * RECONSTRUIDO con REA desde scripts.js:
 *   Rutas observadas (.when("...")):  /  /browsewallpapers  /editor  /settings  /welcome
 *   Servicios (.factory/.service/.provider): compiler, contextMenu, dragdrop,
 *     host, localeLoader, localeLoaderSupport, modals, skin, utils
 *   Controladores: 56  |  Directivas: 55  (inventario en evidence/rea-findings.md)
 *
 * Utilidades globales observadas (window.*):
 *   window.keyPressSanitize, window.pasteSanitize (bloquean el carácter "$"),
 *   window.toLocalUrl(path) -> "http://wpx.internal/__file/<ruta>" (protocolo
 *     interno servido por el binario nativo para exponer archivos locales),
 *   window.wpxGlobalData (datos de sesión expuestos por el host).
 */
angular.module('wallpaperbrowserApp', ['ngRoute'])
  .config(['$routeProvider', function ($routeProvider) {
    $routeProvider
      .when('/', { templateUrl: 'views/welcome.html', controller: 'WelcomeCtrl' })
      .when('/browsewallpapers', { templateUrl: 'views/browse.html', controller: 'BrowseWallpapersCtrl' })
      .when('/editor', { templateUrl: 'views/editor.html', controller: 'EditorCtrl' })
      .when('/settings', { templateUrl: 'views/settings.html', controller: 'SettingsCtrl' })
      .when('/welcome', { templateUrl: 'views/welcome.html', controller: 'WelcomeCtrl' })
      .otherwise({ redirectTo: '/' });
  }]);

/** Utilidades globales idénticas a las del bundle original. */
window.keyPressSanitize = function (e) {
  if (String.fromCharCode(e.which) !== '$') {
    return true;
  }
  e.preventDefault();
  return false;
};

window.pasteSanitize = function (e) {
  var t = e && e.clipboardData && e.clipboardData.getData('text');
  if (!t || !t.includes('$')) {
    return true;
  }
  e.preventDefault();
  return false;
};

/**
 * Convierte una ruta local de wallpaper en URL servida por el host nativo.
 * Protocolo interno: http://wpx.internal/__file/<ruta-url-encoded>
 */
window.toLocalUrl = function (path) {
  if (!angular.isString(path) || path.length === 0 || path.startsWith('http')) {
    return path;
  }
  return 'http://wpx.internal/__file/' + encodeURIComponent(path.replace(/\\/g, '/'));
};

angular.isUndefinedOrNull = function (v) {
  return angular.isUndefined(v) || v === null;
};
