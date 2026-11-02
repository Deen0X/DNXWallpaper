// ============================================================ PANTALLAS v2
// El preview es el escritorio: cada monitor muestra lo que se verá. "Aplicar"
// compone un único vídeo del tamaño del escritorio virtual (cada monitor en su
// sitio) y lo aplica; el motor no necesita cambios.
//
// Estado por monitor: { key, type, fit, rect:{x,y,w,h} }  (rect normalizado 0..1)
//
// La vista previa usa WebP animado (12 fps, 480 px) en vez del vídeo original:
// con la GPU desactivada, varios vídeos a la vez no se sostienen y además cada
// re-render los recreaba (parpadeo al hacer zoom o arrastrar).

function dispState() {
  if (!settings.displays.perMonitor) settings.displays.perMonitor = {};
  return settings.displays.perMonitor;
}

function monitorSlot(i) {
  const st = dispState();
  const k = String(i);
  if (!st[k]) {
    st[k] = {
      key: '',
      type: '',
      fit: settings.displays.fit || 'expand',
      rect: { x: 0, y: 0, w: 1, h: 1 }
    };
  }
  if (!st[k].rect) st[k].rect = { x: 0, y: 0, w: 1, h: 1 };
  return st[k];
}

function primaryIndex() {
  const list = state.monitors || [];
  const p = list.find((m) => m.primary);
  return p ? Number(p.index) : 0;
}

function slotForEdit() {
  if (settings.displays.mode === 'individual') {
    return monitorSlot(state.editingMonitor != null ? state.editingMonitor : primaryIndex());
  }
  return monitorSlot(primaryIndex());
}

function mediaInfo(key) {
  return (state.info && state.info[key]) || { w: 0, h: 0 };
}

function loadMediaInfo(key) {
  if (!key) return Promise.resolve();
  state.info = state.info || {};
  if (state.info[key] && state.info[key].w) return Promise.resolve();
  return fetch('/api/probe?key=' + encodeURIComponent(key))
    .then((r) => (r.ok ? r.json() : null))
    .then((j) => { if (j && j.ok) state.info[key] = { w: j.width, h: j.height }; })
    .catch(() => {});
}

/** Coloca el medio dentro del monitor según fit + rect. */
function placeShot(el, boxW, boxH, slot) {
  const info = mediaInfo(slot.key);
  const iw = info.w || 1;
  const ih = info.h || 1;
  const r = slot.rect || { x: 0, y: 0, w: 1, h: 1 };
  el.style.objectFit = 'fill';
  if (slot.fit === 'fill') {
    el.style.left = '0'; el.style.top = '0';
    el.style.width = boxW + 'px'; el.style.height = boxH + 'px';
    return;
  }
  if (slot.fit === 'center') {
    const s = Math.min(boxW / iw, boxH / ih);
    el.style.width = (iw * s) + 'px';
    el.style.height = (ih * s) + 'px';
    el.style.left = ((boxW - iw * s) / 2) + 'px';
    el.style.top = ((boxH - ih * s) / 2) + 'px';
    return;
  }
  // expand: la región `rect` se ve en el monitor, centrada (si sobra imagen
  // por los lados, se recorta por igual).
  const sc = Math.max(boxW / (r.w * iw), boxH / (r.h * ih));
  const sw = iw * sc;
  const sh = ih * sc;
  el.style.width = sw + 'px';
  el.style.height = sh + 'px';
  el.style.left = (boxW / 2 - (r.x + r.w / 2) * sw) + 'px';
  el.style.top = (boxH / 2 - (r.y + r.h / 2) * sh) + 'px';
}

/** Panorámico: el medio se mapea al escritorio virtual y cada monitor enseña
 *  su trozo (igual que hace el servidor al componer). */
function placePanorama(el, canvas, m, slot, list) {
  const minX = Math.min(...list.map((x) => x.x));
  const minY = Math.min(...list.map((x) => x.y));
  const cw = Math.max(...list.map((x) => x.x + x.width)) - minX;
  const ch = Math.max(...list.map((x) => x.y + x.height)) - minY;
  const info = mediaInfo(slot.key);
  const iw = info.w || 1;
  const ih = info.h || 1;
  const r = slot.rect || { x: 0, y: 0, w: 1, h: 1 };
  let sw, sh, gl, gt;
  if (slot.fit === 'fill') {
    sw = cw; sh = ch; gl = 0; gt = 0;
  } else if (slot.fit === 'center') {
    const s = Math.min(cw / iw, ch / ih);
    sw = iw * s; sh = ih * s;
    gl = (cw - sw) / 2; gt = (ch - sh) / 2;
  } else {
    const sc = Math.max(cw / (r.w * iw), ch / (r.h * ih));
    sw = iw * sc; sh = ih * sc;
    gl = cw / 2 - (r.x + r.w / 2) * sw;
    gt = ch / 2 - (r.y + r.h / 2) * sh;
  }
  const k = canvas.clientWidth / m.width;
  el.style.objectFit = 'fill';
  el.style.width = (sw * k) + 'px';
  el.style.height = (sh * k) + 'px';
  el.style.left = ((gl - (m.x - minX)) * k) + 'px';
  el.style.top = ((gt - (m.y - minY)) * k) + 'px';
}

/** Coloca un medio ya cargado (imagen o vídeo) en su monitor. */
function placeAny(el, canvas, m, slot, mode, list, iw, ih) {
  if (!iw || !ih) return;
  state.info = state.info || {};
  state.info[slot.key] = { w: iw, h: ih };
  if (mode === 'expand') placePanorama(el, canvas, m, slot, list);
  else placeShot(el, canvas.clientWidth, canvas.clientHeight, slot);
  el.style.opacity = '1';
}

/** Registra el medio de un monitor para poder recolocarlo sin re-renderizar. */
function registerShot(canvas, el, m, slot, mode, list) {
  state.shots = (state.shots || []).filter((s) => s.canvas !== canvas);
  state.shots.push({ canvas, el, m, slot, mode, list });
  el.style.opacity = '0';
}

/** Recoloca todo lo que ya está en pantalla (zoom, arrastre, ajuste). */
function repositionAll() {
  (state.shots || []).forEach((s) => {
    const el = s.el;
    if (!el || !el.parentNode) return;
    const isVideo = el.tagName === 'VIDEO';
    const iw = isVideo ? el.videoWidth : el.naturalWidth;
    const ih = isVideo ? el.videoHeight : el.naturalHeight;
    if (iw && ih) {
      placeAny(el, s.canvas, s.m, s.slot, s.mode, s.list, iw, ih);
      return;
    }
    const dims = mediaInfo(s.slot.key);
    if (dims.w) placeAny(el, s.canvas, s.m, s.slot, s.mode, s.list, dims.w, dims.h);
  });
}

/**
 * Pinta el wallpaper de un monitor. La vista previa de un vídeo es un WebP
 * animado (ligero y sin decodificador de vídeo); si no se puede generar, cae
 * al vídeo 540p y, en último término, a la miniatura.
 */
function renderShot(canvas, m, slot, mode, list) {
  const key = slot.key;
  const enc = encodeURIComponent(key);
  const animate = settings.displays.animate !== false;
  const isImage = /image/i.test(String(slot.type || ''));
  canvas.innerHTML = '';

  const showImage = (primary, onFail) => {
    const img = document.createElement('img');
    img.alt = '';
    img.decoding = 'async';
    img.style.opacity = '0';
    registerShot(canvas, img, m, slot, mode, list);
    const place = () => {
      placeAny(img, canvas, m, slot, mode, list, img.naturalWidth, img.naturalHeight);
    };
    img.addEventListener('load', place);
    img.addEventListener('error', () => {
      if (onFail) { onFail(); return; }
      if (!img.dataset.retry) {
        img.dataset.retry = '1';
        img.src = '/api/thumb/' + enc + '?v=1';
        return;
      }
      canvas.innerHTML = '<span class="m-none">sin vista previa</span>';
    });
    canvas.appendChild(img);
    // Si ya se conocen las dimensiones, se coloca al instante (sin destello).
    const dims = mediaInfo(key);
    if (dims.w) placeAny(img, canvas, m, slot, mode, list, dims.w, dims.h);
    img.src = primary;
    return img;
  };

  /** El WebP animado se pide aparte: lo que tarda no debe retrasar la vista. */
  const animateLater = (img, intento = 0) => {
    fetch('/api/preview/' + enc + '?v=1')
      .then((r) => (r.ok ? r.blob() : Promise.reject(new Error('preview'))))
      .then((blob) => {
        if (!img.isConnected) return;
        const url = URL.createObjectURL(blob);
        img.addEventListener('load', () => {
          placeAny(img, canvas, m, slot, mode, list, img.naturalWidth,
                   img.naturalHeight);
          setTimeout(() => URL.revokeObjectURL(url), 2000);
        }, { once: true });
        img.src = url;
      })
      .catch(() => {
        // Si el medio aún se está descargando, se reintenta un par de veces.
        if (intento < 4 && img.isConnected) {
          setTimeout(() => animateLater(img, intento + 1), 4000);
        }
      });
  };

  const showVideo = (after) => {
    const video = document.createElement('video');
    video.muted = true;
    video.loop = true;
    video.autoplay = true;
    video.playsInline = true;
    video.preload = 'metadata';
    video.style.opacity = '0';
    video.poster = '/api/thumb/' + enc + '?v=1';
    registerShot(canvas, video, m, slot, mode, list);
    const place = () => {
      if (!video.videoWidth) return;
      placeAny(video, canvas, m, slot, mode, list, video.videoWidth, video.videoHeight);
    };
    const kick = () => {
      const p = video.play();
      if (p && p.catch) p.catch(() => {});
    };
    video.addEventListener('loadedmetadata', place);
    video.addEventListener('loadeddata', () => { place(); kick(); });
    video.addEventListener('canplay', kick);
    video.addEventListener('error', () => { canvas.innerHTML = ''; after(); });
    canvas.appendChild(video);
    const dims = mediaInfo(key);
    if (dims.w) placeAny(video, canvas, m, slot, mode, list, dims.w, dims.h);
    video.src = '/dl/' + enc + '?p=1';
    kick();
  };

  if (isImage) {
    showImage(animate ? '/dl/' + enc : '/api/thumb/' + enc + '?v=1');
    return;
  }
  if (!animate) {
    showImage('/api/thumb/' + enc + '?v=1');
    return;
  }
  // Vídeo: primero la miniatura (instantánea, ya cacheada por el catálogo) y
  // después el WebP animado, que se genera en el servidor sin bloquear.
  const img = showImage('/api/thumb/' + enc + '?v=1',
                        () => showVideo(() => {}));
  animateLater(img);
}

function renderWorkspace(list) {
  const map = document.getElementById('monitorMap');
  if (!map) return;
  state.shots = [];
  if (!list.length) { map.innerHTML = ''; return; }
  const minX = Math.min(...list.map((m) => m.x));
  const minY = Math.min(...list.map((m) => m.y));
  const maxX = Math.max(...list.map((m) => m.x + m.width));
  const maxY = Math.max(...list.map((m) => m.y + m.height));
  const totalW = Math.max(1, maxX - minX);
  const totalH = Math.max(1, maxY - minY);
  // La altura del mapa se adapta a la ventana: en 720p no debe empujar la
  // barra de Aplicar fuera de la pantalla.
  const maxH = Math.max(120, Math.min(320, Math.round(window.innerHeight * 0.34)));
  const boxW = map.clientWidth || 640;
  const scale = Math.min(boxW / totalW, maxH / totalH);
  map.style.height = Math.round(totalH * scale) + 'px';
  const offsetX = Math.max(0, (boxW - totalW * scale) / 2);

  const mode = settings.displays.mode;
  const prim = primaryIndex();

  map.innerHTML = list.map((m) => {
    const left = (offsetX + (m.x - minX) * scale) + 'px';
    const top = ((m.y - minY) * scale) + 'px';
    const w = (m.width * scale) + 'px';
    const h = (m.height * scale) + 'px';
    const idx = Number(m.index);
    const slot = mode === 'individual' ? monitorSlot(idx) : monitorSlot(prim);
    const assigned = mode === 'none' ? '' : slot.key;
    const name = m.primary ? 'Pantalla principal' : 'Pantalla ' + (idx + 1);
    const fitSel = mode === 'individual'
      ? `<select class="m-fit" data-fit="${idx}" title="Ajuste de esta pantalla">
           <option value="expand"${slot.fit === 'expand' ? ' selected' : ''}>Expandir</option>
           <option value="fill"${slot.fit === 'fill' ? ' selected' : ''}>Rellenar</option>
           <option value="center"${slot.fit === 'center' ? ' selected' : ''}>Centrar</option>
         </select>`
      : '';
    return `<div class="monitor ${m.primary ? 'primary' : ''} ${state.editingMonitor === idx && mode === 'individual' ? 'sel' : ''}"
                 data-mon="${idx}" data-name="${name}"
                 style="left:${left};top:${top};width:${w};height:${h}">
      <div class="m-canvas" data-mon="${idx}"></div>
      ${m.primary ? '<span class="m-badge">principal</span>' : ''}
      ${assigned ? `<button class="m-del" data-del="${idx}" title="Quitar de esta pantalla">✕</button>` : ''}
      ${assigned ? `<button class="m-reset" data-reset="${idx}" title="Restablecer encuadre (sin zoom)">⟲</button>` : ''}
      <span class="m-name">${name}</span>
      <span class="m-res">${assigned ? `${m.width} × ${m.height}` : 'sin wallpaper'}</span>
      <span class="m-prep" data-prep="${idx}"></span>
      ${fitSel}
    </div>`;
  }).join('');

  list.forEach((m) => {
    const idx = Number(m.index);
    const slot = mode === 'individual' ? monitorSlot(idx) : monitorSlot(prim);
    const canvas = map.querySelector('.m-canvas[data-mon="' + idx + '"]');
    if (!canvas) return;
    if (mode === 'none' || !slot.key) { canvas.innerHTML = ''; return; }
    renderShot(canvas, m, slot, mode, list);
  });

  map.querySelectorAll('.monitor').forEach((el) => {
    const idx = Number(el.dataset.mon);
    el.onclick = (ev) => {
      if (ev.target.closest('.m-del') || ev.target.closest('.m-fit') ||
          ev.target.closest('.m-reset')) return;
      // Un arrastre con el botón izquierdo no debe abrir el selector: sólo un
      // clic de verdad (sin desplazamiento).
      const d = state.leftDown;
      state.leftDown = null;
      if (d && Math.abs(ev.clientX - d.x) + Math.abs(ev.clientY - d.y) > 6) return;
      if (settings.displays.mode === 'individual') state.editingMonitor = idx;
      openPickerForMonitor(idx);
    };
    const canvas = el.querySelector('.m-canvas');
    if (!canvas) return;
    // Los gestos van en el monitor completo: la etiqueta central (nombre y
    // resolución) tapa el lienzo y se comía la rueda y el arrastre.
    el.addEventListener('wheel', (ev) => {
      if (ev.target.closest('.m-fit') || ev.target.closest('.m-reset') ||
          ev.target.closest('.m-del')) return;
      ev.preventDefault();
      ev.stopPropagation();
      zoomMonitor(idx, ev.deltaY < 0 ? 1.12 : 1 / 1.12);
    }, { passive: false });
    el.addEventListener('mousedown', (ev) => {
      if (ev.target.closest('.m-fit') || ev.target.closest('.m-reset') ||
          ev.target.closest('.m-del')) return;
      if (settings.displays.mode === 'individual') state.editingMonitor = idx;
      state.leftDown = { x: ev.clientX, y: ev.clientY };
      // Ajustar (panear) sólo con el botón central: el izquierdo selecciona y
      // el derecho abría el menú contextual del navegador.
      if (ev.button !== 1) return;
      ev.preventDefault();
      state.dragging = { idx, x: ev.clientX, y: ev.clientY };
      document.body.classList.add('panning');
    });
    el.addEventListener('contextmenu', (ev) => ev.preventDefault());
  });
  // El botón central no debe disparar el autoscroll del navegador.
  map.addEventListener('auxclick', (ev) => ev.preventDefault());
  map.addEventListener('mousedown', (ev) => {
    if (ev.button === 1) ev.preventDefault();
  });
  map.querySelectorAll('.m-del').forEach((b) => {
    b.onclick = (ev) => { ev.stopPropagation(); removeFromMonitor(Number(b.dataset.del)); };
  });
  // Restablecer encuadre: vuelve a la versión completa, sin zoom ni paneo.
  map.querySelectorAll('.m-reset').forEach((b) => {
    b.onclick = (ev) => {
      ev.stopPropagation();
      const s = monitorSlot(Number(b.dataset.reset));
      s.rect = { x: 0, y: 0, w: 1, h: 1 };
      saveSettings();
      refreshDirty();
      repositionAll();
      toast('Encuadre restablecido');
    };
  });

  // Ajuste por pantalla (sólo en modo individual).
  map.querySelectorAll('.m-fit').forEach((sel) => {
    ['click', 'mousedown', 'wheel'].forEach((ev) =>
      sel.addEventListener(ev, (e) => e.stopPropagation()));
    sel.onchange = (ev) => {
      const i = Number(sel.dataset.fit);
      monitorSlot(i).fit = ev.target.value;
      settings.displays.fit = ev.target.value;
      saveSettings();
      refreshDirty();
      repositionAll();
    };
  });

  // El ajuste global sólo tiene sentido con un único wallpaper (clonar o
  // panorámico); en individual manda el de cada pantalla. El fundido de bucle
  // es común a todo el escritorio compuesto.
  const fitLabel = document.getElementById('fitLabel');
  const fitSel = document.getElementById('fitMode');
  if (fitLabel) fitLabel.style.display = mode === 'individual' ? 'none' : '';
  if (fitSel) fitSel.style.display = mode === 'individual' ? 'none' : '';
  const globalFit = document.getElementById('fitMode');
  if (globalFit && mode !== 'individual') {
    globalFit.value = monitorSlot(prim).fit || settings.displays.fit || 'expand';
  }
  const fadeSel = document.getElementById('fadeMode');
  if (fadeSel) fadeSel.value = String(settings.displays.fade == null ? 0.8 : settings.displays.fade);
  const cloneWrap = document.getElementById('cloneLowWrap');
  if (cloneWrap) cloneWrap.style.display = mode === 'clone' ? '' : 'none';
  const cloneLow = document.getElementById('cloneLow');
  if (cloneLow) cloneLow.checked = !!settings.displays.cloneLow;
  // Al volver a la sección, la barra refleja el estado real (si sabemos qué
  // hay aplicado, o si ya había cambios pendientes).
  if (settings.displays.appliedSig || settings.displays.dirty) refreshDirty();
}

/**
 * Asigna un wallpaper a una pantalla y lo deja pendiente de Aplicar. Es el
 * único camino de asignación: venga del selector de Pantallas, del catálogo o
 * del mapa de monitores.
 */
function assignToSlot(item, idx) {
  const mode = settings.displays.mode;
  const mon = (mode === 'individual') ? (Number(idx) || 0) : primaryIndex();
  const slot = monitorSlot(mon);
  slot.key = item.key;
  slot.type = item.type || '';
  slot.fit = slot.fit || settings.displays.fit || 'expand';
  slot.rect = { x: 0, y: 0, w: 1, h: 1 };
  if (mode === 'none') settings.displays.mode = 'individual';
  // Asignar quita el "sin wallpaper" de esa pantalla.
  settings.displays.none = (settings.displays.none || []).filter((i) => i !== mon);
  saveSettings();
  refreshDirty();
  // El vídeo optimizado se prepara en cuanto entra en el preview, con prioridad.
  prepareSlot(mon, prepMonitorFor(mon));
  // Se muestra Pantallas para que el resultado y el botón Aplicar queden a la
  // vista (si ya estábamos ahí, showSection no hace daño).
  if (typeof showSection === 'function') showSection('displays');
  loadMediaInfo(item.key).then(() => loadDisplays());
}

function openPickerForMonitor(idx) {
  const mode = settings.displays.mode;
  const label = mode === 'individual'
    ? 'pantalla ' + (idx + 1)
    : (mode === 'clone' ? 'todas (clonar)' : 'todas (panorámico)');
  openPicker('Wallpaper para ' + label, (item) => assignToSlot(item, idx));
}

/** El zoom y el encuadre sólo tienen sentido con "Expandir": si el ajuste es
 *  otro, se cambia solo para que la rueda haga lo que el usuario espera. */
function ensureExpandFit(idx, slot) {
  if (slot.fit === 'expand') return;
  slot.fit = 'expand';
  if (settings.displays.mode !== 'individual') settings.displays.fit = 'expand';
  saveSettings();
  const fs = document.querySelector('.m-fit[data-fit="' + idx + '"]');
  if (fs) fs.value = 'expand';
  const gf = document.getElementById('fitMode');
  if (gf) gf.value = 'expand';
  toast('Ajuste: Expandir · zoom y encuadre');
}

function zoomMonitor(idx, factor) {
  const mode = settings.displays.mode;
  const slot = mode === 'individual' ? monitorSlot(idx) : monitorSlot(primaryIndex());
  if (!slot.key) return;
  ensureExpandFit(mode === 'individual' ? idx : primaryIndex(), slot);
  const r = slot.rect || { x: 0, y: 0, w: 1, h: 1 };
  const cx = r.x + r.w / 2;
  const cy = r.y + r.h / 2;
  let nw = Math.min(1, Math.max(0.1, r.w / factor));
  let nh = Math.min(1, Math.max(0.1, r.h / factor));
  if (nw > 1) nw = 1;
  if (nh > 1) nh = 1;
  const nx = Math.min(Math.max(0, cx - nw / 2), 1 - nw);
  const ny = Math.min(Math.max(0, cy - nh / 2), 1 - nh);
  slot.rect = { x: nx, y: ny, w: nw, h: nh };
  refreshDirty();
  // El vídeo ya no es el que se envió a preparar: se cancela ese trabajo.
  cancelPrepThrottled(mode === 'individual' ? idx : primaryIndex());
  // Sin re-renderizar: recrear el elemento provocaba el destello del tamaño
  // original mientras volvía a cargar.
  repositionAll();
}

function panMonitor(dxPx, dyPx, constrain) {
  if (!state.dragging) return;
  const idx = state.dragging.idx;
  const mode = settings.displays.mode;
  const slot = mode === 'individual' ? monitorSlot(idx) : monitorSlot(primaryIndex());
  if (!slot.key) return;
  ensureExpandFit(mode === 'individual' ? idx : primaryIndex(), slot);
  const info = mediaInfo(slot.key);
  let bw, bh;
  if (mode === 'expand') {
    // En panorámico el encuadre se mueve sobre el escritorio completo.
    const list = state.monitors || [];
    const minX = Math.min(...list.map((m) => m.x));
    const minY = Math.min(...list.map((m) => m.y));
    bw = Math.max(...list.map((m) => m.x + m.width)) - minX;
    bh = Math.max(...list.map((m) => m.y + m.height)) - minY;
  } else {
    const mon = (state.monitors || []).find((m) => Number(m.index) === idx) ||
                { width: 1920, height: 1080 };
    bw = mon.width;
    bh = mon.height;
  }
  const r = slot.rect || { x: 0, y: 0, w: 1, h: 1 };
  const sc = Math.max(bw / (r.w * (info.w || 1)), bh / (r.h * (info.h || 1)));
  let nx = r.x - dxPx / sc / (info.w || 1);
  let ny = r.y - dyPx / sc / (info.h || 1);
  if (constrain) {
    // Con Ctrl el encuadre no sale del vídeo: sin bandas negras.
    nx = Math.min(Math.max(0, nx), Math.max(0, 1 - r.w));
    ny = Math.min(Math.max(0, ny), Math.max(0, 1 - r.h));
  } else {
    // Sin Ctrl se puede sacar del borde y aparecen bandas negras; se deja
    // siempre algo de imagen a la vista para no perderlo.
    nx = Math.min(Math.max(0.03 - r.w, nx), 0.97);
    ny = Math.min(Math.max(0.03 - r.h, ny), 0.97);
  }
  slot.rect = { x: nx, y: ny, w: r.w, h: r.h };
  refreshDirty();
  if (!state.dragging.cancelled) {
    state.dragging.cancelled = true;
    cancelPrepThrottled(mode === 'individual' ? idx : primaryIndex());
  }
  repositionAll();
}

// La barra de Aplicar está siempre visible: es el único botón del editor y
// desaparecer dejaba al usuario sin forma de aplicar lo que acababa de elegir.
function markDirty() {
  settings.displays.dirty = true;
  const bar = document.getElementById('applyBar');
  const btn = document.getElementById('applyNow');
  const cancel = document.getElementById('applyCancel');
  if (bar) bar.hidden = false;
  if (btn) btn.disabled = false;
  if (cancel) cancel.disabled = false;
  const txt = document.getElementById('applyText');
  if (txt) txt.textContent = 'Cambios sin aplicar en el escritorio.';
}

function clearDirty() {
  settings.displays.dirty = false;
  const bar = document.getElementById('applyBar');
  const btn = document.getElementById('applyNow');
  const cancel = document.getElementById('applyCancel');
  if (bar) bar.hidden = false;
  if (btn) btn.disabled = true;
  if (cancel) cancel.disabled = true;
  const txt = document.getElementById('applyText');
  if (txt) txt.textContent = 'Escritorio aplicado. Elige otro wallpaper para cambiarlo.';
}

function aspectOf(key) {
  const i = mediaInfo(key);
  if (!i.w || !i.h) return 16 / 9;
  return i.w / i.h;
}

/** Duración del fundido de bucle (segundos). 0 = sin fundido. */
function fadeValue() {
  const v = Number(settings.displays.fade);
  return isFinite(v) ? v : 0.8;
}

/** Parámetros de la celda de un monitor (para prepararla en segundo plano). */
function cellParamsOf(m, slot) {
  const r = slot.rect || { x: 0, y: 0, w: 1, h: 1 };
  return {
    key: slot.key,
    fit: slot.fit || settings.displays.fit || 'expand',
    cx: r.x, cy: r.y, cw: r.w, ch: r.h,
    w: m.width, h: m.height,
    aspect: aspectOf(slot.key)
  };
}

function prepareSlot(slotIndex, m) {
  const slot = monitorSlot(slotIndex);
  if (!slot.key || !m) return;
  const body = Object.assign({ slot: String(slotIndex), front: true },
                             cellParamsOf(m, slot));
  state.prep = state.prep || {};
  state.prep[slotIndex] = { state: 'queued', percent: 0 };
  updatePrepBadges();
  fetch('/api/prepare', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  }).then((r) => (r.ok ? r.json() : null)).then((j) => {
    if (!j) return;
    if (j.cached) state.prep[slotIndex] = { state: 'ready', percent: 100 };
    else if (j.skip) state.prep[slotIndex] = { state: 'none' };
    updatePrepBadges();
  }).catch(() => {});
}

function cancelPrep(slotIndex) {
  fetch('/api/prepare/cancel?slot=' + encodeURIComponent(String(slotIndex)))
    .catch(() => {});
}

/** Cancela la preparación de una celda sin saturar (rueda/arrastre continuo). */
function cancelPrepThrottled(slotIndex) {
  state.cancelAt = state.cancelAt || {};
  const now = Date.now();
  if (now - (state.cancelAt[slotIndex] || 0) < 600) return;
  state.cancelAt[slotIndex] = now;
  cancelPrep(slotIndex);
  state.prep = state.prep || {};
  state.prep[slotIndex] = { state: 'stale', percent: 0 };
  updatePrepBadges();
}

/** Estado de la preparación de cada celda, debajo de cada monitor. */
function updatePrepBadges() {
  document.querySelectorAll('.m-prep').forEach((el) => {
    const i = el.dataset.prep;
    const p = (state.prep || {})[i];
    if (!p) { el.textContent = ''; return; }
    if (p.state === 'ready') el.textContent = '✓ optimizado';
    else if (p.state === 'queued') el.textContent = '⌛ en cola';
    else if (p.state === 'running') el.textContent = '⌛ ' + p.percent + ' %';
    else if (p.state === 'stale') el.textContent = '⚠ sin preparar';
    else el.textContent = '';
  });
}

/** Lee la cola del servidor y refleja el avance por monitor. */
function pollPrep() {
  const sec = document.getElementById('sec-displays');
  if (!sec || !sec.classList.contains('active')) return;
  api('/api/prepare/status').then((s) => {
    const jobs = (s && s.jobs) || [];
    if (!jobs.length) return;
    state.prep = state.prep || {};
    jobs.forEach((j) => {
      if (j.cancelled) return;
      state.prep[j.slot] = j.done
        ? (j.ok ? { state: 'ready', percent: 100 }
                : { state: 'stale', percent: 0 })
        : { state: 'running', percent: j.percent || 0 };
    });
    updatePrepBadges();
  }).catch(() => {});
}

/**
 * Firma del plan: todo lo que determina el vídeo resultante. Si no cambia,
 * no hay nada que regenerar (volver al mismo encuadre no es un cambio).
 */
function planSignature(plan) {
  if (!plan) return '';
  const r4 = (v) => Math.round((Number(v) || 0) * 10000);
  const cells = (plan.cells || []).map((c) => [
    c.key, c.fit, r4(c.cx), r4(c.cy), r4(c.cw), r4(c.ch), c.x, c.y, c.w, c.h
  ]);
  cells.sort((a, b) => String(a).localeCompare(String(b)));
  const pan = plan.panorama
    ? [plan.panorama.key, plan.panorama.fit, r4(plan.panorama.cx),
       r4(plan.panorama.cy), r4(plan.panorama.cw), r4(plan.panorama.ch)]
    : null;
  return JSON.stringify({
    mode: settings.displays.mode,
    black: !!plan.black,
    fade: r4(plan.fade || 0),
    outH: plan.outH || 0,
    pan,
    cells
  });
}

/** Marca o limpia "cambios sin aplicar" comparando con lo último aplicado. */
function refreshDirty() {
  const plan = buildPlan();
  if (!plan) { clearDirty(); return; }
  const sig = planSignature(plan);
  if (settings.displays.appliedSig && sig && sig === settings.displays.appliedSig) {
    clearDirty();
  } else {
    markDirty();
  }
}

/** Monitor que corresponde a la celda que se prepara según el modo. */
function prepMonitorFor(idx) {
  const list = state.monitors || [];
  if (settings.displays.mode === 'individual') {
    return list.find((m) => Number(m.index) === idx) || null;
  }
  if (settings.displays.mode === 'clone') {
    return list.find((m) => Number(m.index) === primaryIndex()) || null;
  }
  return null;  // panorámico: lo resuelve la composición
}

/** Plan de composición: una celda por monitor con su recorte. */
function buildPlan() {
  const mode = settings.displays.mode;
  const list = state.monitors || [];
  if (!list.length) return null;
  const minX = Math.min(...list.map((m) => m.x));
  const minY = Math.min(...list.map((m) => m.y));
  const canvasW = Math.max(...list.map((m) => m.x + m.width)) - minX;
  const canvasH = Math.max(...list.map((m) => m.y + m.height)) - minY;
  if (mode === 'none') {
    // Negro a tamaño del escritorio: equivale a quitar el fondo.
    return { canvasW, canvasH, maxSizeMB: 4, black: true };
  }
  const prim = primaryIndex();
  const baseSlot = monitorSlot(prim);
  if (mode === 'expand') {
    // Panorámico: un solo vídeo mapeado al escritorio virtual completo. El
    // servidor hace el mismo mapeo (cover/stretch/contain + recorte).
    if (!baseSlot.key) return null;
    const r = baseSlot.rect || { x: 0, y: 0, w: 1, h: 1 };
    return {
      canvasW, canvasH, maxSizeMB: 16, fade: fadeValue(),
      panorama: {
        key: baseSlot.key,
        fit: baseSlot.fit || settings.displays.fit || 'expand',
        cx: r.x, cy: r.y, cw: r.w, ch: r.h,
        aspect: aspectOf(baseSlot.key)
      }
    };
  }
  const cells = [];
  list.forEach((m) => {
    let slot, cx, cy, cw, ch;
    if (mode === 'individual') {
      slot = monitorSlot(Number(m.index));
      if (!slot.key) return;
      const r = slot.rect || { x: 0, y: 0, w: 1, h: 1 };
      cx = r.x; cy = r.y; cw = r.w; ch = r.h;
    } else {
      // clonar: el mismo vídeo y el mismo encuadre en todos.
      slot = baseSlot;
      if (!slot.key) return;
      const r = slot.rect || { x: 0, y: 0, w: 1, h: 1 };
      cx = r.x; cy = r.y; cw = r.w; ch = r.h;
    }
    cells.push({
      key: slot.key,
      x: m.x - minX, y: m.y - minY, w: m.width, h: m.height,
      cx, cy, cw, ch, fit: slot.fit || settings.displays.fit || 'expand',
      aspect: aspectOf(slot.key)
    });
  });
  if (!cells.length) return null;
  const plan = { canvasW, canvasH, maxSizeMB: 16, fade: fadeValue(), cells };
  if (mode === 'clone' && settings.displays.cloneLow) {
    // Clonar a la resolución del monitor más pequeño: menos disco y CPU.
    const minH = Math.min(...list.map((m) => m.height));
    if (minH > 0 && minH < canvasH) plan.outH = minH;
  }
  return plan;
}

async function applyWorkspace() {
  const plan = buildPlan();
  const status = document.getElementById('applyText');
  const btn = document.getElementById('applyNow');
  const track = document.getElementById('applyTrack');
  const fill = document.getElementById('applyFill');
  if (!plan) { toast('No hay nada que aplicar'); return; }
  const sig = planSignature(plan);
  // Si el escritorio ya es exactamente este, no se genera nada de nuevo.
  if (settings.displays.appliedSig && sig === settings.displays.appliedSig) {
    toast('Ese escritorio ya está aplicado');
    clearDirty();
    return;
  }
  btn.disabled = true;
  if (track) { track.hidden = false; fill.style.width = '0%'; }
  status.textContent = 'Generando el escritorio optimizado…';
  status.title = '';
  // La composición se hace en el servidor; se consulta su avance para que la
  // barra diga cuánto queda (decodificar 4K y componer tarda).
  const timer = setInterval(() => {
    api('/api/progress').then((p) => {
      if (!p || !p.active) return;
      const pc = Math.max(1, Math.min(99, p.percent || 0));
      if (fill) fill.style.width = pc + '%';
      status.textContent = 'Generando el escritorio… ' + pc + ' %';
    }).catch(() => {});
  }, 400);
  try {
    const res = await fetch('/api/compose', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(plan)
    }).then((r) => r.json());
    if (!res.ok) {
      const det = res.detail ? String(res.detail).replace(/\s+/g, ' ').trim() : '';
      throw new Error((res.error || 'error') + (det ? ': ' + det : ''));
    }
    // Se recuerda exactamente qué escritorio está aplicado: si se vuelve a
    // ese mismo estado, Aplicar no regenera nada.
    settings.displays.appliedSig = sig;
    settings.displays.appliedKey = res.key || '';
    saveSettings();
    if (fill) fill.style.width = '100%';
    status.textContent = res.mode === 'displays'
      ? 'Aplicado: cada pantalla reproduce su propio vídeo'
      : 'Aplicado';
    toast('Escritorio aplicado');
    clearDirty();
    loadDisplays();
  } catch (e) {
    const msg = (e && e.message) ? e.message : String(e);
    status.textContent = 'Error: ' + msg;
    status.title = msg;
    if (fill) fill.style.width = '0%';
    btn.disabled = false;
  } finally {
    clearInterval(timer);
    if (track) setTimeout(() => { track.hidden = true; }, 600);
  }
}

function loadDisplays() {
  Promise.all([api('/api/monitors').catch(() => ({ monitors: [] })),
               api('/api/current').catch(() => ({}))])
    .then(([mons, cur]) => {
      const list = (mons.monitors || []).slice().sort((a, b) => a.x - b.x || a.y - b.y);
      // Si el motor está recomponiendo el escritorio, la enumeración puede
      // venir vacía: se conserva la última lista buena en vez de vaciar el mapa.
      if (list.length) state.monitors = list;
      else toast('No se pudieron leer las pantallas');
      // Si el escritorio está rotando (playlist), no hay un "aplicado" fijo.
      if (cur.playlist) {
        settings.displays.appliedSig = '';
        settings.displays.appliedKey = '';
        const lg = document.getElementById('monitorLegend');
        if (lg) lg.textContent = 'Rotación activa · al Aplicar se detiene.';
      } else {
        const lg = document.getElementById('monitorLegend');
        if (lg) lg.textContent = '';
      }
      // Si el fondo actual no es el que aplicamos nosotros (p. ej. desde el
      // móvil), no sabemos a qué estado corresponde: se olvida la firma.
      if (settings.displays.appliedKey && cur.key &&
          cur.key !== settings.displays.appliedKey) {
        settings.displays.appliedSig = '';
        settings.displays.appliedKey = '';
      }
      if (cur.key && !Object.keys(dispState()).length &&
          !String(cur.key).startsWith('optimized:')) {
        // Sólo se hereda un wallpaper real: `optimized:` es el escritorio
        // compuesto que generamos, no una fuente.
        monitorSlot(primaryIndex()).key = cur.key;
      }
      // Se pinta ya con lo que haya (miniatura / WebP en segundo plano); el
      // sondeo de dimensiones no debe retrasar la vista.
      renderWorkspace(state.monitors || []);
      Object.keys(dispState()).forEach((k) => {
        const s = monitorSlot(k);
        if (s.key) loadMediaInfo(s.key);
      });
    })
    .catch(() => toast('No se pudieron leer las pantallas'));
}

function removeFromMonitor(index) {
  monitorSlot(Number(index)).key = '';
  saveSettings();
  refreshDirty();
  loadDisplays();
}

(function initDisplaysV2() {
  const note = document.getElementById('displaysNote');
  if (note) note.style.display = 'none';
  // Si el navegador bloqueó algún autoplay, el primer clic/tecla lo desbloquea.
  const kickAll = () => {
    document.querySelectorAll('#monitorMap video').forEach((v) => {
      const p = v.play();
      if (p && p.catch) p.catch(() => {});
    });
  };
  ['pointerdown', 'keydown'].forEach((ev) =>
    window.addEventListener(ev, kickAll, { passive: true }));
  window.addEventListener('mousemove', (e) => {
    if (!state.dragging) return;
    const dx = e.clientX - state.dragging.x;
    const dy = e.clientY - state.dragging.y;
    state.dragging.x = e.clientX;
    state.dragging.y = e.clientY;
    // Ctrl: el encuadre no sale del vídeo (sin bandas negras).
    panMonitor(dx, dy, e.ctrlKey);
  });
  window.addEventListener('mouseup', () => {
    state.dragging = null;
    document.body.classList.remove('panning');
  });
  // Evita que arrastrar seleccione texto o elementos de la página.
  document.addEventListener('selectstart', (e) => {
    if (state.dragging) e.preventDefault();
  });
  document.addEventListener('dragstart', (e) => {
    if (state.dragging) e.preventDefault();
  });
  window.addEventListener('resize', () => { repositionAll(); });

  $$('[data-dmode]').forEach((b) => {
    b.onclick = () => {
      settings.displays.mode = b.dataset.dmode;
      $$('[data-dmode]').forEach((x) =>
        x.classList.toggle('active', x.dataset.dmode === settings.displays.mode));
      saveSettings();
      // El modo forma parte del resultado: la barra se recalcula comparando
      // con lo aplicado (volver al mismo modo deja de marcar cambios).
      refreshDirty();
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
    const slot = slotForEdit();
    if (slot) slot.fit = e.target.value;
    saveSettings();
    refreshDirty();
    repositionAll();
  };
  if ($('#fadeMode')) $('#fadeMode').onchange = (e) => {
    settings.displays.fade = Number(e.target.value) || 0;
    saveSettings();
    refreshDirty();
    toast(settings.displays.fade > 0
      ? 'Bucle con fundido de ' + String(settings.displays.fade).replace('.', ',') + ' s'
      : 'Bucle sin fundido');
  };
  if ($('#cloneLow')) $('#cloneLow').onchange = (e) => {
    settings.displays.cloneLow = e.target.checked;
    saveSettings();
    refreshDirty();
    toast(e.target.checked
      ? 'Clonar a la resolución del monitor más pequeño'
      : 'Clonar a resolución nativa del escritorio');
  };
  if ($('#applyNow')) $('#applyNow').onclick = () => applyWorkspace();
  if ($('#applyCancel')) $('#applyCancel').onclick = () => { clearDirty(); loadDisplays(); };
  if ($('#optOpen')) $('#optOpen').style.display = 'none';
  const optModal = document.getElementById('optimize');
  if (optModal) optModal.hidden = true;

  clearDirty();
  loadDisplays();
  // Refresca el avance de la preparación de celdas mientras se ve la sección.
  setInterval(pollPrep, 1500);
})();
