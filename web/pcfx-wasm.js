const STORAGE_CONFIG = 'pcfx.wasm.config.v1';
const STORAGE_STATE = 'pcfx.wasm.savestate.slot0.v1';

const BUTTONS = [
  ['A', 1 << 0], ['B', 1 << 1], ['C', 1 << 2],
  ['X', 1 << 3], ['Y', 1 << 4], ['Z', 1 << 5],
  ['Select', 1 << 6], ['Start', 1 << 7],
  ['Up', 1 << 8], ['Right', 1 << 9], ['Down', 1 << 10], ['Left', 1 << 11],
];

const DEFAULT_KEYS = {
  A: 'KeyZ', B: 'KeyX', C: 'KeyC',
  X: 'KeyA', Y: 'KeyS', Z: 'KeyD',
  Select: 'ShiftRight', Start: 'Enter',
  Up: 'ArrowUp', Right: 'ArrowRight', Down: 'ArrowDown', Left: 'ArrowLeft'
};

const STATUS_TEXT = {
  0: 'waiting for BIOS',
  1: 'BIOS loaded',
  2: 'media ready',
  3: 'running',
  4: 'paused'
};

const els = {
  canvas: document.getElementById('video'),
  screenFrame: document.getElementById('screenFrame'),
  hud: document.getElementById('hud'),
  modal: document.getElementById('startupModal'),
  openStartup: document.getElementById('openStartup'),
  pauseToggle: document.getElementById('pauseToggle'),
  saveState: document.getElementById('saveState'),
  loadState: document.getElementById('loadState'),
  clearStorage: document.getElementById('clearStorage'),
  biosFile: document.getElementById('biosFile'),
  biosName: document.getElementById('biosName'),
  startButton: document.getElementById('startButton'),
  mediaFile: document.getElementById('mediaFile'),
  mediaName: document.getElementById('mediaName'),
  controlMap: document.getElementById('controlMap'),
  storageStatus: document.getElementById('storageStatus'),
  runtimeSystem: document.getElementById('runtimeSystem'),
  runtimeStatus: document.getElementById('runtimeStatus'),
  runtimeResolution: document.getElementById('runtimeResolution'),
  runtimeFrame: document.getElementById('runtimeFrame'),
};

let wasm;
let memory;
let ctx = els.canvas.getContext('2d', { alpha: false });
let imageData = null;
let rgba = null;
let config = loadConfig();
let pressed = new Set();
let remapTarget = null;
let biosPending = null;
let paused = false;
let mouseX = 0;
let mouseY = 0;
let mouseButtons = 0;

function loadConfig() {
  try {
    const raw = localStorage.getItem(STORAGE_CONFIG);
    if (raw) {
      const saved = JSON.parse(raw);
      return {
        systemMode: saved.systemMode === 'pcfxga' ? 'pcfxga' : 'pcfx',
        keys: { ...DEFAULT_KEYS, ...(saved.keys || {}) },
      };
    }
  } catch (_) {}
  return { systemMode: 'pcfx', keys: { ...DEFAULT_KEYS } };
}

function saveConfig() {
  localStorage.setItem(STORAGE_CONFIG, JSON.stringify(config));
  updateStorageStatus('configuration saved', 'ok');
}

function updateStorageStatus(text, tone = 'muted') {
  els.storageStatus.textContent = text;
  els.storageStatus.className = tone === 'ok' ? 'status-ok' : tone === 'bad' ? 'status-bad' : tone === 'warn' ? 'status-warn' : 'muted';
}

function systemModeValue() {
  return config.systemMode === 'pcfxga' ? 1 : 0;
}

function selectedSystemFromDom() {
  return document.querySelector('input[name="systemMode"]:checked')?.value === 'pcfxga' ? 'pcfxga' : 'pcfx';
}

function setSystemMode(mode) {
  config.systemMode = mode === 'pcfxga' ? 'pcfxga' : 'pcfx';
  document.querySelectorAll('input[name="systemMode"]').forEach(r => r.checked = (r.value === config.systemMode));
  if (wasm?.pcfx_wasm_set_system_mode) wasm.pcfx_wasm_set_system_mode(systemModeValue());
  saveConfig();
  renderOnce();
}

async function instantiateBackend() {
  let result;
  try {
    result = await WebAssembly.instantiateStreaming(fetch('pcfx_wasm_core.wasm'), {});
  } catch (e) {
    const bytes = await (await fetch('pcfx_wasm_core.wasm')).arrayBuffer();
    result = await WebAssembly.instantiate(bytes, {});
  }
  wasm = result.instance.exports;
  memory = wasm.memory;
  wasm.pcfx_wasm_init(systemModeValue());
  renderOnce();
  requestAnimationFrame(tick);
}

function wasmU8() {
  return new Uint8Array(memory.buffer);
}

function copyBytesToWasm(bytes) {
  const ptr = wasm.pcfx_wasm_malloc(bytes.byteLength);
  if (!ptr) throw new Error('wasm heap allocation failed');
  wasmU8().set(bytes, ptr);
  return ptr;
}

async function readFirstBytes(file, maxBytes) {
  const slice = file.slice(0, Math.min(file.size, maxBytes));
  return new Uint8Array(await slice.arrayBuffer());
}

function mediaKindForName(name) {
  const n = name.toLowerCase();
  if (n.endsWith('.ex') || n.endsWith('.exe')) return 2;
  if (n.endsWith('.cue') || n.endsWith('.ccd')) return 1;
  if (n.endsWith('.chd')) return 3;
  if (n.endsWith('.iso') || n.endsWith('.bin')) return 4;
  if (n.endsWith('.zip')) return 5;
  return 0;
}

function biosKindForMode() {
  return config.systemMode === 'pcfxga' ? 2 : 1;
}

async function loadBiosFile(file) {
  if (!file) return;
  wasm.pcfx_wasm_reset_heap();
  const bytes = new Uint8Array(await file.arrayBuffer());
  const ptr = copyBytesToWasm(bytes);
  const ok = wasm.pcfx_wasm_load_bios(ptr, bytes.byteLength, biosKindForMode());
  if (!ok) throw new Error('BIOS was rejected by wasm backend');
  els.biosName.textContent = `${file.name} (${bytes.byteLength.toLocaleString()} bytes)`;
  biosPending = file;
  updateStorageStatus('BIOS accepted for this session', 'ok');
  renderOnce();
}

async function loadMediaFile(file) {
  if (!file) return;
  const sample = await readFirstBytes(file, 1024 * 1024);
  const ptr = copyBytesToWasm(sample);
  wasm.pcfx_wasm_set_media(ptr, sample.byteLength, file.size >>> 0, mediaKindForName(file.name));
  els.mediaName.textContent = `${file.name} (${file.size.toLocaleString()} bytes)`;
  updateStorageStatus('media metadata loaded', 'ok');
  renderOnce();
}

function buttonsMask() {
  let mask = 0;
  for (const [name, bit] of BUTTONS) {
    if (pressed.has(config.keys[name])) mask |= bit;
  }
  return mask;
}

function resizeCanvasIfNeeded(w, h) {
  if (els.canvas.width !== w || els.canvas.height !== h) {
    els.canvas.width = w;
    els.canvas.height = h;
    imageData = ctx.createImageData(w, h);
    rgba = imageData.data;
  } else if (!imageData) {
    imageData = ctx.createImageData(w, h);
    rgba = imageData.data;
  }
}

function rgb565ToImageData(ptr, w, h) {
  const pixels = new Uint16Array(memory.buffer, ptr, w * h);
  for (let i = 0, j = 0; i < pixels.length; i++, j += 4) {
    const p = pixels[i];
    const r5 = (p >> 11) & 0x1f;
    const g6 = (p >> 5) & 0x3f;
    const b5 = p & 0x1f;
    rgba[j + 0] = (r5 << 3) | (r5 >> 2);
    rgba[j + 1] = (g6 << 2) | (g6 >> 4);
    rgba[j + 2] = (b5 << 3) | (b5 >> 2);
    rgba[j + 3] = 255;
  }
}

function renderOnce() {
  if (!wasm) return;
  const w = wasm.pcfx_wasm_get_width();
  const h = wasm.pcfx_wasm_get_height();
  resizeCanvasIfNeeded(w, h);
  rgb565ToImageData(wasm.pcfx_wasm_get_framebuffer(), w, h);
  ctx.putImageData(imageData, 0, 0);
  const status = wasm.pcfx_wasm_get_status();
  const frame = wasm.pcfx_wasm_get_frame_count();
  els.hud.textContent = `${config.systemMode.toUpperCase()} ${w}x${h} ${STATUS_TEXT[status] || status} frame ${frame}`;
  els.runtimeSystem.textContent = config.systemMode.toUpperCase();
  els.runtimeStatus.textContent = STATUS_TEXT[status] || String(status);
  els.runtimeResolution.textContent = `${w}x${h}`;
  els.runtimeFrame.textContent = String(frame);
}

function tick() {
  if (wasm) {
    wasm.pcfx_wasm_frame(buttonsMask(), mouseX | 0, mouseY | 0, mouseButtons >>> 0);
    renderOnce();
  }
  requestAnimationFrame(tick);
}

function rebuildControlMap() {
  els.controlMap.innerHTML = '';
  for (const [name] of BUTTONS) {
    const row = document.createElement('div');
    row.className = 'map-row';
    row.dataset.name = name;
    const label = document.createElement('span');
    label.textContent = name;
    const button = document.createElement('button');
    button.textContent = config.keys[name];
    button.addEventListener('click', () => {
      remapTarget = name;
      document.querySelectorAll('.map-row').forEach(r => r.classList.toggle('pending', r.dataset.name === name));
      button.textContent = 'press key...';
      els.screenFrame.focus();
    });
    row.append(label, button);
    els.controlMap.append(row);
  }
}

function saveStateToLocalStorage() {
  if (!wasm) return;
  const ok = wasm.pcfx_wasm_save_state();
  if (!ok) return updateStorageStatus('save failed', 'bad');
  const ptr = wasm.pcfx_wasm_get_save_ptr();
  const size = wasm.pcfx_wasm_get_save_size();
  const bytes = wasmU8().slice(ptr, ptr + size);
  let text = '';
  for (const b of bytes) text += String.fromCharCode(b);
  localStorage.setItem(STORAGE_STATE, btoa(text));
  updateStorageStatus(`state saved (${size} bytes)`, 'ok');
}

function loadStateFromLocalStorage() {
  if (!wasm) return;
  const raw = localStorage.getItem(STORAGE_STATE);
  if (!raw) return updateStorageStatus('no saved state in LocalStorage', 'warn');
  const text = atob(raw);
  const bytes = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i);
  const ptr = copyBytesToWasm(bytes);
  const ok = wasm.pcfx_wasm_load_state(ptr, bytes.byteLength);
  updateStorageStatus(ok ? 'state loaded' : 'state rejected', ok ? 'ok' : 'bad');
  renderOnce();
}

function clearLocalStorage() {
  localStorage.removeItem(STORAGE_CONFIG);
  localStorage.removeItem(STORAGE_STATE);
  config = { systemMode: 'pcfx', keys: { ...DEFAULT_KEYS } };
  setSystemMode('pcfx');
  rebuildControlMap();
  updateStorageStatus('configuration and state cleared', 'warn');
}

function canvasMousePosition(event) {
  const rect = els.canvas.getBoundingClientRect();
  const x = (event.clientX - rect.left) * els.canvas.width / rect.width;
  const y = (event.clientY - rect.top) * els.canvas.height / rect.height;
  return [x, y];
}

function wireEvents() {
  document.querySelectorAll('input[name="systemMode"]').forEach(r => {
    r.checked = (r.value === config.systemMode);
    r.addEventListener('change', () => setSystemMode(selectedSystemFromDom()));
  });

  els.openStartup.addEventListener('click', () => els.modal.classList.add('open'));
  els.pauseToggle.addEventListener('click', () => {
    paused = !paused;
    wasm?.pcfx_wasm_pause(paused ? 1 : 0);
    els.pauseToggle.textContent = paused ? 'Resume' : 'Pause';
    renderOnce();
  });
  els.saveState.addEventListener('click', saveStateToLocalStorage);
  els.loadState.addEventListener('click', loadStateFromLocalStorage);
  els.clearStorage.addEventListener('click', clearLocalStorage);

  els.biosFile.addEventListener('change', async () => {
    try {
      const file = els.biosFile.files[0];
      await loadBiosFile(file);
      els.startButton.disabled = !file;
    } catch (e) {
      els.biosName.textContent = e.message;
      els.startButton.disabled = true;
    }
  });

  els.startButton.addEventListener('click', () => {
    els.modal.classList.remove('open');
    els.screenFrame.focus();
  });

  els.mediaFile.addEventListener('change', async () => {
    try { await loadMediaFile(els.mediaFile.files[0]); }
    catch (e) { els.mediaName.textContent = e.message; updateStorageStatus(e.message, 'bad'); }
  });

  window.addEventListener('keydown', (e) => {
    if (remapTarget) {
      config.keys[remapTarget] = e.code;
      remapTarget = null;
      document.querySelectorAll('.map-row').forEach(r => r.classList.remove('pending'));
      saveConfig();
      rebuildControlMap();
      e.preventDefault();
      return;
    }
    pressed.add(e.code);
    if (Object.values(config.keys).includes(e.code)) e.preventDefault();
  });
  window.addEventListener('keyup', (e) => pressed.delete(e.code));

  els.screenFrame.addEventListener('click', () => els.screenFrame.focus());
  els.canvas.addEventListener('contextmenu', e => e.preventDefault());
  els.canvas.addEventListener('mousemove', e => {
    [mouseX, mouseY] = canvasMousePosition(e);
  });
  els.canvas.addEventListener('mousedown', e => {
    [mouseX, mouseY] = canvasMousePosition(e);
    mouseButtons |= (1 << e.button);
    els.screenFrame.focus();
    e.preventDefault();
  });
  window.addEventListener('mouseup', e => mouseButtons &= ~(1 << e.button));

  for (const target of [els.canvas, els.screenFrame]) {
    target.addEventListener('dragover', e => { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; });
    target.addEventListener('drop', async e => {
      e.preventDefault();
      const file = e.dataTransfer.files[0];
      if (file) await loadMediaFile(file);
    });
  }
}

wireEvents();
rebuildControlMap();
instantiateBackend().catch(err => {
  els.hud.textContent = `wasm load failed: ${err.message}`;
  updateStorageStatus(err.message, 'bad');
});
