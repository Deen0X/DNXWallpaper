// DNXWallpaper v3 — estructura de interfaz (prototipo).
// Datos: servidor LAN de la app (/api/library, /api/thumb).

const state = {
  items: [],
  offset: 0,
  source: 'all',
  query: '',
  ftype: '',
  forient: '',
  fminh: 0,
  loading: false,
  picked: {},        // sección -> item elegido
  playlist: [],
  pickerPage: 0,
  pickerCb: null,
  pickerSource: 'all',
  pickerQuery: '',
  pickerType: '',
  pickerOrient: '',
  // Estado del selector: se conserva al cerrarlo y reabrirlo (filtro, búsqueda,
  // resultados y posición del scroll).
  pickerFilter: 'all',
  pickerPlugin: '',
  pickerItems: [],
  pickerLiveQuery: '',
  pickerLoaded: false,
  pickerScroll: 0
};

const $ = (s) => document.querySelector(s);
const $$ = (s) => Array.from(document.querySelectorAll(s));

/** Agrupa llamadas seguidas (búsqueda mientras se escribe). */
function debounce(fn, ms) {
  let t = 0;
  return function () {
    const args = arguments;
    clearTimeout(t);
    t = setTimeout(() => fn.apply(null, args), ms);
  };
}

// Wallpaper activo (clave del catálogo y marca de tiempo para refrescar).
let currentKey = '';
// Temporizador de la barra de descarga del visor (se cancela al abrir otro).
let viewerBarTimer = 0;
let currentStamp = Date.now();

function api(path) {
  return fetch(path).then((r) => (r.ok ? r.json() : Promise.reject(r.status)));
}

function toast(msg) {
  const el = $('#toast');
  el.textContent = msg;
  el.hidden = false;
  clearTimeout(toast._t);
  toast._t = setTimeout(() => (el.hidden = true), 2400);
}

function thumbUrl(it) {
  return it.thumb || (it.previewsmall ? it.previewsmall : '');
}

// El catálogo del host usa "preview"/"previewsmall" con wpx.internal; se
// traduce a la ruta pública /api/thumb/<key>.
function keyOf(it) {
  return it.workshopid || (it.file || '').replace(/^wpxr:/, '');
}

function cardHtml(it) {
  const key = keyOf(it);
  const v = (it.type || '').toLowerCase().includes('video');
  const src = `/api/thumb/${encodeURIComponent(key)}`;
  const title = (it.title || key).replace(/  \[Animado\]/, '');
  // Las miniaturas se generan bajo demanda y con cola: se reintenta con
  // espera creciente antes de darla por perdida (antes se rendía al segundo).
  const retry = `this.dataset.r=((+this.dataset.r||0)+1);var n=+this.dataset.r;` +
                `if(n<12){var d=Math.min(8000,900+n*700);setTimeout(function(){im.src='${src}?r='+Date.now()},d)}` +
                `else{im.classList.add('broken');im.parentNode.classList.add('failed')}`;
  const onerr = `var im=this;im.parentNode.classList.remove('loaded');${retry}`;
  // Mientras no hay miniatura se ve el icono del programa con un pulso.
  const onload = `this.parentNode.classList.add('loaded');` +
                 `this.parentNode.classList.remove('failed')`;
  return `<div class="thumb" data-key="${encodeURIComponent(key)}" data-title="${title.replace(/"/g, '&quot;')}" data-type="${it.type || ''}">
    <span class="ph" aria-hidden="true"></span>
    <img class="shot" loading="lazy" src="${src}" alt="${title.replace(/"/g, '&quot;')}"
         onload="${onload}" onerror="${onerr}">
    ${v ? '<span class="v">▶</span>' : ''}
    <span class="t">${title}</span>
  </div>`;
}

// Prefetch de miniaturas visibles. El servidor tiene una cola por proveedor
// (2 workers por plugin, 12 en total) y prioriza lo que el usuario ve.
const thumbPrefetchPending = new Set();
let thumbPrefetchTimer = 0;
const thumbObservers = new WeakMap();

function thumbKeyOf(el) {
  const raw = el && el.dataset ? (el.dataset.key || '') : '';
  try { return decodeURIComponent(raw); } catch (e) { return raw; }
}

function flushThumbPrefetch() {
  thumbPrefetchTimer = 0;
  if (!thumbPrefetchPending.size) return;
  const keys = Array.from(thumbPrefetchPending).slice(0, 200);
  keys.forEach((k) => thumbPrefetchPending.delete(k));
  fetch('/api/thumb/prefetch', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ keys })
  }).catch(() => {});
  if (thumbPrefetchPending.size) {
    thumbPrefetchTimer = setTimeout(flushThumbPrefetch, 80);
  }
}

function queueThumbPrefetch(key) {
  if (!key) return;
  thumbPrefetchPending.add(key);
  if (!thumbPrefetchTimer) thumbPrefetchTimer = setTimeout(flushThumbPrefetch, 120);
}

function setupThumbPrefetch(rootSel, immediate) {
  const root = typeof rootSel === 'string' ? document.querySelector(rootSel) : rootSel;
  if (!root) return;
  const cards = Array.from(root.querySelectorAll('.thumb'));
  const first = typeof immediate === 'number' ? immediate : 20;
  cards.slice(0, first).forEach((el) => queueThumbPrefetch(thumbKeyOf(el)));
  if (!('IntersectionObserver' in window)) return;
  const old = thumbObservers.get(root);
  if (old) old.disconnect();
  const obs = new IntersectionObserver((entries) => {
    entries.forEach((entry) => {
      if (!entry.isIntersecting) return;
      obs.unobserve(entry.target);
      queueThumbPrefetch(thumbKeyOf(entry.target));
    });
  }, { rootMargin: '240px 0px' });
  thumbObservers.set(root, obs);
  cards.slice(first).forEach((el) => obs.observe(el));
}

// ------------------------------------------------------------ catálogo
function providerOf(it) {
  return (it.provider || '').toString();
}

function loadCatalog(reset) {
  if (state.loading) return;
  state.loading = true;
  if (reset) {
    state.items = [];
    state.offset = 0;
    $('#catalog').innerHTML = '';
  }
  // Los filtros se aplican en el servidor: así la búsqueda recorre todo el
  // catálogo y no sólo las páginas ya cargadas.
  const p = new URLSearchParams({
    offset: String(state.offset), limit: '60'
  });
  if (state.query) p.set('q', state.query);
  if (state.source && state.source !== 'all') p.set('provider', state.source);
  if (state.ftype) p.set('type', state.ftype);
  if (state.forient) p.set('orient', state.forient);
  if (state.fminh) p.set('minh', String(state.fminh));
  return api('/api/library?' + p.toString())
    .then((list) => {
      state.items = state.items.concat(list);
      state.offset += list.length;
      state.providers = Array.from(new Set(
        (state.providers || []).concat(list.map(providerOf))));
      renderCatalog();
      $('#more').style.display = list.length < 60 ? 'none' : '';
      renderSources();
    })
    .catch(() => toast('No se pudo leer el catálogo'))
    .finally(() => (state.loading = false));
}

function visibleItems() {
  // El servidor ya ha filtrado; aquí sólo se pinta lo recibido.
  return state.items;
}

// --- barajar ---------------------------------------------------------------
// El barajado del servidor pide una página nueva a cada plugin: tarda ~20-25 s
// (los plugins son procesos CLI). Para que el botón no parezca muerto, primero
// se reordena al instante lo que ya está cargado y después se sustituye por la
// página nueva, mostrando el progreso en el propio botón.
function shuffleCopy(arr) {
  const a = arr.slice();
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    const t = a[i]; a[i] = a[j]; a[j] = t;
  }
  return a;
}

function barajar(btn, actuales, aplicar) {
  const lista = actuales() || [];
  if (lista.length > 1) aplicar(shuffleCopy(lista));   // cambio inmediato
  const label = btn ? btn.textContent : '';
  let punto = 0;
  let timer = 0;
  if (btn) {
    btn.disabled = true;
    btn.textContent = 'Barajando';
    timer = setInterval(() => {
      punto = (punto + 1) % 4;
      btn.textContent = 'Barajando' + '.'.repeat(punto);
    }, 350);
  }
  toast('Barajando en los plugins (tarda unos segundos)…');
  api('/api/library/randomize')
    .then((list) => {
      if (!Array.isArray(list) || !list.length) {
        toast('Los plugins no han devuelto nada');
        return;
      }
      aplicar(list);
      toast(list.length + ' wallpapers al azar');
    })
    .catch(() => toast('No se pudo barajar'))
    .finally(() => {
      if (timer) clearInterval(timer);
      if (btn) {
        btn.disabled = false;
        btn.textContent = label;
      }
    });
}

// --- búsqueda en vivo -------------------------------------------------------
// Al pulsar Enter: primero lo local (instantáneo) y, debajo, los plugins por
// separado. Cada plugin responde cuando puede y se van añadiendo resultados.
// Si el término cambia, `seq` invalida las respuestas que aún lleguen.
const live = { seq: 0, term: '', pending: 0, total: 0, items: [] };

function cancelLive() {
  live.seq++;
  live.term = '';
  live.pending = 0;
  live.total = 0;
  live.items = [];
  state.liveQuery = '';
  if ($('#liveInfo')) $('#liveInfo').textContent = '';
  if ($('#liveBack')) $('#liveBack').hidden = true;
}

function searchPlugins(term) {
  const q = (term || '').trim();
  if (!q) return;
  cancelLive();
  const seq = live.seq;
  live.term = q;
  state.liveQuery = q;
  state.query = q;
  if ($('#liveBack')) $('#liveBack').hidden = false;
  const info = $('#liveInfo');
  if (info) info.textContent = 'Buscando «' + q + '» en los plugins…';
  // 1) resultados locales ya mismo
  loadCatalog(true);
  renderCatalog();
  // 2) una consulta por plugin, en paralelo
  api('/api/plugins').then((list) => {
    if (seq !== live.seq) return;
    const ids = (Array.isArray(list) ? list : [])
      .filter((p) => p.enabled !== false)
      .map((p) => p.id)
      .filter((id) => !state.source || state.source === 'all' || id === state.source);
    live.total = ids.length;
    live.pending = ids.length;
    renderCatalog();
    ids.forEach((id) => {
      const p = new URLSearchParams({ q, provider: id, limit: '10' });
      api('/api/search?' + p.toString()).then((items) => {
        if (seq !== live.seq) return;
        (items || []).forEach((it) => {
          if (!live.items.some((x) => x.workshopid === it.workshopid)) {
            live.items.push(it);
          }
        });
        live.pending--;
        if (info) {
          info.textContent = 'Plugins: ' + live.items.length +
            (live.pending > 0 ? ' · buscando…' : ' resultados');
        }
        renderCatalog();
      }).catch(() => {
        if (seq !== live.seq) return;
        live.pending--;
        renderCatalog();
      });
    });
  }).catch(() => {
    if (seq !== live.seq) return;
    live.pending = 0;
    renderCatalog();
  });
}

function leaveLiveSearch() {
  cancelLive();
  loadCatalog(true);
}

function renderCatalog() {
  const list = visibleItems();
  state.byKey = {};
  list.forEach((it) => { state.byKey[it.workshopid] = it; });
  let html = list.slice(0, 400).map(cardHtml).join('');
  // Sección de plugins (búsqueda en vivo), debajo de lo local.
  if (state.liveQuery && state.liveQuery === live.term) {
    live.items.forEach((it) => { state.byKey[it.workshopid] = it; });
    const spin = live.pending > 0 ? '<span class="spinner"></span>' : '';
    let txt;
    if (live.pending > 0) {
      txt = 'Buscando en los plugins… (' + (live.total - live.pending) + '/' +
            live.total + ')';
    } else if (live.items.length) {
      txt = 'Plugins: ' + live.items.length + ' resultados';
    } else {
      txt = 'Los plugins no han devuelto resultados';
    }
    html += '<div class="grid-sep">' + spin + '<span>' + txt +
            ' · «' + state.liveQuery + '»</span></div>';
    html += live.items.map(cardHtml).join('');
  }
  $('#catalog').innerHTML = html;
  setupThumbPrefetch('#catalog', 20);
  if ($('#catCount')) {
    const filtrado = !!(state.query || state.ftype || state.forient || state.fminh ||
                        (state.source && state.source !== 'all'));
    $('#catCount').textContent = filtrado
      ? `${list.length} resultados cargados`
      : `${list.length} cargados de ${state.total || '?'} en el servidor`;
  }
  $$('#catalog .thumb').forEach((el) => {
    el.onclick = () => selectFromMain(el);
  });
}

function renderSources() {
  const providers = (state.providers || []).slice().sort();
  const el = $('#sources');
  if (!el) return;
  const label = (id) => (state.providerNames && state.providerNames[id]) || id;
  const all = `<button class="chip ${state.source === 'all' ? 'active' : ''}" data-src="all">Todo</button>`;
  el.innerHTML = all + providers.map((p) =>
    `<button class="chip ${state.source === p ? 'active' : ''}" data-src="${p}">${label(p)}</button>`
  ).join('');
  $$('#sources .chip').forEach((b) => {
    b.onclick = () => {
      state.source = b.dataset.src;
      renderSources();
      loadCatalog(true);
    };
  });
}

function selectFromMain(el) {
  // El catálogo es un visor: al pulsar se abre el wallpaper en grande.
  const key = decodeURIComponent(el.dataset.key);
  openViewer(key);
}

// ------------------------------------------------------------ visor
function openViewer(key) {
  const it = (state.byKey && state.byKey[key]) || { title: key };
  const media = it.mediaType === 'image' || (it.type || '') === 'Image';
  const src = `/dl/${encodeURIComponent(key)}`;
  $('#viewerTitle').textContent = (it.title || key).replace(/  \[Animado\]$/, '');
  // CEF no reproduce H.264 (la app va sin códecs propietarios), así que en el
  // visor se usa el WebP animado que ya genera el servidor. Antes se usaba un
  // <video> y todos los MP4 fallaban con "No se pudo cargar".
  const enc = encodeURIComponent(key);
  clearTimeout(viewerBarTimer);
  const dlBar = document.getElementById('viewerDl');
  if (dlBar) dlBar.hidden = true;
  if (media) {
    $('#viewerMedia').innerHTML = `<img id="viewerShot" src="${src}" alt="">`;
    const im = document.getElementById('viewerShot');
    im.addEventListener('error', () => {
      im.src = '/api/thumb/' + enc + '?v=1';
    }, { once: true });
  } else {
    // Miniatura al instante y, encima, aviso de carga; cuando el WebP animado
    // está listo se cambia sin que el usuario vea un hueco negro.
    $('#viewerMedia').innerHTML =
      `<img id="viewerShot" src="/api/thumb/${enc}?v=1" alt="">
       <div class="viewer-wait" id="viewerWait">Cargando vista previa…</div>`;
    const img = document.getElementById('viewerShot');
    const bar = document.getElementById('viewerDl');
    let intentos = 0;
    // La barra aparece sólo si de verdad hay espera (450 ms): si la vista
    // previa ya está cacheada no se ve ni un parpadeo.
    const barTimer = setTimeout(() => { if (bar) bar.hidden = false; }, 450);
    viewerBarTimer = barTimer;
    const ocultarBarra = () => {
      clearTimeout(barTimer);
      if (bar) bar.hidden = true;
    };

    const pedirPreview = () => {
      fetch('/api/preview/' + enc)
        .then((r) => (r.ok ? r.blob() : Promise.reject(new Error('preview'))))
        .then((blob) => {
          const url = URL.createObjectURL(blob);
          // Se precarga en una imagen aparte: el <img> visible ya está mostrando
          // la miniatura y su "load" ocultaba la barra antes de tener el preview.
          const pv = new Image();
          pv.onload = () => {
            img.src = url;
            const w = document.getElementById('viewerWait');
            if (w) w.remove();
            ocultarBarra();
            setTimeout(() => URL.revokeObjectURL(url), 3000);
          };
          pv.onerror = () => URL.revokeObjectURL(url);
          pv.src = url;
        })
        .catch(() => {
          intentos++;
          const w = document.getElementById('viewerWait');
          if (w) w.textContent = 'Preparando vista previa…';
          if (intentos < 20) setTimeout(pedirPreview, 3000);
          else {
            ocultarBarra();
            if (w) w.textContent = 'Sin vista previa (usa la miniatura)';
          }
        });
    };
    pedirPreview();
  }

  const res = it.resolution;
  const bits = [];
  bits.push(`<span>${media ? 'Imagen' : 'Vídeo'}</span>`);
  if (res && res.width) bits.push(`<span><b>${res.width} × ${res.height}</b></span>`);
  if (it.provider) bits.push(`<span>Plugin: <b>${it.provider}</b></span>`);
  if (it.sizeBytes) bits.push(`<span>${humanBytes(it.sizeBytes)}</span>`);
  $('#viewerMeta').innerHTML = bits.join('');

  const isMosaic = key.indexOf('mosaics:') === 0;
  $('#viewerActions').innerHTML = `
    <button class="btn" id="viewerToDisplay">Usar en una pantalla…</button>
    ${isMosaic ? '<button class="btn ghost" id="viewerEditMosaic">Abrir en el editor de mosaicos</button>' : ''}
    <a class="btn ghost" href="/editor/?file=${encodeURIComponent(key)}"
       target="_blank" rel="noopener">Adaptar a dispositivo</a>
  `;
  if (isMosaic && $('#viewerEditMosaic')) {
    const btn = $('#viewerEditMosaic');
    btn.disabled = true;
    btn.textContent = 'Comprobando composición…';
    fetch('/api/mosaic/project?key=' + encodeURIComponent(key))
      .then((r) => (r.ok ? r.json() : null))
      .then((p) => {
        if (!p || !p.cells) {
          btn.disabled = true;
          btn.textContent = 'Sin composición guardada';
          btn.title = 'Este mosaico se generó antes de guardar la composición; no se puede reabrir.';
          return;
        }
        btn.disabled = false;
        btn.textContent = 'Abrir en el editor de mosaicos';
        btn.title = '';
        btn.onclick = () => {
          $('#viewer').hidden = true;
          if (typeof window.mosaicLoadProject === 'function') {
            window.mosaicLoadProject(p);
          }
        };
      })
      .catch(() => {
        btn.disabled = true;
        btn.textContent = 'Sin composición guardada';
      });
  }
  $('#viewerToDisplay').onclick = () => {
    $('#viewer').hidden = true;
    openPicker('Wallpaper para la pantalla', (item) => setPending(item, 0));
  };
  $('#viewer').hidden = false;
}

function humanBytes(n) {
  if (!n) return '';
  const u = ['B', 'KB', 'MB', 'GB'];
  let i = 0;
  let v = n;
  while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
  return v.toFixed(v >= 10 || i === 0 ? 0 : 1) + ' ' + u[i];
}

// ------------------------------------------------------------ selector
// El selector se reabre tal y como se cerró: mismo filtro, mismo texto y los
// mismos resultados (sin volver a pedirlos al servidor ni perder el scroll).
function openPicker(title, cb) {
  state.pickerCb = cb;
  $('#pickerTitle').textContent = title;
  $('#pickerSearch').value = state.pickerQuery;
  $$('#pickerFilters .chip').forEach((b) =>
    b.classList.toggle('active', b.dataset.pf === state.pickerFilter));
  if ($('#pickerPlugin')) $('#pickerPlugin').value = state.pickerPlugin;
  $('#picker').hidden = false;
  if (!state.pickerLoaded) {
    loadPicker(true);
  } else {
    renderPicker();          // repinta sin pedir nada: mismos resultados
    restaurarScrollPicker();
  }
}

function restaurarScrollPicker() {
  const panel = $('#picker .modal-panel');
  if (!panel) return;
  const y = state.pickerScroll || 0;
  panel.scrollTop = y;
  // La rejilla acaba de repintarse; se reaplica tras el primer recálculo.
  setTimeout(() => { panel.scrollTop = y; }, 0);
}

function closePicker() {
  const panel = $('#picker .modal-panel');
  if (panel) state.pickerScroll = panel.scrollTop;
  $('#picker').hidden = true;
}

function pickerVisible() {
  // El servidor ya aplica los filtros del selector.
  return state.pickerItems || [];
}

// Búsqueda en vivo del selector: misma idea que en el catálogo.
const plive = { seq: 0, term: '', pending: 0, total: 0, items: [] };

function cancelPickerLive() {
  plive.seq++;
  plive.term = '';
  plive.pending = 0;
  plive.total = 0;
  plive.items = [];
  state.pickerLiveQuery = '';
}

function startPickerLive(term) {
  const q = (term || '').trim();
  if (!q) return;
  cancelPickerLive();
  const seq = plive.seq;
  plive.term = q;
  state.pickerLiveQuery = q;
  state.pickerQuery = q;
  // 1) lo local, ya
  loadPicker(true);
  renderPicker();
  // 2) una consulta por plugin
  api('/api/plugins').then((list) => {
    if (seq !== plive.seq) return;
    const ids = (Array.isArray(list) ? list : [])
      .filter((p) => p.enabled !== false)
      .map((p) => p.id)
      .filter((id) => !state.pickerPlugin || id === state.pickerPlugin);
    plive.total = ids.length;
    plive.pending = ids.length;
    renderPicker();
    ids.forEach((id) => {
      const p = new URLSearchParams({ q, provider: id, limit: '10' });
      api('/api/search?' + p.toString()).then((items) => {
        if (seq !== plive.seq) return;
        (items || []).forEach((it) => {
          if (!plive.items.some((x) => x.workshopid === it.workshopid)) {
            plive.items.push(it);
          }
        });
        plive.pending--;
        renderPicker();
      }).catch(() => {
        if (seq !== plive.seq) return;
        plive.pending--;
        renderPicker();
      });
    });
  }).catch(() => {
    if (seq !== plive.seq) return;
    plive.pending = 0;
    renderPicker();
  });
}

function renderPicker() {
  const list = pickerVisible();
  const grid = $('#pickerGrid');
  let html = list.slice(0, 400).map(cardHtml).join('');
  if (state.pickerLiveQuery && state.pickerLiveQuery === plive.term) {
    state.byKey = state.byKey || {};
    plive.items.forEach((it) => { state.byKey[it.workshopid] = it; });
    const spin = plive.pending > 0 ? '<span class="spinner"></span>' : '';
    let txt;
    if (plive.pending > 0) {
      txt = 'Buscando en los plugins… (' + (plive.total - plive.pending) + '/' +
            plive.total + ')';
    } else if (plive.items.length) {
      txt = 'Plugins: ' + plive.items.length + ' resultados';
    } else {
      txt = 'Los plugins no han devuelto resultados';
    }
    html += '<div class="grid-sep">' + spin + '<span>' + txt +
            ' · «' + state.pickerLiveQuery + '»</span></div>';
    html += plive.items.map(cardHtml).join('');
  }
  grid.innerHTML = html;
  setupThumbPrefetch('#pickerGrid', 20);
  $$('#pickerGrid .thumb').forEach((el) => {
    el.onclick = () => {
      const key = decodeURIComponent(el.dataset.key);
      const item = { key, title: el.dataset.title, type: el.dataset.type || '' };
      closePicker();
      if (state.pickerCb) state.pickerCb(item);
    };
  });
  if (!list.length && !state.pickerLiveQuery) {
    grid.innerHTML = '<p class="muted" style="font-size:12px">Sin resultados con estos filtros.</p>';
  }
}

function loadPicker(reset) {
  if (reset) {
    state.pickerPage = 0;
    state.pickerItems = [];
    state.pickerScroll = 0;
    $('#pickerGrid').innerHTML = '';
  }
  const offset = state.pickerPage * 60;
  const p = new URLSearchParams({ offset: String(offset), limit: '60' });
  if (state.pickerQuery) p.set('q', state.pickerQuery);
  if (state.pickerPlugin) p.set('provider', state.pickerPlugin);
  if (state.pickerFilter === 'video') p.set('type', 'video');
  if (state.pickerFilter === 'image') p.set('type', 'image');
  if (state.pickerFilter === 'portrait') p.set('orient', 'portrait');
  api('/api/library?' + p.toString()).then((list) => {
    state.pickerLoaded = true;
    if (!list.length) {
      $('#pickerMore').style.display = 'none';
      renderPicker();
      return;
    }
    state.pickerPage++;
    state.pickerItems = state.pickerItems.concat(list);
    renderPicker();
    $('#pickerMore').style.display = list.length < 60 ? 'none' : '';
  }).catch(() => {
    state.pickerLoaded = true;
    renderPicker();
  });
}

// ------------------------------------------------------------ pantallas
function baseName(p) {
  if (!p) return '';
  const s = String(p).replace(/\\/g, '/');
  const i = s.lastIndexOf('/');
  return decodeURIComponent(s.slice(i + 1));
}

/** Fondo del monitor: vídeo animado si se puede, o captura estática. */
function shotHtml(m, minX, minY, totalW, totalH, cur) {
  if (!cur || !currentKey) return '';
  if (settings.displays.mode === 'none') return '';
  if ((settings.displays.none || []).includes(Number(m.index))) return '';
  const isVideo = cur.mediaType === 'video' && settings.displays.animate;
  const src = isVideo
    ? `/dl/${encodeURIComponent(currentKey)}`
    : `/api/current/thumb?v=${currentStamp}`;
  const tag = isVideo ? 'video' : 'img';

  // Ajuste dentro del monitor. Cada opción hace algo distinto (antes varias
  // caían en "cover" y parecían iguales):
  //   expand -> cubre y recorta
  //   zoom   -> cubre y amplía un 15 % extra
  //   fill   -> estira (deforma)
  //   center -> tamaño original, centrada
  const fitCss = {
    expand: 'cover',
    fill: 'fill',
    center: 'none'
  }[settings.displays.fit] || 'cover';

  const style = [`object-fit:${fitCss}`];

  // Expandir: cada monitor enseña su trozo del wallpaper completo.
  if (settings.displays.mode === 'expand' && totalW > 0 && totalH > 0) {
    const kx = totalW / Math.max(1, m.width);
    const ky = totalH / Math.max(1, m.height);
    const left = -(m.x - minX) / Math.max(1, m.width) * 100;
    const top = -(m.y - minY) / Math.max(1, m.height) * 100;
    style.push(`left:${left}%`, `top:${top}%`,
               `width:${kx * 100}%`, `height:${ky * 100}%`);
  }

  const extra = tag === 'video'
    ? ' muted loop autoplay playsinline preload="metadata"'
    : ' alt=""';
  return `<${tag} class="m-shot" src="${src}" style="${style.join(';')}"${extra}></${tag}>`;
}

function removeFromMonitor(index) {
  const none = new Set(settings.displays.none || []);
  none.add(Number(index));
  settings.displays.none = Array.from(none);
  saveSettings();
  loadDisplays();
  toast('Sin wallpaper en esa pantalla');
}

// ------------------------------------------------------------ aplicar
// Elegir un wallpaper deja una propuesta pendiente; nada se aplica hasta
// pulsar "Aplicar" (así se puede cambiar de idea o ajustar antes). La
// asignación la resuelve el editor de escritorio (assignToSlot), que marca los
// cambios y deja el único botón Aplicar habilitado.
function setPending(item, monitor) {
  state.pending = { item, monitor: Number(monitor) || 0 };
  if (typeof assignToSlot === 'function') {
    assignToSlot(item, Number(monitor) || 0);
  } else {
    $('#applyText').textContent =
      `«${item.title || item.key}» seleccionado. Pulsa Aplicar para usarlo.`;
  }
}

function clearPending() {
  state.pending = null;
}

function applyPending() {
  if (!state.pending) return;
  applyItem(state.pending.item, state.pending.monitor);
  clearPending();
}

// ------------------------------------------------------------ optimizador
const opt = { key: '', scale: 1, x: 0, y: 0, dragging: false, lastX: 0, lastY: 0 };

function openOptimize(monitorIndex) {
  // Se optimiza lo que está propuesto; si no hay nada pendiente, el actual.
  opt.key = (state.pending && state.pending.item.key) || currentKey;
  const mi = state.pending ? state.pending.monitor : 0;
  opt.scale = 1;
  opt.x = 0;
  opt.y = 0;
  const v = $('#optVideo');
  v.src = `/dl/${encodeURIComponent(opt.key)}`;
  $('#optTitle').textContent = 'Optimizar para la pantalla';
  $('#optStatus').textContent = '';
  $('#optimize').hidden = false;
  const m = state.monitors && state.monitors[Number(mi)];
  opt.monitor = m || { width: 1920, height: 1080 };
  // La caja del recorte tiene la proporción del MONITOR: así lo que ves es
  // exactamente lo que se verá en la pantalla.
  const box = $('#optBox');
  const ar = opt.monitor.width / opt.monitor.height;
  const availW = (box.parentElement ? box.parentElement.clientWidth : 900) - 40;
  const maxH = Math.round(window.innerHeight * 0.46);
  let bw = Math.max(320, availW);
  let bh = bw / ar;
  if (bh > maxH) {
    bh = maxH;
    bw = Math.round(bh * ar);
  }
  box.style.width = Math.round(bw) + 'px';
  box.style.height = Math.round(bh) + 'px';
  box.style.margin = '0 auto';
  v.onloadedmetadata = () => {
    layoutOpt();
    $('#optInfo').textContent =
      `${v.videoWidth}×${v.videoHeight} → ${opt.monitor.width}×${opt.monitor.height}`;
  };
}

/** Coloca el vídeo para cubrir la caja con el zoom/desplazamiento actual. */
function layoutOpt() {
  const box = $('#optBox');
  const v = $('#optVideo');
  if (!v.videoWidth) return;
  const bw = box.clientWidth;
  const bh = box.clientHeight;
  const s0 = Math.max(bw / v.videoWidth, bh / v.videoHeight);
  const s = s0 * opt.scale;
  const w = v.videoWidth * s;
  const h = v.videoHeight * s;
  // Límites: el vídeo siempre cubre la caja.
  opt.x = Math.min(0, Math.max(bw - w, opt.x));
  opt.y = Math.min(0, Math.max(bh - h, opt.y));
  v.style.width = w + 'px';
  v.style.height = h + 'px';
  v.style.transform = `translate(${opt.x}px, ${opt.y}px)`;
}

/** Región visible en coordenadas normalizadas del vídeo (0..1). */
function optRect() {
  const box = $('#optBox');
  const v = $('#optVideo');
  const bw = box.clientWidth;
  const bh = box.clientHeight;
  const s0 = Math.max(bw / v.videoWidth, bh / v.videoHeight);
  const s = s0 * opt.scale;
  return {
    x: (-opt.x / s) / v.videoWidth,
    y: (-opt.y / s) / v.videoHeight,
    w: (bw / s) / v.videoWidth,
    h: (bh / s) / v.videoHeight
  };
}

function bindOptimizer() {
  const box = $('#optBox');
  if (!box) return;
  box.addEventListener('mousedown', (e) => {
    opt.dragging = true;
    opt.lastX = e.clientX;
    opt.lastY = e.clientY;
    box.classList.add('dragging');
  });
  window.addEventListener('mousemove', (e) => {
    if (!opt.dragging) return;
    opt.x += e.clientX - opt.lastX;
    opt.y += e.clientY - opt.lastY;
    opt.lastX = e.clientX;
    opt.lastY = e.clientY;
    layoutOpt();
  });
  window.addEventListener('mouseup', () => {
    opt.dragging = false;
    if (box) box.classList.remove('dragging');
  });
  box.addEventListener('wheel', (e) => {
    e.preventDefault();
    const before = optRect();
    opt.scale = Math.min(3, Math.max(1, opt.scale + (e.deltaY < 0 ? 0.1 : -0.1)));
    layoutOpt();
    // Mantener centrado el punto bajo el cursor de forma aproximada.
    void before;
  }, { passive: false });
  $('#optReset').onclick = () => {
    opt.scale = 1;
    opt.x = 0;
    opt.y = 0;
    layoutOpt();
  };
  $('#optFade').oninput = (e) => {
    $('#optFadeVal').textContent = Number(e.target.value).toFixed(1).replace('.', ',') + ' s';
  };
  $('#optClose').onclick = () => {
    const v = $('#optVideo');
    v.pause();
    v.removeAttribute('src');
    $('#optimize').hidden = true;
  };
  $('#optRun').onclick = runOptimize;
}

async function runOptimize() {
  const r = optRect();
  const fade = Number($('#optFade').value) || 0;
  const vh = $('#optVideo').videoHeight || 1080;
  // Nunca se reescala hacia arriba: se toma el menor entre pantalla y origen.
  const target = Math.min(1440, Math.max(360,
    Math.min(opt.monitor.height || 1080, vh)));
  // Tope de peso: el recorte no debe pesar más que la parte del original que
  // se conserva (si no, "optimizar" engordaría el fichero).
  const area = Math.max(0.05, r.w * r.h);
  const srcItem = (state.byKey && state.byKey[opt.key]) ||
                  (state.pickerItems || []).find((x) => x.workshopid === opt.key);
  const srcMB = srcItem && srcItem.sizeBytes ? srcItem.sizeBytes / 1048576 : 0;
  const capMB = srcMB > 0
    ? Math.min(16, Math.max(1.5, srcMB * area * 1.2))
    : 16;
  $('#optRun').disabled = true;
  $('#optStatus').textContent = 'Generando…';
  try {
    const body = {
      key: opt.key,
      title: 'Optimizado',
      rect: r,
      target,
      quality: 0,
      fade,
      maxSizeMB: capMB,
      padColor: '#000000',
      proxy: true
    };
    const res = await fetch('/api/editor/export', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    }).then((x) => x.json());
    if (!res.ok || !res.id) throw new Error(res.error || 'no se pudo iniciar');
    const id = res.id;
    for (let i = 0; i < 240; i++) {
      await new Promise((r2) => setTimeout(r2, 700));
      const st = await fetch('/api/editor/status/' + id).then((x) => x.json());
      if (st.state === 'error') throw new Error(st.error || 'error');
      if (st.state === 'done') break;
      const pct = st.percent || 0;
      $('#optStatus').textContent = 'Generando… ' + pct + ' %';
    }
    $('#optStatus').textContent = 'Aplicando…';
    const applied = await fetch('/api/optimize/apply?id=' + encodeURIComponent(id),
                                 { method: 'POST' }).then((x) => x.json());
    if (!applied.ok) throw new Error(applied.error || 'no se pudo aplicar');
    $('#optStatus').textContent = 'Listo';
    toast('Wallpaper optimizado y aplicado');
    setTimeout(() => {
      $('#optClose').click();
      loadCatalog(true);
      loadDisplays();
    }, 900);
  } catch (e) {
    $('#optStatus').textContent = 'Error: ' + e;
  } finally {
    $('#optRun').disabled = false;
  }
}

function loadDisplays() {
  Promise.all([api('/api/monitors').catch(() => ({ monitors: [] })), api('/api/current').catch(() => ({}))])
    .then(([mons, cur]) => {
      const current = baseName(cur.project || '');
      currentKey = cur.key || '';
      currentStamp = Date.now();
      const list = (mons.monitors || []).slice().sort((a, b) => a.x - b.x || a.y - b.y);
      state.monitors = list;
      const map = $('#monitorMap');
      const legend = $('#monitorLegend');

      if (!list.length) {
        map.innerHTML = '';
        legend.innerHTML = '<span>No se han detectado monitores.</span>';
        return;
      }

      // Caja que engloba todos los monitores (soporta disposiciones en L,
      // verticales, separaciones...).
      const minX = Math.min(...list.map((m) => m.x));
      const minY = Math.min(...list.map((m) => m.y));
      const maxX = Math.max(...list.map((m) => m.x + m.width));
      const maxY = Math.max(...list.map((m) => m.y + m.height));
      const totalW = Math.max(1, maxX - minX);
      const totalH = Math.max(1, maxY - minY);

      // La altura del mapa se limita a 320 px; el ancho lo pone el contenedor.
      const maxH = 320;
      const boxW = map.clientWidth || 640;
      const scale = Math.min(boxW / totalW, maxH / totalH);
      map.style.height = Math.round(totalH * scale) + 'px';
      // Centrar el conjunto en el contenedor.
      const offsetX = Math.max(0, (boxW - totalW * scale) / 2);

      map.innerHTML = list.map((m) => {
        const left = (offsetX + (m.x - minX) * scale) + 'px';
        const top = ((m.y - minY) * scale) + 'px';
        const w = (m.width * scale) + 'px';
        const h = (m.height * scale) + 'px';
        const name = m.primary ? 'Pantalla principal' : 'Pantalla ' + (m.index + 1);
        const off = settings.displays.mode === 'none' ||
                    (settings.displays.none || []).includes(Number(m.index));
        return `<div class="monitor ${m.primary ? 'primary' : ''} ${off ? 'off' : ''}"
                     data-mon="${m.index}" data-name="${name}"
                     style="left:${left};top:${top};width:${w};height:${h}">
          ${shotHtml(m, minX, minY, totalW, totalH, cur)}
          ${m.primary ? '<span class="m-badge">principal</span>' : ''}
          ${!off && current ? `<button class="m-del" data-del="${m.index}"
              title="Quitar wallpaper de esta pantalla">✕</button>` : ''}
          <span class="m-name">${name}</span>
          <span class="m-res">${off ? 'sin wallpaper' : `${m.width} × ${m.height}`}</span>
          ${!off && current ? `<span class="m-cur">${current}</span>` : ''}
        </div>`;
      }).join('');

      legend.innerHTML = list.map((m) =>
        `<span>${m.primary ? 'Principal' : 'Pantalla ' + (m.index + 1)}:
         <b>${m.width}×${m.height}</b></span>`).join('') +
        (current ? `<span>Ahora: <b>${current}</b></span>` : '');

      $$('#monitorMap .monitor').forEach((el) => {
        el.onclick = () => openPicker(
          'Wallpaper para ' + (el.dataset.name || 'la pantalla'),
          (item) => {
            // Asignar a un monitor quita el "sin wallpaper" de ese monitor.
            const idx = Number(el.dataset.mon);
            settings.displays.none =
              (settings.displays.none || []).filter((i) => i !== idx);
            if (settings.displays.mode === 'none') settings.displays.mode = 'individual';
            saveSettings();
            // No se aplica todavía: queda como propuesta hasta "Aplicar".
            setPending(item, idx);
          }
        );
      });
      $$('#monitorMap .m-del').forEach((b) => {
        b.onclick = (ev) => {
          ev.stopPropagation();
          removeFromMonitor(Number(b.dataset.del));
        };
      });

      const noteEl = $('#displaysNote');
      if (noteEl) {
        noteEl.style.display = list.length > 1 ? '' : 'none';
        noteEl.textContent = list.length > 1
          ? (settings.displays.mode === 'individual'
              ? 'Con «Sin fundido», cada pantalla reproduce su propio vídeo al instante. ' +
                'Con fundido se genera un vídeo compuesto.'
              : 'Disposición real de Windows. El escritorio se compone a la resolución del conjunto de pantallas.')
          : '';
      }
    })
    .catch(() => toast('No se pudieron leer las pantallas'));
}

function applyItem(item, monitor) {
  const url = `/api/apply?key=${encodeURIComponent(item.key)}` + (monitor ? `&monitor=${monitor}` : '');
  toast('Aplicando…');
  api(url)
    .then(() => {
      toast('Wallpaper aplicado');
      loadDisplays();
    })
    .catch((e) => toast('No se pudo aplicar (' + e + ')'));
}

function showSection(name) {
  $$('.sec').forEach((s) => s.classList.remove('active'));
  $('#sec-' + name).classList.add('active');
  $$('.nav-item').forEach((b) => b.classList.toggle('active', b.dataset.sec === name));
  if (name === 'catalog' && state.items.length === 0) loadCatalog(true);
  if (name === 'connect') loadConn();
  if (name === 'plugins') loadPlugins();
  if (name === 'displays') loadDisplays();
  if (name === 'mosaic' && typeof window.mosaicInit === 'function') {
    window.mosaicInit();
  }
  if (name === 'playlist') loadPlaylist();
}

function renderPlaylist() {
  const el = $('#plList');
  if (!state.playlist.length) {
    el.innerHTML = '<li class="muted">La playlist está vacía.</li>';
    return;
  }
  el.innerHTML = state.playlist.map((it, i) => `
    <li>
      <span class="grow">${it.title || it.path}</span>
      <button class="btn ghost" data-i="${i}">Quitar</button>
    </li>`).join('');
  $$('#plList button').forEach((b) => {
    b.onclick = () => removeFromPlaylist(Number(b.dataset.i));
  });
}

// ------------------------------------------------------------ subida local
// El fichero se elige en el navegador (drag & drop o selector) y se sube al
// servidor. Con "Guardar en el catálogo" se queda como wallpaper más; sin
// marcar, el servidor lo usa para exportar y lo descarta.
function uploadLocal(file) {
  if (!file) return;
  const persist = $('#upPersist').checked;
  const url = `/api/upload${persist ? '?persist=1' : ''}`;
  toast(`Subiendo ${file.name}…`);

  const xhr = new XMLHttpRequest();
  xhr.open('POST', url, true);
  xhr.setRequestHeader('X-Filename', file.name);
  xhr.setRequestHeader('Content-Type', file.type || 'application/octet-stream');

  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      toast(`Subiendo ${file.name}… ${pct}%`);
    }
  };

  xhr.onload = () => {
    let res = {};
    try { res = JSON.parse(xhr.responseText || '{}'); } catch (e) { res = {}; }
    if (xhr.status !== 200 || !res.ok) {
      toast('No se pudo subir: ' + (res.error || xhr.status));
      return;
    }
    toast(persist ? 'Guardado en el catálogo' : 'Subido (temporal)');
    loadCatalog(true);
  };

  xhr.onerror = () => toast('Error de red al subir');
  xhr.send(file);
}

// ------------------------------------------------------------ plugins
function loadPlugins() {
  api('/api/plugins')
    .then((list) => {
      const el = $('#pluginsList');
      if (!Array.isArray(list) || !list.length) {
        el.innerHTML = '<li class="muted">No hay plugins instalados.</li>';
        return;
      }
      el.innerHTML = list.map((p) => `
        <li>
          <span class="grow">${p.name}<span class="muted"> · ${p.id}</span></span>
          ${p.config ? `<button class="btn ghost" data-cfg="${p.id}">Configurar</button>` : ''}
          <label class="switch">
            <input type="checkbox" data-id="${p.id}" ${p.enabled ? 'checked' : ''}>
            <span></span>
          </label>
        </li>`).join('');
      $$('#pluginsList [data-cfg]').forEach((b) => {
        b.onclick = () => openPluginConfig(b.dataset.cfg, b.dataset.cfg);
      });
      $$('#pluginsList input').forEach((cb) => {
        cb.onchange = () => {
          const on = cb.checked;
          fetch(`/api/plugins/toggle?id=${encodeURIComponent(cb.dataset.id)}&enabled=${on ? 1 : 0}`,
                { method: 'POST' })
            .then(() => {
              toast((on ? 'Activado: ' : 'Desactivado: ') + cb.dataset.id);
              loadCatalog(true);
            })
            .catch(() => {
              cb.checked = !on;
              toast('No se pudo cambiar');
            });
        };
      });
    })
    .catch(() => toast('No se pudieron leer los plugins'));
}

// ------------------------------------------------------------ salvapantallas
const settings = {
  ss: { enabled: true, wait: 10, key: '', title: '' },
  conn: { ddns: '', useDdns: false },
  displays: { mode: 'individual', fit: 'expand', animate: true, none: [] }
};

function applySettingsToUi() {
  const ss = settings.ss;
  if ($('#ssEnabled')) $('#ssEnabled').checked = ss.enabled;
  if ($('#ssWait')) $('#ssWait').value = ss.wait;
  if ($('#ssPicked')) $('#ssPicked').textContent = ss.title || 'Sin elegir';
  if ($('#ddnsHost')) $('#ddnsHost').value = settings.conn.ddns || '';
  if ($('#ddnsUse')) $('#ddnsUse').checked = !!settings.conn.useDdns;
  const hint = $('#ddnsHint');
  if (hint) {
    hint.textContent = settings.conn.useDdns && ddnsUrl()
      ? 'El QR usará ' + ddnsUrl()
      : 'Se usará la IP de las interfaces marcadas.';
  }
}

/** Normaliza lo que escribe el usuario a una URL completa. */
function ddnsUrl() {
  let h = (settings.conn.ddns || '').trim();
  if (!h) return '';
  if (!/^https?:\/\//i.test(h)) h = 'http://' + h;
  h = h.replace(/\/+$/, '');
  // Sin puerto explícito se usa el del servidor.
  const rest = h.replace(/^https?:\/\//i, '');
  if (!/:\d+$/.test(rest)) {
    const port = (conn.info && conn.info.port) || 8477;
    h = h + ':' + port;
  }
  return h;
}

function loadSettings() {
  api('/api/settings')
    .then((s) => {
      const ss = (s && s.screensaver) || {};
      settings.ss = {
        enabled: ss.enabled !== false,
        wait: Number(ss.wait || 10),
        key: ss.key || '',
        title: ss.title || ''
      };
      const c = (s && s.connection) || {};
      settings.conn = { ddns: c.ddns || '', useDdns: !!c.useDdns };
      const d = (s && s.displays) || {};
      const mode = ['individual', 'clone', 'expand', 'none'].includes(d.mode)
        ? d.mode
        : (d.mode === 'span' ? 'expand' : 'individual');
      const fit = ['expand', 'fill', 'center'].includes(d.fit) ? d.fit : 'expand';
      // `perMonitor` guarda la asignación de cada pantalla (key/fit/rect) y
      // debe conservarse: si se descarta aquí, el siguiente guardado la borra
      // del fichero y el editor pierde lo que el usuario había preparado.
      settings.displays = {
        mode,
        fit,
        animate: d.animate !== false,
        none: Array.isArray(d.none) ? d.none.map(Number) : [],
        fade: typeof d.fade === 'number' ? d.fade : 0.8,
        cloneLow: !!d.cloneLow,
        appliedSig: typeof d.appliedSig === 'string' ? d.appliedSig : '',
        appliedKey: typeof d.appliedKey === 'string' ? d.appliedKey : '',
        perMonitor: (d.perMonitor && typeof d.perMonitor === 'object')
          ? d.perMonitor : {}
      };
      if ($('#animPreview')) $('#animPreview').checked = settings.displays.animate;
      if ($('#fitMode')) $('#fitMode').value = settings.displays.fit;
      $$('[data-dmode]').forEach((b) =>
        b.classList.toggle('active', b.dataset.dmode === settings.displays.mode));
      applySettingsToUi();
      renderQr();
      // El editor de pantallas vive en displays-v2.js y necesita el estado ya
      // cargado para pintar cada monitor con su asignación guardada.
      if (typeof loadDisplays === 'function') loadDisplays();
    })
    .catch(() => applySettingsToUi());
}

function saveSettings() {
  fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      screensaver: settings.ss,
      connection: settings.conn,
      displays: settings.displays
    })
  }).catch(() => toast('No se pudo guardar'));
}

// ------------------------------------------------------------ playlist
// El fichero lo comparte el motor: rutas locales + intervalos en segundos.
function baseNameOf(p) {
  const s = String(p).replace(/\\/g, '/');
  const i = s.lastIndexOf('/');
  return s.slice(i + 1);
}

function loadPlaylist() {
  api('/api/playlist')
    .then((p) => {
      state.playlist = (p.items || []).map((path) => ({
        path,
        title: baseNameOf(path).replace(/\.[a-z0-9]+$/i, '')
      }));
      state.plInterval = Number(p.intervalSeconds || 45);
      renderPlaylist();
      if ($('#plInterval')) $('#plInterval').value = state.plInterval;
      refreshPlaylistStatus();
    })
    .catch(() => {});
}

/** El motor marca current.json con "playlist": true cuando rota. */
function refreshPlaylistStatus() {
  api('/api/current').then((c) => {
    const on = !!(c && c.playlist);
    const el = $('#plStatus');
    if (el) {
      el.textContent = on
        ? 'Activa · cambia cada ' + state.plInterval + ' s'
        : 'Detenida';
      el.style.color = on ? 'var(--accent)' : '';
    }
    if ($('#plStart')) $('#plStart').disabled = on || !state.playlist.length;
    if ($('#plStop')) $('#plStop').disabled = !on;
  }).catch(() => {});
}

function activatePlaylist() {
  fetch('/api/playlist/activate', { method: 'POST' })
    .then((r) => (r.ok ? r.json() : Promise.reject(r.status)))
    .then(() => { toast('Rotación activada'); refreshPlaylistStatus(); })
    .catch(() => toast('No se pudo activar (¿playlist vacía?)'));
}

function stopPlaylist() {
  fetch('/api/playlist/stop', { method: 'POST' })
    .then(() => { toast('Rotación detenida'); refreshPlaylistStatus(); })
    .catch(() => toast('No se pudo detener'));
}

function setPlaylistInterval(seconds) {
  const v = Math.max(3, Math.min(3600, Number(seconds) || 45));
  fetch('/api/playlist/interval?seconds=' + v, { method: 'POST' })
    .then(() => { state.plInterval = v; refreshPlaylistStatus(); })
    .catch(() => toast('No se pudo cambiar el intervalo'));
}

function addToPlaylist(key, title) {
  fetch('/api/playlist/add?key=' + encodeURIComponent(key), { method: 'POST' })
    .then((r) => (r.ok ? r.json() : Promise.reject(r.status)))
    .then(() => {
      toast('Añadido a la playlist: ' + title);
      loadPlaylist();
    })
    .catch(() => toast('No se pudo añadir a la playlist'));
}

function removeFromPlaylist(index) {
  fetch('/api/playlist/remove?index=' + index, { method: 'POST' })
    .then(() => loadPlaylist())
    .catch(() => toast('No se pudo quitar'));
}

// ------------------------------------------------------------ config de plugins
// Editor de carpetas del plugin local (config.json del propio plugin).
const pcfg = { id: '', folders: [] };

function openPluginConfig(id, name) {
  pcfg.id = id;
  pcfg.folders = [];
  pcfg.data = {};
  pcfg.hasFolders = false;
  $('#cfgTitle').textContent = 'Configurar ' + name;
  $('#cfgNew').value = '';
  $('#pluginCfg').hidden = false;
  api('/api/plugin/' + encodeURIComponent(id) + '/config').then((cfg) => {
    pcfg.data = (cfg && typeof cfg === 'object') ? cfg : {};
    pcfg.hasFolders = Array.isArray(pcfg.data.folders);
    pcfg.folders = pcfg.hasFolders ? pcfg.data.folders.slice() : [];
    renderPluginConfig();
  }).catch(() => {
    renderPluginConfig();
    toast('No se pudo leer la configuración');
  });
}

function renderPluginConfig() {
  const el = $('#cfgList');
  const hint = $('#cfgHint');
  if (hint) hint.textContent = 'Configuración del plugin.';
  // La fila de "añadir carpeta" sólo tiene sentido si el plugin usa carpetas.
  const addRow = document.getElementById('cfgAddRow');
  if (addRow) addRow.hidden = !pcfg.hasFolders;
  const rows = [];
  if (pcfg.hasFolders) {
    rows.push('<li class="muted" style="border:0;padding:0 0 4px">Carpetas que se recorren:</li>');
    if (!pcfg.folders.length) {
      rows.push('<li class="muted">Sin carpetas todavía.</li>');
    } else {
      rows.push(pcfg.folders.map((f, i) => `
        <li>
          <span class="grow" style="font-size:12px">${f}</span>
          <button class="btn ghost" data-del="${i}">Quitar</button>
        </li>`).join(''));
    }
  }
  // Resto de claves simples (p. ej. la API key de Pixabay).
  Object.keys(pcfg.data).forEach((k) => {
    if (k === 'folders') return;
    const v = pcfg.data[k];
    if (typeof v === 'string' || typeof v === 'number') {
      const secret = /key|secret|token|pass/i.test(k);
      rows.push(`<li style="display:block">
        <label class="muted" style="font-size:12px;display:block;margin-bottom:4px">${k}</label>
        <input class="input" data-cfgfield="${k}" type="${secret ? 'password' : 'text'}"
               value="${String(v).replace(/"/g, '&quot;')}">
      </li>`);
    }
  });
  el.innerHTML = rows.join('') || '<li class="muted">Este plugin no tiene ajustes.</li>';
  $$('#cfgList [data-del]').forEach((b) => {
    b.onclick = () => {
      pcfg.folders.splice(Number(b.dataset.del), 1);
      renderPluginConfig();
    };
  });
}

function savePluginConfig() {
  // Se envía el objeto completo: hay plugins cuya config tiene más claves
  // (p. ej. la API key de Pixabay) y no se deben perder.
  const out = Object.assign({}, pcfg.data);
  if (pcfg.hasFolders) out.folders = pcfg.folders;
  $$('#cfgList [data-cfgfield]').forEach((inp) => {
    out[inp.dataset.cfgfield] = inp.value;
  });
  fetch('/api/plugin/' + encodeURIComponent(pcfg.id) + '/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(out)
  }).then((r) => (r.ok ? r.json() : Promise.reject(r.status)))
    .then(() => {
      toast('Configuración guardada · recargando catálogo');
      $('#pluginCfg').hidden = true;
      loadCatalog(true);
    })
    .catch(() => toast('No se pudo guardar la configuración'));
}


// El servidor expone sus interfaces para que el QR lleve solo las que el
// usuario marque (Ethernet y Wi-Fi pueden ser válidas a la vez).
const conn = { info: null, ifaces: [], selected: {} };

function connUrls() {
  return conn.ifaces
    .filter((f) => conn.selected[f.ip])
    .map((f) => f.url);
}

function renderConnList() {
  const el = $('#connIfaces');
  if (!conn.ifaces.length) {
    el.innerHTML = '<li class="muted">No se han encontrado interfaces con puerta de enlace.</li>';
    return;
  }
  el.innerHTML = conn.ifaces.map((f) => `
    <li>
      <label class="iface">
        <input type="checkbox" data-ip="${f.ip}" ${conn.selected[f.ip] ? 'checked' : ''}>
        <span class="grow">
          ${f.name || 'Interfaz'} · <b>${f.ip}</b>
          ${f.default ? '<span class="tag">principal</span>' : ''}
          ${f.virtual ? '<span class="tag warn">virtual</span>' : ''}
        </span>
      </label>
    </li>`).join('');
  $$('#connIfaces input').forEach((cb) => {
    cb.onchange = () => {
      conn.selected[cb.dataset.ip] = cb.checked;
      renderQr();
    };
  });
}

function renderQr() {
  const urls = connUrls();
  const box = $('#connQr');
  box.innerHTML = '';
  // Si hay DDNS y está activado, el QR lleva solo esa dirección: es la que
  // funciona desde fuera de casa.
  const custom = settings.conn.useDdns ? ddnsUrl() : '';
  const list = custom ? [custom] : urls;
  $('#connUrls').textContent = list.length ? list.join('  ·  ') : 'Sin interfaces seleccionadas';
  if (!list.length || typeof QRCode === 'undefined') return;
  const text = list.length === 1
    ? list[0]
    : JSON.stringify({ product: 'dnxwallpaper', urls: list });
  new QRCode(box, {
    text,
    width: 220,
    height: 220,
    correctLevel: QRCode.CorrectLevel.M,
    colorDark: '#14161a',
    colorLight: '#ffffff'
  });
}

function loadConn() {
  api('/api/info')
    .then((info) => {
      conn.info = info;
      $('#connServer').textContent =
        `${info.name || 'PC'} · puerto ${info.port} · ${info.items || 0} wallpapers`;
      $('#connAuth').textContent = info.auth ? 'token activo' : 'sin token';
      conn.ifaces = info.interfaces || [];
      if (!conn.ifaces.some((f) => conn.selected[f.ip])) {
        conn.ifaces.forEach((f) => {
          conn.selected[f.ip] = !!f.default && !f.virtual;
        });
        if (!connUrls().length && conn.ifaces.length) {
          conn.selected[conn.ifaces[0].ip] = true;
        }
      }
      renderConnList();
      renderQr();
    })
    .catch(() => {
      $('#connServer').textContent = 'No responde el servidor';
      $('#connIfaces').innerHTML = '';
      $('#connQr').innerHTML = '';
    });
}

function boot() {
  api('/api/info').then((info) => {
    state.total = info.items || 0;
    $('#pcName').textContent = info.name || 'PC';
    $('#pcAddr').textContent = `puerto ${info.port || location.port}`;
    $('#aboutInfo').textContent = `Servidor ${info.version} · ${info.items} wallpapers · puerto ${info.port}`;
    renderCatalog();
  }).catch(() => {
    $('#pcName').textContent = 'Sin conexión';
    $('#aboutInfo').textContent = 'No responde el servidor local.';
  });

  $$('.nav-item').forEach((b) => (b.onclick = () => showSection(b.dataset.sec)));

  $$('[data-dmode]').forEach((b) => {
    b.onclick = () => {
      settings.displays.mode = b.dataset.dmode;
      $$('[data-dmode]').forEach((x) =>
        x.classList.toggle('active', x.dataset.dmode === settings.displays.mode));
      saveSettings();
      loadDisplays();
      const names = {
        individual: 'Un wallpaper por monitor',
        clone: 'El mismo wallpaper en todos',
        expand: 'Un wallpaper repartido entre todos',
        none: 'Sin wallpaper'
      };
      toast(names[settings.displays.mode] || '');
    };
  });
  if ($('#fitMode')) $('#fitMode').onchange = (e) => {
    settings.displays.fit = e.target.value;
    saveSettings();
    loadDisplays();
    toast('Ajuste: ' + e.target.options[e.target.selectedIndex].text);
  };  if ($('#animPreview')) $('#animPreview').onchange = (e) => {
    settings.displays.animate = e.target.checked;
    saveSettings();
    loadDisplays();
  };

  // Aplicar y Descartar los controla el editor de escritorio (displays-v2):
  // allí el botón compone el escritorio con /api/compose.
  if ($('#optOpen')) $('#optOpen').onclick = () => openOptimize(0);
  bindOptimizer();

  $('#connReload').onclick = () => loadConn();  if ($('#ddnsHost')) $('#ddnsHost').onchange = (e) => {
    settings.conn.ddns = e.target.value.trim();
    saveSettings();
    applySettingsToUi();
    renderQr();
  };  if ($('#ddnsUse')) $('#ddnsUse').onchange = (e) => {
    settings.conn.useDdns = e.target.checked;
    saveSettings();
    applySettingsToUi();
    renderQr();
  };  $('#connCopy').onclick = () => {
    const urls = connUrls();
    if (!urls.length) return toast('No hay direcciones seleccionadas');
    const text = urls.length === 1 ? urls[0] : JSON.stringify({ product: 'dnxwallpaper', urls });
    navigator.clipboard.writeText(text)
      .then(() => toast('Dirección copiada'))
      .catch(() => toast(text));
  };

  $('#search').oninput = debounce((e) => {
    const q = e.target.value.trim();
    if (q === state.liveQuery) return;   // misma búsqueda en vivo: se respeta
    state.query = q;
    // Si cambia el texto, las respuestas de plugins en curso ya no valen.
    if (state.liveQuery) cancelLive();
    loadCatalog(true);
  }, 350);
  // Enter: además de filtrar en local, se pregunta a los plugins.
  $('#search').onkeydown = (e) => {
    if (e.key !== 'Enter') return;
    e.preventDefault();
    searchPlugins(e.target.value);
  };
  if ($('#liveBack')) $('#liveBack').onclick = () => leaveLiveSearch();
  $$('#catFilters .chip').forEach((b) => {
    b.onclick = () => {
      state.ftype = b.dataset.t === 'all' ? '' : b.dataset.t;
      $$('#catFilters .chip').forEach((x) => x.classList.toggle('active', x === b));
      loadCatalog(true);
    };
  });
  if ($('#catOrient')) {
    $('#catOrient').onchange = (e) => {
      state.forient = e.target.value;
      loadCatalog(true);
    };
  }
  if ($('#catRes')) {
    $('#catRes').onchange = (e) => {
      state.fminh = Number(e.target.value) || 0;
      loadCatalog(true);
    };
  }
  $('#more').onclick = () => loadCatalog(false);
  if ($('#cfgClose')) $('#cfgClose').onclick = () => ($('#pluginCfg').hidden = true);
  if ($('#cfgCancel')) $('#cfgCancel').onclick = () => ($('#pluginCfg').hidden = true);
  if ($('#cfgAdd')) {
    $('#cfgAdd').onclick = () => {
      const v = $('#cfgNew').value.trim();
      if (!v) return;
      pcfg.folders.push(v);
      $('#cfgNew').value = '';
      renderPluginConfig();
    };
  }
  if ($('#cfgSave')) $('#cfgSave').onclick = () => savePluginConfig();

  // Si falta ffmpeg se ofrece descargarlo al directorio de la aplicación.
  api('/api/tools/ffmpeg').then((s) => {
    if (!s || !s.missing || s.stage === 1) return;
    const msg = 'Falta ffmpeg (se usa para las vistas previas y para aplicar el ' +
                'escritorio).\n\n¿Descargarlo ahora a la carpeta tools? (~100 MB, ' +
                'build oficial de gyan.dev)';
    if (!confirm(msg)) return;
    fetch('/api/tools/ffmpeg', { method: 'POST' });
    toast('Descargando ffmpeg… puede tardar un par de minutos');
    const timer = setInterval(() => {
      api('/api/tools/ffmpeg').then((x) => {
        if (x.stage === 2) {
          clearInterval(timer);
          toast('ffmpeg instalado en tools');
        } else if (x.stage === -1) {
          clearInterval(timer);
          toast('No se pudo descargar ffmpeg');
        }
      }).catch(() => {});
    }, 4000);
  }).catch(() => {});

  if ($('#catRandom')) {
    $('#catRandom').onclick = () => barajar($('#catRandom'), () => state.items, (list) => {
      state.items = list;
      state.offset = list.length;
      state.providers = Array.from(new Set(list.map(providerOf)));
      renderCatalog();
      renderSources();
      // La página aleatoria ya trae suficiente: "Cargar más" volvería a pedir
      // tramos ya mezclados y repetiría tarjetas.
      if ($('#more')) $('#more').style.display = 'none';
    });
  }

  $('#refresh').onclick = () => loadCatalog(true);

  // Recarga profunda: varias páginas por plugin para repoblar el catálogo.
  if ($('#catDeep')) {
    $('#catDeep').onclick = () => {
      const btn = $('#catDeep');
      btn.disabled = true;
      fetch('/api/library/refresh?pages=4', { method: 'POST' })
        .then((r) => r.json())
        .then(() => {
          const timer = setInterval(() => {
            api('/api/library/refresh/status').then((s) => {
              if (s && s.running) {
                btn.textContent = 'Recargando ' + s.done + '/' + s.total +
                                   ' · ' + s.items + ' items';
                return;
              }
              clearInterval(timer);
              btn.disabled = false;
              btn.textContent = 'Recargar catálogo';
              toast('Catálogo: ' + ((s && s.items) || 0) + ' items');
              loadCatalog(true);
            }).catch(() => {});
          }, 1500);
        })
        .catch(() => {
          btn.disabled = false;
          toast('No se pudo recargar el catálogo');
        });
    };
  }

  $('#ssPick').onclick = () => openPicker('Wallpaper del salvapantallas', (it) => {
    settings.ss.key = it.key;
    settings.ss.title = it.title || it.key;
    applySettingsToUi();
    saveSettings();
    toast('Salvapantallas: ' + (it.title || it.key));
  });
  if ($('#ssEnabled')) $('#ssEnabled').onchange = (e) => {
    settings.ss.enabled = e.target.checked;
    saveSettings();
  };
  if ($('#ssWait')) $('#ssWait').onchange = (e) => {
    settings.ss.wait = Math.max(1, Number(e.target.value) || 10);
    saveSettings();
  };
  if ($('#ssInstall')) $('#ssInstall').onclick = () => {
    fetch('/api/screensaver/install', { method: 'POST' })
      .then((r) => (r.ok ? r.json() : Promise.reject(r.status)))
      .then((j) => {
        toast(j.ok
          ? 'Salvapantallas instalado · ' + j.minutes + ' min'
          : 'No se pudo instalar');
        const h = $('#ssInstallHint');
        if (h && j.ok) h.textContent = 'Instalado · se activa tras ' + j.minutes + ' min de inactividad.';
      })
      .catch(() => toast('No se pudo instalar el salvapantallas'));
  };

  $('#plAdd').onclick = () => openPicker('Añadir a la playlist', (it) => {
    addToPlaylist(it.key, it.title || it.key);
  });
  if ($('#plStart')) $('#plStart').onclick = () => activatePlaylist();
  if ($('#plStop')) $('#plStop').onclick = () => stopPlaylist();
  if ($('#plInterval')) {
    $('#plInterval').onchange = (e) => setPlaylistInterval(e.target.value);
  }

  $('#pickerClose').onclick = () => closePicker();
  if ($('#pickerRandom')) {
    $('#pickerRandom').onclick = () => barajar($('#pickerRandom'), () => state.pickerItems,
      (list) => {
        state.pickerItems = list;
        state.pickerPage = 0;
        state.pickerScroll = 0;
        renderPicker();
        const panel = $('#picker .modal-panel');
        if (panel) panel.scrollTop = 0;
      });
  }
  $('#viewerClose').onclick = () => {
    const v = $('#viewerMedia').querySelector('video');
    if (v) v.pause();
    $('#viewer').hidden = true;
  };
  $$('#pickerFilters .chip').forEach((b) => {
    b.onclick = () => {
      state.pickerFilter = b.dataset.pf;
      $$('#pickerFilters .chip').forEach((x) =>
        x.classList.toggle('active', x.dataset.pf === state.pickerFilter));
      cancelPickerLive();
      loadPicker(true);
    };
  });
  if ($('#pickerPlugin')) $('#pickerPlugin').onchange = (e) => {
    state.pickerPlugin = e.target.value;
    cancelPickerLive();
    loadPicker(true);
  };
  // Lista de plugins para el filtro del selector.
  api('/api/plugins').then((list) => {
    const sel = $('#pickerPlugin');
    if (!sel || !Array.isArray(list)) return;
    sel.innerHTML = '<option value="">Todos los plugins</option>' +
      list.filter((p) => p.enabled !== false)
        .map((p) => `<option value="${p.id}">${p.name}</option>`).join('');
    // Nombres legibles para los filtros del catálogo (id -> nombre).
    state.providerNames = {};
    list.forEach((p) => { state.providerNames[p.id] = p.name || p.id; });
    renderSources();
  }).catch(() => {});
  $('#pickerMore').onclick = () => loadPicker(false);
  $('#pickerSearch').oninput = debounce((e) => {
    const q = e.target.value.trim();
    // Mismo término que la búsqueda en vivo ya lanzada: no se invalida (si no,
    // escribir y pulsar Enter enseguida cancelaba los resultados de plugins).
    if (q === state.pickerLiveQuery) return;
    state.pickerQuery = q;
    if (state.pickerLiveQuery) cancelPickerLive();
    loadPicker(true);
  }, 350);
  // Enter en el selector: búsqueda en vivo en los plugins.
  if ($('#pickerSearch')) {
    $('#pickerSearch').onkeydown = (e) => {
      if (e.key !== 'Enter') return;
      e.preventDefault();
      startPickerLive(e.target.value);
    };
  }

  loadDisplays();
  renderPlaylist();
  loadSettings();
  loadPlaylist();
  loadCatalog(true);

  // Subida local: selector o arrastrar sobre el catálogo.
  $('#upFile').onchange = (e) => {
    uploadLocal(e.target.files && e.target.files[0]);
    e.target.value = '';
  };
  const zone = $('#sec-catalog');
  ['dragenter', 'dragover'].forEach((ev) => zone.addEventListener(ev, (e) => {
    e.preventDefault();
    zone.classList.add('drop');
  }));
  ['dragleave', 'drop'].forEach((ev) => zone.addEventListener(ev, (e) => {
    e.preventDefault();
    if (ev === 'dragleave' && zone.contains(e.relatedTarget)) return;
    zone.classList.remove('drop');
  }));
  zone.addEventListener('drop', (e) => {
    if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]) {
      uploadLocal(e.dataTransfer.files[0]);
    }
  });
}

boot();
