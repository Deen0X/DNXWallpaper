// mosaic.js — Generador de mosaicos (fondo + rejilla + capas libres).
// El resultado es un vídeo nuevo que entra al catálogo como item "mosaic:".
(function () {
  'use strict';

  const S = {
    cols: 2,
    rows: 2,
    w: 1920,
    h: 1080,
    bg: { type: 'empty', key: '', color: '', rect: { x: 0, y: 0, w: 1, h: 1 }, fade: 0 },
    cells: [],
    layers: [],
    sel: null,      // {kind:'cell'|'layer'|'bg', i}
    drag: null,
    inited: false
  };

  const $ = (s) => document.querySelector(s);
  const enc = (k) => encodeURIComponent(k || '');

  function api(path, opts) {
    return fetch(path, opts).then((r) => r.json());
  }

  function toast(msg) {
    if (typeof window.toast === 'function') window.toast(msg);
  }

  function typeOf(item) {
    return /image/i.test(String((item && item.type) || '')) ? 'image' : 'video';
  }

  function emptyLayer() {
    return {
      type: 'empty', key: '', color: '',
      rect: { x: 0, y: 0, w: 1, h: 1 }, fade: 0,
      x: 0, y: 0, w: 0, h: 0, rot: 0
    };
  }

  function ensureCells() {
    const n = S.cols * S.rows;
    while (S.cells.length < n) S.cells.push(emptyLayer());
    if (S.cells.length > n) S.cells.length = n;
  }

  // ---------------------------------------------------------------- preview
  function mediaHtml(item, el) {
    if (item.type === 'color') {
      return '';
    }
    const r = item.rect || { x: 0, y: 0, w: 1, h: 1 };
    const w = Math.max(0.05, r.w || 1);
    const h = Math.max(0.05, r.h || 1);
    const sx = 1 / w;
    const sy = 1 / h;
    const tx = -(r.x || 0) / w * 100;
    const ty = -(r.y || 0) / h * 100;
    return `<img src="/api/thumb/${enc(item.key)}" alt=""
      style="position:absolute;inset:0;width:100%;height:100%;object-fit:cover;
      transform-origin:0 0;transform:scale(${sx},${sy}) translate(${tx}%,${ty}%)">`;
  }

  function layerStyle(item) {
    const px = (S.w || 1920);
    const py = (S.h || 1080);
    const r = item.rect || { x: 0, y: 0, w: 1, h: 1 };
    return [
      `left:${(item.x / px) * 100}%`,
      `top:${(item.y / py) * 100}%`,
      `width:${(item.w / px) * 100}%`,
      `height:${(item.h / py) * 100}%`,
      `transform:rotate(${item.rot || 0}deg)`
    ].join(';');
  }

  function cellHtml(c, i) {
    const sel = S.sel && S.sel.kind === 'cell' && S.sel.i === i;
    const cls = 'mo-cell' + (sel ? ' sel' : '') + (c.type === 'empty' ? ' empty' : '');
    const style = c.type === 'color' ? `background:${c.color || '#222'}` : '';
    const inner = c.type === 'empty'
      ? '<span class="mo-plus">+</span>'
      : mediaHtml(c);
    return `<div class="${cls}" data-kind="cell" data-i="${i}" style="${style}">
      ${inner}<span class="mo-tag">${c.type === 'empty' ? '' : (c.type === 'color' ? 'color' : '')}</span>
    </div>`;
  }

  function layerHtml(l, i) {
    const sel = S.sel && S.sel.kind === 'layer' && S.sel.i === i;
    const style = layerStyle(l);
    const bg = l.type === 'color' ? `background:${l.color || '#222'};` : '';
    const inner = l.type === 'color' ? '' : mediaHtml(l);
    const handles = sel
      ? `<span class="mo-h mo-resize" data-h="resize"></span>
         <span class="mo-h mo-rotate" data-h="rotate"></span>`
      : '';
    return `<div class="mo-layer${sel ? ' sel' : ''}" data-kind="layer" data-i="${i}"
      style="${style};${bg}">${inner}${handles}</div>`;
  }

  function render() {
    const stage = $('#moStage');
    if (!stage) return;
    ensureCells();
    stage.style.aspectRatio = `${S.w} / ${S.h}`;
    const bgColor = S.bg.type === 'color' ? S.bg.color : (S.bg.type === 'empty' ? '#101014' : '');
    let html = `<div class="mo-bg" style="${bgColor ? 'background:' + bgColor : ''}">
      ${S.bg.type === 'video' || S.bg.type === 'image' ? mediaHtml(S.bg) : ''}
    </div>`;
    html += `<div class="mo-grid" style="grid-template-columns:repeat(${S.cols},1fr);grid-template-rows:repeat(${S.rows},1fr)">`;
    for (let i = 0; i < S.cells.length; i++) html += cellHtml(S.cells[i], i);
    html += '</div>';
    for (let i = 0; i < S.layers.length; i++) html += layerHtml(S.layers[i], i);
    stage.innerHTML = html;
  }

  // -------------------------------------------------------------- inspector
  function selected() {
    if (!S.sel) return null;
    if (S.sel.kind === 'bg') return S.bg;
    if (S.sel.kind === 'cell') return S.cells[S.sel.i] || null;
    return S.layers[S.sel.i] || null;
  }

  function renderInspector() {
    const box = $('#moInspector');
    const it = selected();
    if (!box) return;
    box.hidden = !it;
    if (!it) return;
    const names = { bg: 'Fondo', cell: 'Celda', layer: 'Capa libre' };
    $('#moSelName').textContent = names[S.sel.kind] +
      (S.sel.kind === 'cell' ? ' ' + (S.sel.i + 1) : (S.sel.kind === 'layer' ? ' ' + (S.sel.i + 1) : ''));
    $('#moFade').value = String(it.fade || 0);
    $('#moColor').value = it.color || '#1b1b22';
    $('#moColorWrap').hidden = false;
    const isLayer = S.sel.kind === 'layer';
    $('#moRotWrap').hidden = !isLayer;
    if (isLayer) {
      $('#moRot').value = String(it.rot || 0);
      $('#moRotVal').textContent = (it.rot || 0) + '°';
    }
    $('#moPick').hidden = false;
  }

  function select(kind, i) {
    S.sel = { kind, i };
    render();
    renderInspector();
  }

  // ----------------------------------------------------------------- picker
  function pickFor(kind, i) {
    if (typeof window.openPicker !== 'function') return;
    window.openPicker('Mosaico', (item) => {
      const target = kind === 'bg' ? S.bg
        : (kind === 'cell' ? S.cells[i] : S.layers[i]);
      if (!target) return;
      target.type = typeOf(item);
      target.key = item.key;
      target.color = '';
      target.rect = { x: 0, y: 0, w: 1, h: 1 };
      if (kind === 'layer') {
        target.w = Math.round(S.w * 0.3);
        target.h = Math.round(S.h * 0.3);
        target.x = Math.round((S.w - target.w) / 2);
        target.y = Math.round((S.h - target.h) / 2);
        target.rot = 0;
      }
      select(kind, i);
      scheduleSave();
    });
  }

  // ------------------------------------------------------------- aleatorio
  function randomize() {
    ensureCells();
    const free = [];
    for (let i = 0; i < S.cells.length; i++) {
      const c = S.cells[i];
      // Una celda con color fijo cuenta como asignada.
      if (c.type === 'empty' && !c.key && !c.color) free.push(i);
    }
    if (!free.length) { toast('No hay celdas vacías'); return; }
    const used = ['|' + (S.bg.key || '') + '|'];
    S.cells.forEach((c) => { if (c.key) used.push('|' + c.key + '|'); });
    api('/api/mosaic/random?count=' + free.length + '&exclude=' + encodeURIComponent(used.join('')))
      .then((r) => {
        const keys = (r && r.keys) || [];
        free.forEach((idx, n) => {
          if (!keys[n]) return;
          S.cells[idx].type = 'video';
          S.cells[idx].key = keys[n];
          S.cells[idx].rect = { x: 0, y: 0, w: 1, h: 1 };
        });
        render();
        scheduleSave();
        toast(keys.length + ' asignados');
      })
      .catch(() => toast('No se pudieron elegir aleatorios'));
  }

  // ---------------------------------------------------------------- generar
  let progressTimer = 0;

  function generate() {
    ensureCells();
    const btn = $('#moGen');
    const status = $('#moStatus');
    const track = $('#moTrack');
    const fill = $('#moFill');
    const withContent = S.cells.some((c) => c.type !== 'empty') ||
      S.bg.type !== 'empty' || S.layers.length;
    if (!withContent) { toast('Añade algún wallpaper o color'); return; }
    btn.disabled = true;
    if (track) { track.hidden = false; fill.style.width = '0%'; }
    status.textContent = 'Generando el mosaico…';
    progressTimer = setInterval(() => {
      api('/api/progress').then((p) => {
        if (!p || !p.active) return;
        if (p.stage === 'descargando medios') {
          status.textContent = 'Descargando los vídeos que faltan…';
          if (fill) fill.classList.add('indet');
          return;
        }
        const pc = (typeof p.percent === 'number' && p.percent >= 0) ? p.percent : 0;
        if (fill) {
          fill.classList.remove('indet');
          fill.style.width = Math.max(1, Math.min(99, pc)) + '%';
        }
        status.textContent = 'Generando el mosaico… ' + pc + ' %';
      }).catch(() => {});
    }, 400);

    fetch('/api/mosaic', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload())
    }).then((r) => r.json()).then((res) => {
      if (!res.ok) {
        const det = res.detail ? ' — ' + res.detail : '';
        throw new Error((res.error || 'error') + det);
      }
      if (fill) fill.style.width = '100%';
      status.textContent = 'Listo: ' + res.key + ' (' + res.width + '×' + res.height + ', ' + res.seconds + ' s)';
      toast('Mosaico generado');
      if (typeof window.loadCatalog === 'function') window.loadCatalog(true);
    }).catch((e) => {
      const msg = (e && e.message) ? e.message : String(e);
      status.textContent = 'Error: ' + msg;
      status.title = msg;
      if (fill) fill.style.width = '0%';
    }).finally(() => {
      clearInterval(progressTimer);
      btn.disabled = false;
      if (track) setTimeout(() => { track.hidden = true; }, 800);
    });
  }

  function payload() {
    const cells = S.cells.map((c) => ({
      type: c.type, key: c.key || '', color: c.color || '',
      fade: Number(c.fade) || 0,
      cx: c.rect.x, cy: c.rect.y, cw: c.rect.w, ch: c.rect.h
    }));
    const layers = S.layers.map((l) => ({
      type: l.type, key: l.key || '', color: l.color || '',
      fade: Number(l.fade) || 0,
      x: Math.round(l.x), y: Math.round(l.y),
      w: Math.round(l.w), h: Math.round(l.h), rot: Number(l.rot) || 0,
      cx: l.rect.x, cy: l.rect.y, cw: l.rect.w, ch: l.rect.h
    }));
    const bg = {
      type: S.bg.type, key: S.bg.key || '', color: S.bg.color || '',
      fade: Number(S.bg.fade) || 0
    };
    return {
      cols: S.cols, rows: S.rows,
      width: S.w, height: S.h,
      maxSizeMB: 64,
      background: bg,
      cells: cells,
      layers: layers
    };
  }

  // --------------------------------------------------------------- proyecto
  let saveTimer = 0;
  function scheduleSave() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
      api('/api/mosaic/autosave', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload())
      }).catch(() => {});
    }, 600);
  }

  // Aplica un proyecto (guardado o recuperado de un mosaico) al editor.
  function applyProject(p) {
    if (!p || !p.cells || !p.cells.length) return false;
    S.cols = Math.max(1, Math.min(5, Number(p.cols) || 2));
    S.rows = Math.max(1, Math.min(5, Number(p.rows) || 2));
    S.w = Number(p.width) || S.w;
    S.h = Number(p.height) || S.h;
    S.bg = emptyLayer();
    if (p.background) {
      S.bg = Object.assign(emptyLayer(), {
        type: p.background.type || 'empty',
        key: p.background.key || '',
        color: p.background.color || '',
        fade: Number(p.background.fade) || 0
      });
    }
    S.cells = (p.cells || []).map((c) => Object.assign(emptyLayer(), {
      type: c.type || (c.key ? 'video' : (c.color ? 'color' : 'empty')),
      key: c.key || '', color: c.color || '',
      fade: Number(c.fade) || 0,
      rect: { x: c.cx || 0, y: c.cy || 0, w: c.cw || 1, h: c.ch || 1 }
    }));
    S.layers = (p.layers || []).map((l) => Object.assign(emptyLayer(), {
      type: l.type || (l.key ? 'video' : (l.color ? 'color' : 'empty')),
      key: l.key || '', color: l.color || '',
      fade: Number(l.fade) || 0,
      x: Number(l.x) || 0, y: Number(l.y) || 0,
      w: Number(l.w) || 400, h: Number(l.h) || 300,
      rot: Number(l.rot) || 0,
      rect: { x: l.cx || 0, y: l.cy || 0, w: l.cw || 1, h: l.ch || 1 }
    }));
    ensureCells();
    S.sel = null;
    syncControls();
    render();
    renderInspector();
    return true;
  }

  // Al arrancar se recupera el estado de la sesión (autosave) o, si no hay,
  // el último proyecto guardado a mano.
  function loadProject() {
    return api('/api/mosaic/autosave').then((p) => {
      if (p && p.cells && p.cells.length) return applyProject(p);
      return api('/api/mosaic/project').then((q) => applyProject(q));
    }).catch(() => false);
  }

  function syncControls() {
    if ($('#moCols')) $('#moCols').value = String(S.cols);
    if ($('#moRows')) $('#moRows').value = String(S.rows);
    if ($('#moW')) $('#moW').value = String(S.w);
    if ($('#moH')) $('#moH').value = String(S.h);
    warnIfBig();
  }

  function warnIfBig() {
    const w = $('#moWarn');
    if (!w) return;
    const n = S.cols * S.rows;
    w.hidden = n <= 4;
    if (n > 4) {
      const fuerte = n > 9 ? ' Mucho ojo: a partir de 4x4 puede tardar varios minutos.' : '';
      w.textContent = `⚠ ${n} celdas: se decodifican ${n} vídeos a la vez, ` +
        `el vídeo tardará bastante en generarse.${fuerte}`;
    }
  }

  // ------------------------------------------------------------- interacción
  function stagePx() {
    const stage = $('#moStage');
    const r = stage ? stage.getBoundingClientRect() : { width: 960, height: 540 };
    return { w: r.width, h: r.height };
  }

  function toCanvas(ev) {
    const { w, h } = stagePx();
    const stage = $('#moStage');
    const r = stage.getBoundingClientRect();
    return {
      x: (ev.clientX - r.left) / r.width * S.w,
      y: (ev.clientY - r.top) / r.height * S.h,
      px: (ev.clientX - r.left) / r.width,
      py: (ev.clientY - r.top) / r.height
    };
  }

  function onStageClick(ev) {
    const cell = ev.target.closest('.mo-cell');
    const layer = ev.target.closest('.mo-layer');
    if (layer) {
      const i = Number(layer.dataset.i);
      if (!S.sel || S.sel.kind !== 'layer' || S.sel.i !== i) {
        select('layer', i);
      } else if (ev.detail > 1) {
        pickFor('layer', i);
      }
      return;
    }
    if (cell) {
      const i = Number(cell.dataset.i);
      const c = S.cells[i];
      if (c.type === 'empty') {
        select('cell', i);
        pickFor('cell', i);
      } else if (S.sel && S.sel.kind === 'cell' && S.sel.i === i && ev.detail > 1) {
        pickFor('cell', i);
      } else {
        select('cell', i);
      }
      return;
    }
    const bg = ev.target.closest('.mo-bg');
    if (bg) {
      select('bg', 0);
      if (ev.detail > 1) pickFor('bg', 0);
    }
  }

  function onWheel(ev) {
    const cell = ev.target.closest('.mo-cell');
    const layer = ev.target.closest('.mo-layer');
    if (!cell && !layer) return;
    if (ev.target.closest('.mo-h')) return;
    const kind = layer ? 'layer' : 'cell';
    const i = Number((layer || cell).dataset.i);
    if (!S.sel || S.sel.kind !== kind || S.sel.i !== i) {
      select(kind, i);
    }
    ev.preventDefault();
    const it = kind === 'cell' ? S.cells[i] : S.layers[i];
    const f = ev.deltaY < 0 ? 0.9 : 1 / 0.9;
    zoomItem(it, f);
  }

  function zoomItem(it, f) {
    const r = it.rect || { x: 0, y: 0, w: 1, h: 1 };
    const nw = Math.max(0.05, Math.min(2, r.w * f));
    const nh = Math.max(0.05, Math.min(2, r.h * f));
    const cx = r.x + r.w / 2;
    const cy = r.y + r.h / 2;
    it.rect = {
      x: Math.max(-0.5, Math.min(1.5, cx - nw / 2)),
      y: Math.max(-0.5, Math.min(1.5, cy - nh / 2)),
      w: nw, h: nh
    };
    render();
    scheduleSave();
  }

  // Pan con botón central (o izquierdo si no es una capa/celda seleccionada).
  function onMouseDown(ev) {
    const layer = ev.target.closest('.mo-layer');
    const cell = ev.target.closest('.mo-cell');
    const handle = ev.target.closest('.mo-h');
    if (handle && layer) {
      ev.preventDefault();
      const i = Number(layer.dataset.i);
      S.drag = { mode: handle.dataset.h, kind: 'layer', i, start: toCanvas(ev),
        rot0: S.layers[i].rot || 0, w0: S.layers[i].w, h0: S.layers[i].h };
      return;
    }
    if (ev.button === 1) {
      const kind = layer ? 'layer' : (cell ? 'cell' : null);
      if (!kind) return;
      ev.preventDefault();
      const i = Number((layer || cell).dataset.i);
      const it = kind === 'cell' ? S.cells[i] : S.layers[i];
      S.drag = { mode: 'pan', kind, i, start: toCanvas(ev), rect0: Object.assign({}, it.rect) };
      return;
    }
    if (layer && ev.button === 0) {
      ev.preventDefault();
      const i = Number(layer.dataset.i);
      S.drag = { mode: 'move', kind: 'layer', i, start: toCanvas(ev), x0: S.layers[i].x, y0: S.layers[i].y };
      select('layer', i);
    }
  }

  function onMouseMove(ev) {
    if (!S.drag) return;
    const p = toCanvas(ev);
    const d = S.drag;
    if (d.mode === 'pan') {
      const it = d.kind === 'cell' ? S.cells[d.i] : S.layers[d.i];
      const dx = (p.px - d.start.px);
      const dy = (p.py - d.start.py);
      it.rect = {
        x: d.rect0.x - dx * d.rect0.w,
        y: d.rect0.y - dy * d.rect0.h,
        w: d.rect0.w, h: d.rect0.h
      };
      render();
    } else if (d.mode === 'move') {
      const L = S.layers[d.i];
      L.x = Math.round(d.x0 + (p.x - d.start.x));
      L.y = Math.round(d.y0 + (p.y - d.start.y));
      render();
    } else if (d.mode === 'resize') {
      const L = S.layers[d.i];
      L.w = Math.max(24, Math.round(d.w0 + (p.x - d.start.x)));
      L.h = Math.max(24, Math.round(d.h0 + (p.y - d.start.y)));
      render();
    } else if (d.mode === 'rotate') {
      const L = S.layers[d.i];
      const cx = L.x + L.w / 2;
      const cy = L.y + L.h / 2;
      const a = Math.atan2(p.y - cy, p.x - cx) * 180 / Math.PI + 90;
      L.rot = Math.round(a);
      render();
      renderInspector();
    }
  }

  function onMouseUp() {
    if (!S.drag) return;
    const moved = S.drag.mode !== 'pan';
    S.drag = null;
    if (moved) scheduleSave();
  }

  // ------------------------------------------------------------------- init
  function init() {
    if (S.inited) return;
    S.inited = true;
    ensureCells();
    // Si hay proyecto guardado se recupera; si no, resolución del monitor
    // principal por defecto.
    loadProject().then((loaded) => {
      if (loaded) return;
      api('/api/monitors').then((list) => {
        const arr = Array.isArray(list) ? list : (list.monitors || []);
        const m = arr.find((x) => x.primary) || arr[0];
        if (m) {
          S.w = m.width;
          S.h = m.height;
          syncControls();
          render();
        }
      }).catch(() => {});
    });
    syncControls();
    render();

    $('#moCols').onchange = (e) => {
      S.cols = Math.max(1, Math.min(5, Number(e.target.value) || 2));
      e.target.value = S.cols;
      ensureCells();
      warnIfBig();
      render();
      scheduleSave();
    };
    $('#moRows').onchange = (e) => {
      S.rows = Math.max(1, Math.min(5, Number(e.target.value) || 2));
      e.target.value = S.rows;
      ensureCells();
      warnIfBig();
      render();
      scheduleSave();
    };
    $('#moW').onchange = (e) => {
      S.w = Math.max(320, (Number(e.target.value) || 1920) & ~1);
      e.target.value = S.w;
      render();
      scheduleSave();
    };
    $('#moH').onchange = (e) => {
      S.h = Math.max(240, (Number(e.target.value) || 1080) & ~1);
      e.target.value = S.h;
      render();
      scheduleSave();
    };
    $('#moRandom').onclick = randomize;
    $('#moBg').onclick = () => pickFor('bg', 0);
    $('#moAddLayer').onclick = () => {
      const L = emptyLayer();
      L.type = 'empty';
      L.w = Math.round(S.w * 0.3);
      L.h = Math.round(S.h * 0.3);
      L.x = Math.round((S.w - L.w) / 2);
      L.y = Math.round((S.h - L.h) / 2);
      S.layers.push(L);
      const i = S.layers.length - 1;
      select('layer', i);
      pickFor('layer', i);
    };
    $('#moLoad').onclick = () => {
      api('/api/mosaic/project').then((p) => {
        if (applyProject(p)) toast('Proyecto cargado');
        else toast('No hay proyecto guardado');
      }).catch(() => toast('No se pudo cargar el proyecto'));
    };
    $('#moGen').onclick = generate;
    $('#moSave').onclick = () => {
      api('/api/mosaic/project', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload())
      }).then(() => toast('Proyecto guardado')).catch(() => toast('No se pudo guardar'));
    };
    $('#moPick').onclick = () => {
      if (!S.sel) return;
      pickFor(S.sel.kind, S.sel.i);
    };
    $('#moClear').onclick = () => {
      if (!S.sel) return;
      if (S.sel.kind === 'layer') {
        S.layers.splice(S.sel.i, 1);
      } else if (S.sel.kind === 'cell') {
        S.cells[S.sel.i] = emptyLayer();
      } else {
        S.bg = emptyLayer();
      }
      S.sel = null;
      render();
      renderInspector();
      scheduleSave();
    };
    $('#moReset').onclick = () => {
      const it = selected();
      if (!it) return;
      it.rect = { x: 0, y: 0, w: 1, h: 1 };
      render();
      scheduleSave();
    };
    $('#moFade').onchange = (e) => {
      const it = selected();
      if (!it) return;
      it.fade = Number(e.target.value) || 0;
      scheduleSave();
    };
    $('#moColor').oninput = (e) => {
      const it = selected();
      if (!it) return;
      it.type = 'color';
      it.key = '';
      it.color = e.target.value;
      render();
      renderInspector();
      scheduleSave();
    };
    $('#moRot').oninput = (e) => {
      const it = selected();
      if (!it || S.sel.kind !== 'layer') return;
      it.rot = Number(e.target.value) || 0;
      $('#moRotVal').textContent = it.rot + '°';
      render();
    };
    $('#moRot').onchange = () => scheduleSave();

    const stage = $('#moStage');
    stage.addEventListener('click', onStageClick);
    stage.addEventListener('wheel', onWheel, { passive: false });
    stage.addEventListener('mousedown', onMouseDown);
    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
  }


  // ---------------------------------------------- menú contextual del catálogo
  function closeMenu() {
    const m = document.getElementById('moMenu');
    if (m) m.remove();
  }

  function showMenu(x, y, key) {
    closeMenu();
    const menu = document.createElement('div');
    menu.id = 'moMenu';
    menu.className = 'mo-menu';
    menu.style.left = x + 'px';
    menu.style.top = y + 'px';
    menu.innerHTML =
      '<button data-act="load">Cargar proyecto en el editor</button>' +
      '<button data-act="delete" class="danger">Eliminar mosaico</button>';
    document.body.appendChild(menu);
    menu.querySelectorAll('button').forEach((b) => {
      b.onclick = (ev) => {
        ev.stopPropagation();
        const act = b.dataset.act;
        closeMenu();
        if (act === 'load') {
          api('/api/mosaic/project?key=' + encodeURIComponent(key))
            .then((p) => {
              if (!p || !p.cells) {
                toast('Este mosaico no tiene proyecto guardado');
                return;
              }
              if (applyProject(p)) {
                const nav = document.querySelector('[data-sec=mosaic]');
                if (nav) nav.click();
                toast('Proyecto cargado');
              }
            })
            .catch(() => toast('No se pudo cargar el proyecto'));
          return;
        }
        if (!window.confirm('¿Eliminar este mosaico? Se borra el vídeo generado.')) {
          return;
        }
        api('/api/mosaic/delete?key=' + encodeURIComponent(key), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: '{}'
        }).then((r) => {
          if (r && r.ok) {
            const el = document.querySelector(
              '#catalog .thumb[data-key="' + encodeURIComponent(key) + '"]');
            if (el) el.remove();
            toast('Mosaico eliminado');
          } else {
            toast('No se pudo eliminar');
          }
        }).catch(() => toast('No se pudo eliminar'));
      };
    });
  }

  document.addEventListener('contextmenu', (ev) => {
    const el = ev.target.closest('#catalog .thumb');
    if (!el) return;
    const key = decodeURIComponent(el.dataset.key || '');
    if (key.indexOf('mosaics:') !== 0) return;
    ev.preventDefault();
    showMenu(ev.clientX, ev.clientY, key);
  });
  document.addEventListener('click', closeMenu);
  window.addEventListener('scroll', closeMenu, true);

  window.mosaicInit = init;
  document.addEventListener('DOMContentLoaded', () => {
    if (document.getElementById('sec-mosaic')) init();
  });
})();
