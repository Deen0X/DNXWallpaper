'use strict';

const $ = (s) => document.querySelector(s);
const state = {
  file: '',
  info: null,
  rect: { x: 0, y: 0, w: 1, h: 1 },
  target: 1080,
  quality: 3,
  fade: 0,
  square: false,
  job: null,
  lan: null,
  polling: false,
};

function setStatus(text, err = false) {
  const el = $('#status');
  el.textContent = text || '';
  el.style.color = err ? 'var(--danger)' : 'var(--muted)';
}

function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }
function even(v) { return Math.max(2, Math.round(v / 2) * 2); }
function aspectValue() {
  const sel = $('#aspect').value;
  if (sel === 'custom') {
    const w = parseFloat($('#aw').value) || 9;
    const h = parseFloat($('#ah').value) || 19.5;
    return w / h;
  }
  const [w, h] = sel.split(':').map(Number);
  return w / h;
}

function baseName(p) {
  const s = String(p || '').replace(/\\/g, '/');
  const n = s.substring(s.lastIndexOf('/') + 1);
  const d = n.lastIndexOf('.');
  return d > 0 ? n.substring(0, d) : n;
}

function fitBox() {
  const box = $('#videoBox');
  const outer = document.querySelector('.video-outer');
  if (!state.info) {
    box.style.width = '100%';
    box.style.height = '240px';
    return;
  }
  const maxW = Math.max(200, outer.clientWidth - 8);
  const maxH = Math.max(200, outer.clientHeight - 8);
  const scale = Math.min(maxW / state.info.width, maxH / state.info.height);
  box.style.width = Math.round(state.info.width * scale) + 'px';
  box.style.height = Math.round(state.info.height * scale) + 'px';
  renderRect();
}

function switchTab(name) {
  document.querySelectorAll('.tab').forEach(t => {
    t.classList.toggle('active', t.dataset.tab === name);
  });
  document.querySelectorAll('.tab-content').forEach(c => {
    c.classList.toggle('active', c.id === 'tab-' + name);
  });
  if (name === 'config') fitBox();
}

function renderRect() {
  const r = state.rect;
  const el = $('#rect');
  el.style.left = (r.x * 100) + '%';
  el.style.top = (r.y * 100) + '%';
  el.style.width = (r.w * 100) + '%';
  el.style.height = (r.h * 100) + '%';
  updateOut();
}

function updateOut() {
  const info = state.info;
  if (!info) { $('#outInfo').textContent = '—'; return; }
  const r = state.rect;
  const cw = even(r.w * info.width);
  const ch = even(r.h * info.height);
  const target = parseInt($('#target').value, 10) || 1080;
  const short = Math.min(cw, ch);
  const scale = short > target ? target / short : 1;
  const ow = even(cw * scale);
  const oh = even(ch * scale);
  const dur = state.fade > 0 ? Math.max(0.1, info.duration - state.fade) : info.duration;
  $('#outInfo').textContent =
    `Salida ~ ${ow}×${oh} · recorte ${cw}×${ch} · ${dur.toFixed(1)} s`;
}

function applyAspect() {
  if (!state.info) return;
  const aS = state.info.width / state.info.height;
  const aT = aspectValue();
  let r;
  if (aT < aS) {
    const w = aT / aS;
    r = { x: (1 - w) / 2, y: 0, w, h: 1 };
  } else {
    const h = aS / aT;
    r = { x: 0, y: (1 - h) / 2, w: 1, h };
  }
  state.rect = r;
  renderRect();
}

function setAspectFromOrientation() {
  if ($('#square').checked) {
    $('#aspect').value = '1:1';
  } else {
    $('#aspect').value = $('#orientation').value === 'landscape' ? '16:9' : '9:19.5';
  }
  $('#customAspect').classList.toggle('hidden', $('#aspect').value !== 'custom');
  applyAspect();
}

async function api(url, opts) {
  const res = await fetch(url, opts);
  const text = await res.text();
  try { return JSON.parse(text); } catch (_) { return { ok: false, error: text }; }
}

async function loadLan() {
  try { state.lan = await api('/api/editor/lan'); } catch (_) { state.lan = null; }
}

async function loadSource(file) {
  if (!file) { setStatus('Indica una ruta de vídeo.', true); return; }
  state.file = file;
  $('#filePath').value = file;
  setStatus('Leyendo vídeo…');
  const info = await api('/api/editor/info?file=' + encodeURIComponent(file));
  if (!info.ok) {
    setStatus('No se pudo leer el vídeo: ' + (info.error || 'desconocido'), true);
    return;
  }
  state.info = info;
  $('#sourceInfo').textContent =
    `${info.width}×${info.height} · ${Number(info.duration).toFixed(1)} s · ${info.hdr ? 'HDR' : 'SDR'}`;
  if (!$('#title').value) $('#title').value = baseName(file);
  const v = $('#video');
  v.src = '/api/editor/file?file=' + encodeURIComponent(file);
  v.play().catch(() => {});
  fitBox();
  applyAspect();
  $('#btnExport').disabled = false;
  setStatus('');
  $('#result').classList.add('hidden');
  $('#emptyResult').classList.remove('hidden');
  switchTab('config');
}

async function ensureLan() {
  if (!state.lan || !state.lan.ip) await loadLan();
}

function showResult(id, info) {
  ensureLan().then(() => {
    const ip = (state.lan && state.lan.ip) || location.hostname;
    const port = (state.lan && state.lan.port) || location.port;
    const url = `http://${ip}:${port}/m/${id}`;
    $('#resultUrl').value = url;
    const v = $('#resultVideo');
    v.src = `/m/${id}?inline=1`;
    v.play().catch(() => {});
    $('#resultInfo').textContent =
      `${info.width}×${info.height} · ${(info.size / 1048576).toFixed(1)} MB · ${Number(info.duration).toFixed(1)} s`;
    const qr = $('#qr');
    qr.innerHTML = '';
    try {
      new QRCode(qr, {
        text: url, width: 136, height: 136,
        correctLevel: QRCode.CorrectLevel.M
      });
    } catch (_) {}
    $('#result').classList.remove('hidden');
    $('#emptyResult').classList.add('hidden');
    switchTab('result');
    $('#btnCancel').classList.add('hidden');
    $('#btnExport').disabled = false;
    setStatus('Vídeo generado. Escanea el QR en el móvil.');
  });
}

async function poll() {
  if (!state.job || state.polling) return;
  state.polling = true;
  while (state.job) {
    const st = await api('/api/editor/status/' + encodeURIComponent(state.job));
    if (!st.ok) {
      setStatus('No se pudo consultar el estado.', true);
      break;
    }
    $('#bar').style.width = (st.percent || 0) + '%';
    if (st.state === 'running') {
      setStatus(`Generando… ${st.percent || 0} %`);
      await new Promise(r => setTimeout(r, 600));
      continue;
    }
    if (st.state === 'done') {
      showResult(st.id, st);
      state.job = null;
      break;
    }
    setStatus('Error: ' + (st.error || 'desconocido'), true);
    state.job = null;
    $('#btnCancel').classList.add('hidden');
    $('#btnExport').disabled = false;
    break;
  }
  state.polling = false;
}

async function exportVideo() {
  if (!state.info || !state.file) return;
  await ensureLan();
  const body = {
    file: state.file,
    title: $('#title').value || baseName(state.file),
    device: $('#device').value || 'Dispositivo',
    rect: state.rect,
    target: parseInt($('#target').value, 10) || 1080,
    quality: parseInt($('#quality').value, 10) || 3,
    fade: parseFloat($('#fade').value) || 0,
    square: $('#square').checked,
  };
  setStatus('Preparando exportación…');
  const res = await api('/api/editor/export', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    setStatus('No se pudo iniciar: ' + (res.error || 'desconocido'), true);
    return;
  }
  state.job = res.id;
  $('#bar').style.width = '0%';
  switchTab('config');
  $('#result').classList.add('hidden');
  $('#btnExport').disabled = true;
  $('#btnCancel').classList.remove('hidden');
  poll();
}

function drag(e, dir) {
  if (!state.info) return;
  e.preventDefault();
  const box = $('#videoBox').getBoundingClientRect();
  const start = { ...state.rect };
  const sx = e.clientX, sy = e.clientY;
  const k = (state.info.width / state.info.height) / aspectValue();
  const minW = 0.02;
  const snap = (v) => {
    if (!(e.ctrlKey || e.shiftKey || e.altKey)) return v;
    if (Math.abs(v) < 0.02) return 0;
    if (Math.abs(v - 1) < 0.02) return 1;
    return v;
  };
  const onMove = (ev) => {
    const dx = (ev.clientX - sx) / box.width;
    const dy = (ev.clientY - sy) / box.height;
    let r = { ...start };
    if (dir === 'move') {
      r.x = clamp(snap(start.x + dx), 0, 1 - r.w);
      r.y = clamp(snap(start.y + dy), 0, 1 - r.h);
    } else {
      let w = start.w, h = start.h, x = start.x, y = start.y;
      const west = dir.includes('w');
      const east = dir.includes('e');
      const north = dir.includes('n');
      const south = dir.includes('s');
      if (east) w = start.w + dx;
      if (west) w = start.w - dx;
      if (north || south) {
        if (east || west) {
          h = w * k;
        } else {
          h = south ? start.h + dy : start.h - dy;
          w = h / k;
        }
      } else {
        h = w * k;
      }
      w = clamp(w, minW, 1);
      h = clamp(h, minW, 1);
      if (h / k > 1) { h = k; w = 1; }
      if (w * k > 1) { w = 1 / k; h = 1; }
      if (west) x = start.x + start.w - w;
      if (north) y = start.y + start.h - h;
      if (!west && !east) x = start.x + (start.w - w) / 2;
      if (!north && !south) y = start.y + (start.h - h) / 2;
      x = clamp(x, 0, 1 - w);
      y = clamp(y, 0, 1 - h);
      r = { x, y, w, h };
    }
    state.rect = r;
    renderRect();
  };
  const onUp = () => {
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', onUp);
    document.body.style.cursor = '';
  };
  window.addEventListener('pointermove', onMove);
  window.addEventListener('pointerup', onUp);
}

$('#rect').addEventListener('pointerdown', (e) => {
  if (e.target !== e.currentTarget) return;
  drag(e, 'move');
});
document.querySelectorAll('.h').forEach(h => {
  h.addEventListener('pointerdown', (e) => { e.stopPropagation(); drag(e, h.dataset.dir); });
});

$('#btnCurrent').addEventListener('click', async () => {
  const cur = await api('/api/editor/current');
  if (!cur.ok) { setStatus('No hay un fondo actual: ' + (cur.error || ''), true); return; }
  loadSource(cur.file);
});
$('#btnLoad').addEventListener('click', () => loadSource($('#filePath').value.trim()));
$('#btnExport').addEventListener('click', exportVideo);
$('#btnCancel').addEventListener('click', async () => {
  if (!state.job) return;
  await api('/api/editor/cancel/' + encodeURIComponent(state.job), { method: 'POST' });
  setStatus('Cancelado.');
  state.job = null;
  $('#btnCancel').classList.add('hidden');
  $('#btnExport').disabled = false;
});
$('#btnCopy').addEventListener('click', async () => {
  const u = $('#resultUrl').value;
  try { await navigator.clipboard.writeText(u); setStatus('URL copiada.'); }
  catch (_) { window.prompt('Copia la URL', u); }
});
$('#btnOpen').addEventListener('click', () => window.open($('#resultUrl').value, '_blank'));

$('#orientation').addEventListener('change', setAspectFromOrientation);
$('#aspect').addEventListener('change', () => {
  $('#customAspect').classList.toggle('hidden', $('#aspect').value !== 'custom');
  if ($('#aspect').value !== 'custom') applyAspect();
});
$('#btnAspect').addEventListener('click', () => {
  $('#aspect').value = 'custom';
  applyAspect();
});
$('#square').addEventListener('change', setAspectFromOrientation);
$('#target').addEventListener('change', updateOut);
$('#quality').addEventListener('change', updateOut);
$('#fade').addEventListener('input', () => {
  state.fade = parseFloat($('#fade').value) || 0;
  $('#fadeVal').textContent = state.fade.toFixed(2);
  updateOut();
});
$('#video').addEventListener('loadedmetadata', fitBox);
document.querySelectorAll('.tab').forEach(t => {
  t.addEventListener('click', () => switchTab(t.dataset.tab));
});
window.addEventListener('resize', () => { fitBox(); renderRect(); });

(async function init() {
  await loadLan();
  const params = new URLSearchParams(location.search);
  const requested = params.get('file');
  if (requested) {
    loadSource(requested);
    return;
  }
  const cur = await api('/api/editor/current');
  if (cur.ok) {
    loadSource(cur.file);
  } else {
    setStatus('Carga el fondo actual o indica una ruta.');
  }
})();
