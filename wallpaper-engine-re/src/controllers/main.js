/**
 * Controladores principales reconstruidos (firmas + responsabilidades).
 * Nombres originales preservados (observados vía REA en scripts.js).
 * La lógica interna de cada uno vive tras el bridge `host.callEditor/callUI`.
 */
angular.module('wallpaperbrowserApp')

  /**
   * BrowseWallpapersCtrl — ruta /browsewallpapers.
   * Responsabilidades observadas: listado de wallpapers instalados/Workshop,
   * filtros, monitor múltiple, playlists, compartir, publicar a Steam.
   * Expuesto también como window.browseWallpapersCtrl por el host.
   */
  .controller('BrowseWallpapersCtrl', ['$scope', 'host', function ($scope, host) {
    window.browseWallpapersCtrl = this;
    var vm = this;
    vm.wallpapers = [];
    vm.refresh = function () {
      return host.callEditor('getProjects').then(function (projects) {
        vm.wallpapers = projects || [];
      });
    };
    vm.getWorkshopUrl = host.getWorkshopUrl;
    vm.copyWorkshopUrl = host.copyWorkshopUrl;
    vm.refresh();
  }])

  /**
   * EditorCtrl — ruta /editor (el editor de escenas).
   * Responsabilidades observadas: árbol del proyecto (k.project.tree),
   * datos (k.project.data.type), viewport, capas, efectos, partículas,
   * huesos, materiales, plantillas, publicación.
   * Expuesto también como window.editorCtrl por el host.
   */
  .controller('EditorCtrl', ['$scope', 'host', function ($scope, host) {
    window.editorCtrl = this;
    var vm = this;
    vm.project = { tree: [], data: { type: 'scene' } };
    vm.blockEditorInput = false;

    vm.openProject = function (name) {
      vm.blockEditorInput = true;
      return host.callEditor('openProject', name).then(function (project) {
        vm.blockEditorInput = false;
        vm.project = project;
      }).catch(function () {
        return host.callEditor('closeProject', true).finally(function () {
          vm.blockEditorInput = false;
        });
      });
    };
    vm.saveProject = function () {
      return host.callEditor('saveProjectData');
    };
    vm.setGridVisible = function (visible) {
      return host.callEditor('setGridVisible', !!visible);
    };
  }])

  /**
   * SettingsCtrl — ruta /settings.
   * Responsabilidades observadas: framework de vídeo (cef|native),
   * CEF devtools, ajustes del editor, apps externas, persistencia vía
   * host.callEditor('saveEditorSettings', ...).
   */
  .controller('SettingsCtrl', ['$scope', 'host', function ($scope, host) {
    var vm = this;
    vm.videoFramework = 'cef'; // valores observados: "cef" | "native"
    vm.save = function (settings) {
      return host.callEditor('saveEditorSettings', angular.toJson(settings));
    };
  }])

  /**
   * WelcomeCtrl — rutas / y /welcome.
   * Pantalla de bienvenida / primer arranque.
   */
  .controller('WelcomeCtrl', ['$scope', function ($scope) {
    var vm = this;
    vm.continue = function () { window.location.hash = '#/browsewallpapers'; };
  }]);
