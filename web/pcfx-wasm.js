const STORAGE_CONFIG = 'pcfx.wasm.config.v7';
const STORAGE_STATE_PREFIX = 'pcfx.wasm.savestate.v3.';
const DB_STATE_PREFIX = 'state:';
const STORAGE_MANIFEST = 'pcfx.wasm.storage.manifest.v2';
const DB_NAME = 'pcfx-wasm-storage';
const DB_VERSION = 1;
const STORE_FILES = 'files';

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

const DEFAULT_KEYS_P2 = {
  A: 'Numpad1', B: 'Numpad2', C: 'Numpad3',
  X: 'Numpad4', Y: 'Numpad5', Z: 'Numpad6',
  Select: 'Numpad7', Start: 'Numpad8',
  Up: 'KeyI', Right: 'KeyL', Down: 'KeyK', Left: 'KeyJ'
};

const STATUS_TEXT = {
  0: 'waiting for BIOS', 1: 'BIOS loaded', 2: 'media ready',
  3: 'running', 4: 'paused', 5: 'error'
};

const MEDIA_TEXT = {
  0: 'none', 1: 'cue/bin', 2: 'HuEXE', 3: 'CHD', 4: 'ISO/BIN',
  5: 'audio CD', 6: 'M3U', 7: 'TOC'
};

const DEFAULT_CONFIG = {
  systemMode: 'auto',
  keys: { ...DEFAULT_KEYS },
  keysP2: { ...DEFAULT_KEYS_P2 },
  touchGamepad: { opacity: 0.72 },
  video: { aspect: 'native', smoothUpscale: false, scanlines: false, showFps: true },
  enable3D: true,
  controllerType: 'gamepad',
  stateSlot: 0,
};

const els = {
  canvas: document.getElementById('video'),
  screenFrame: document.getElementById('screenFrame'),
  fpsCounter: document.getElementById('fpsCounter'),
  dropHint: document.getElementById('dropHint'),
  modal: document.getElementById('startupModal'),
  startupError: document.getElementById('startupError'),
  openStartup: document.getElementById('openStartup'),
  menuToggle: document.getElementById('menuToggle'),
  closeMenu: document.getElementById('closeMenu'),
  sideMenu: document.getElementById('sideMenu'),
  overlayActions: document.getElementById('overlayActions'),
  fullscreenToggle: document.getElementById('fullscreenToggle'),
  pauseToggle: document.getElementById('pauseToggle'),
  resetButton: document.getElementById('resetButton'),
  softResetButton: document.getElementById('softResetButton'),
  saveState: document.getElementById('saveState'),
  loadState: document.getElementById('loadState'),
  exportState: document.getElementById('exportState'),
  clearStorage: document.getElementById('clearStorage'),
  pcfxBiosFile: document.getElementById('pcfxBiosFile'),
  pcfxgaBiosFile: document.getElementById('pcfxgaBiosFile'),
  pcfxBiosStatus: document.getElementById('pcfxBiosStatus'),
  pcfxgaBiosStatus: document.getElementById('pcfxgaBiosStatus'),
  pcfxBiosCard: document.getElementById('pcfxBiosCard'),
  pcfxgaBiosCard: document.getElementById('pcfxgaBiosCard'),
  startButton: document.getElementById('startButton'),
  mediaFiles: document.getElementById('mediaFiles'),
  swapDiscFiles: document.getElementById('swapDiscFiles'),
  mediaName: document.getElementById('mediaName'),
  controlMap: document.getElementById('controlMap'),
  touchGamepad: document.getElementById('touchGamepad'),
  touchStick: document.getElementById('touchStick'),
  touchKnob: document.getElementById('touchKnob'),
  touchOpacity: document.getElementById('touchOpacity'),
  storageStatus: document.getElementById('storageStatus'),
  runtimeStatus: document.getElementById('runtimeStatus'),
  runtimeResolution: document.getElementById('runtimeResolution'),
  runtimeFrame: document.getElementById('runtimeFrame'),
  runtimeMedia: document.getElementById('runtimeMedia'),
  enable3D: document.getElementById('enable3D'),
  aspectMode: document.getElementById('aspectMode'),
  smoothUpscale: document.getElementById('smoothUpscale'),
  scanlines: document.getElementById('scanlines'),
  showFps: document.getElementById('showFps'),
  controllerType: document.getElementById('controllerType'),
  stateSlot: document.getElementById('stateSlot'),
};

let wasm;
let memory;
let db;
let hostFiles = new Map();
let nextHostFileHandle = 1;
let ctx = els.canvas.getContext('2d', { alpha: false });
let imageData = null;
let rgba = null;
let config = loadConfig();
let manifest = loadManifest();
let pressed = new Set();
let virtualPressed = new Set();
let remapTarget = null;
let activeTouchStickId = null;
let activeTouchStickRect = null;
let paused = false;
let mouseX = 0;
let mouseY = 0;
let mouseDx = 0;
let mouseDy = 0;
let mouseButtons = 0;
const touchControlsMedia = typeof window !== 'undefined' && window.matchMedia
  ? window.matchMedia('(hover: none) and (pointer: coarse), (any-pointer: coarse), (max-width: 980px), (max-height: 620px)')
  : null;
let activeBios = null;
let activeMedia = null;
let loadingMedia = false;
let mediaSessionId = 0;
let frameRequest = 0;
let audioCtx = null;
let audioNode = null;
let audioWorkletReady = false;
let audioWorkletPromise = null;
const AUDIO_WORKLET_PROCESSOR = `pcfx-pcm-processor-${Math.random().toString(36).slice(2)}`;
let audioQueue = [];
let audioQueueOffset = 0;
let audioQueueFrames = 0;
let audioUnderruns = 0;
let audioRate = 44100;
let fpsLastTime = 0;
let fpsLastFrame = 0;
let fpsValue = 0;
let limiterLastTime = 0;
let limiterAccumulator = 0;
let rafIntervalMs = 1000 / 60;
const FRAME_RATE = 60000 / 1001;
const NOMINAL_RAF_INTERVAL_MS = 1000 / 60;
const MAX_EMU_FRAMES_PER_RAF = 3;
const AUDIO_TARGET_LATENCY_MS = 42;
const AUDIO_HIGH_WATER_MS = 75;
const AUDIO_STALE_MS = 140;
let autoPausedByFocus = false;

function loadConfig() {
  try {
    const raw = localStorage.getItem(STORAGE_CONFIG);
    if (raw) {
      const saved = JSON.parse(raw);
      return {
        ...DEFAULT_CONFIG,
        ...saved,
        systemMode: normalizeSystemMode(saved.systemMode),
        keys: { ...DEFAULT_KEYS, ...(saved.keys || {}) },
        keysP2: { ...DEFAULT_KEYS_P2, ...(saved.keysP2 || {}) },
        touchGamepad: { opacity: Math.max(0.2, Math.min(1.0, Number(saved.touchGamepad?.opacity ?? DEFAULT_CONFIG.touchGamepad.opacity))) },
        video: {
          aspect: ['native', '4:3', 'stretch'].includes(saved.video?.aspect) ? saved.video.aspect : DEFAULT_CONFIG.video.aspect,
          smoothUpscale: !!saved.video?.smoothUpscale,
          scanlines: !!saved.video?.scanlines,
          showFps: saved.video?.showFps !== false,
        },
        enable3D: saved.enable3D !== false,
        controllerType: saved.controllerType === 'mouse' ? 'mouse' : 'gamepad',
        stateSlot: Number.isInteger(saved.stateSlot) ? saved.stateSlot : 0,
      };
    }
  } catch (_) {}
  return structuredClone(DEFAULT_CONFIG);
}

function loadManifest() {
  try {
    const raw = localStorage.getItem(STORAGE_MANIFEST);
    if (raw) return JSON.parse(raw);
  } catch (_) {}
  return { bios: {}, media: null };
}

function saveConfig() {
  localStorage.setItem(STORAGE_CONFIG, JSON.stringify(config));
}

function saveManifest() {
  localStorage.setItem(STORAGE_MANIFEST, JSON.stringify(manifest));
}

function updateStorageStatus(text, tone = 'muted') {
  els.storageStatus.textContent = text;
  els.storageStatus.className = tone === 'ok' ? 'status-ok' : tone === 'bad' ? 'status-bad' : tone === 'warn' ? 'status-warn' : 'muted';
}

function openDb() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE_FILES);
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error || new Error('IndexedDB open failed'));
  });
}

function dbPut(key, value) {
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_FILES, 'readwrite');
    tx.objectStore(STORE_FILES).put(value, key);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error || new Error(`IndexedDB put failed: ${key}`));
  });
}

function dbGet(key) {
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_FILES, 'readonly');
    const req = tx.objectStore(STORE_FILES).get(key);
    req.onsuccess = () => resolve(req.result || null);
    req.onerror = () => reject(req.error || new Error(`IndexedDB get failed: ${key}`));
  });
}

function dbClear() {
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_FILES, 'readwrite');
    tx.objectStore(STORE_FILES).clear();
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error || new Error('IndexedDB clear failed'));
  });
}

function normalizeSystemMode(mode) { return mode === 'pcfx' || mode === 'pcfxga' || mode === 'auto' ? mode : 'auto'; }
function systemModeValue(mode = config.systemMode) { return mode === 'pcfxga' ? 1 : mode === 'auto' ? 2 : 0; }
function biosKindForSystem(system) { return system === 'pcfxga' ? 2 : 1; }
function displaySystemName(system) { return system === 'pcfxga' ? 'PC-FXGA' : system === 'auto' ? 'Auto' : 'PC-FX'; }
function selectedStartupSystem() { return normalizeSystemMode(document.querySelector('input[name="startupSystemMode"]:checked')?.value); }
function selectedMenuSystem() { return normalizeSystemMode(document.querySelector('input[name="systemMode"]:checked')?.value); }

function setRadioGroup(name, value) {
  document.querySelectorAll(`input[name="${name}"]`).forEach(r => { r.checked = r.value === value; });
}

function applyConfigToControls() {
  setRadioGroup('systemMode', config.systemMode);
  setRadioGroup('startupSystemMode', config.systemMode);
  els.enable3D.checked = config.enable3D;
  els.aspectMode.value = config.video.aspect;
  els.smoothUpscale.checked = config.video.smoothUpscale;
  els.scanlines.checked = config.video.scanlines;
  els.showFps.checked = config.video.showFps;
  if (els.controllerType) els.controllerType.value = config.controllerType === 'mouse' ? 'mouse' : 'gamepad';
  if (els.touchOpacity) els.touchOpacity.value = String(Math.round((config.touchGamepad?.opacity ?? 0.72) * 100));
  applyTouchOpacity();
  els.stateSlot.value = String(config.stateSlot);
}

function updateBiosStatus() {
  const pcfx = manifest.bios?.pcfx;
  const pcfxga = manifest.bios?.pcfxga;
  els.pcfxBiosStatus.textContent = pcfx ? `${pcfx.name} (${pcfx.size.toLocaleString()} bytes)` : 'missing';
  els.pcfxgaBiosStatus.textContent = pcfxga ? `${pcfxga.name} (${pcfxga.size.toLocaleString()} bytes)` : 'missing';
  els.pcfxBiosStatus.className = pcfx ? 'status-ok' : 'status-bad';
  els.pcfxgaBiosStatus.className = pcfxga ? 'status-ok' : 'status-bad';
  els.pcfxBiosCard.classList.toggle('ready', !!pcfx);
  els.pcfxgaBiosCard.classList.toggle('ready', !!pcfxga);
}

function requireSelectedBios(system) {
  system = normalizeSystemMode(system);
  if (system === 'auto') {
    const pcfx = manifest.bios?.pcfx || null;
    const pcfxga = manifest.bios?.pcfxga || null;
    if (!pcfx && !pcfxga) throw new Error('At least one BIOS is required for Auto mode');
    return { name: pcfx && pcfxga ? 'Auto BIOS selection' : (pcfx || pcfxga).name, auto: true };
  }
  const entry = manifest.bios?.[system];
  if (!entry) throw new Error(`${displaySystemName(system)} BIOS is missing`);
  return entry;
}


function resetHostFiles() {
  hostFiles.clear();
  nextHostFileHandle = 1;
}

function releaseHostHandles(handles) {
  if (!handles) return;
  for (const h of handles) hostFiles.delete(h >>> 0);
}

function registerHostFile(bytes) {
  const handle = nextHostFileHandle++ >>> 0;
  if (!handle) throw new Error('host file handle exhausted');
  hostFiles.set(handle, bytes);
  return handle;
}

function wasmImports() {
  return {
    pcfx: {
      read_host_file(handle, offset, size, dest) {
        const bytes = hostFiles.get(handle >>> 0);
        if (!bytes || !memory) return 0;
        const off = offset >>> 0;
        const want = size >>> 0;
        const to = dest >>> 0;
        if (off >= bytes.byteLength || !want) return 0;
        const mem = wasmU8();
        if (to >= mem.byteLength) return 0;
        const len = Math.min(want, bytes.byteLength - off, mem.byteLength - to);
        if (!len) return 0;
        mem.set(bytes.subarray(off, off + len), to);
        return len >>> 0;
      }
    }
  };
}

async function instantiateBackend() {
  let result;
  try {
    result = await WebAssembly.instantiateStreaming(fetch('pcfx_wasm_core.wasm'), wasmImports());
  } catch (_) {
    const bytes = await (await fetch('pcfx_wasm_core.wasm')).arrayBuffer();
    result = await WebAssembly.instantiate(bytes, wasmImports());
  }
  wasm = result.instance.exports;
  memory = wasm.memory;
  resetHostFiles();
  wasm.pcfx_wasm_init(systemModeValue());
  wasm.pcfx_wasm_set_3d_enabled(config.enable3D ? 1 : 0);
  wasm.pcfx_wasm_set_browser_smooth(config.video.smoothUpscale ? 1 : 0);
  renderOnce();
}

function wasmU8() { return new Uint8Array(memory.buffer); }

function wasmErrorString(code) {
  if (code === undefined || code === null) return 'unknown';
  return `0x${(code >>> 0).toString(16).padStart(8, '0')}`;
}

function heapUsedText() {
  if (!wasm?.pcfx_wasm_heap_used) return '';
  const used = wasm.pcfx_wasm_heap_used() >>> 0;
  return `; heap ${Math.round(used / (1024 * 1024))} MiB`;
}

function copyBytesToWasm(bytes) {
  const size = bytes.byteLength || 1;
  const ptr = wasm.pcfx_wasm_malloc(size);
  if (!ptr) throw new Error(`wasm heap allocation failed for ${Math.round(size / (1024 * 1024))} MiB${heapUsedText()}`);
  wasmU8().set(bytes, ptr);
  return ptr;
}

function baseName(name) {
  const clean = String(name || '').replace(/\\/g, '/').split('/').pop() || 'file';
  return clean.replace(/[\x00-\x1f]/g, '_');
}

function hasExt(name, exts) {
  const n = String(name || '').toLowerCase();
  return exts.some(ext => n.endsWith(ext));
}

async function fileToBytes(file) {
  // Archive entries produced by expandZipArchive() carry a Uint8Array .bytes field.
  // Modern browser File objects may also expose a bytes() method; returning that
  // function object was the cause of 'can't convert undefined to BigInt'.
  if (file && file.bytes instanceof Uint8Array) return file.bytes;
  if (file && file.bytes instanceof ArrayBuffer) return new Uint8Array(file.bytes);
  if (file && typeof file.bytes === 'function') {
    const out = await file.bytes();
    if (out instanceof Uint8Array) return out;
    if (out instanceof ArrayBuffer) return new Uint8Array(out);
    if (ArrayBuffer.isView(out)) return new Uint8Array(out.buffer, out.byteOffset, out.byteLength);
    throw new Error('browser File.bytes() returned a non-byte payload');
  }
  if (file && typeof file.arrayBuffer === 'function') return new Uint8Array(await file.arrayBuffer());
  throw new Error(`media entry has no byte reader: ${baseName(file && file.name)}`);
}


function stringToWasm(text) {
  const bytes = new TextEncoder().encode(text);
  const ptr = wasm.pcfx_wasm_malloc(bytes.byteLength || 1);
  if (!ptr) throw new Error('wasm string allocation failed');
  wasmU8().set(bytes, ptr);
  return [ptr, bytes.byteLength];
}

function mediaBootFile(list, kind) {
  const lower = (f) => String(f.name || '').toLowerCase();
  const byExt = (exts) => list.find(f => exts.some(ext => lower(f).endsWith(ext)));
  return byExt(kind === 1 ? ['.cue', '.ccd']
    : kind === 3 ? ['.chd']
    : kind === 6 ? ['.m3u']
    : kind === 7 ? ['.toc']
    : kind === 2 ? ['.ex', '.exe']
    : kind === 4 ? ['.iso', '.bin', '.img']
    : ['.cue', '.chd', '.toc', '.m3u', '.iso', '.bin', '.img', '.ex', '.exe']) || list[0];
}

function mediaKindForFiles(files) {
  const names = [...files].map(f => String(f.name || '').toLowerCase());
  if (names.some(n => n.endsWith('.ex') || n.endsWith('.exe'))) return 2;
  if (names.some(n => n.endsWith('.chd'))) return 3;
  if (names.some(n => n.endsWith('.m3u'))) return 6;
  if (names.some(n => n.endsWith('.toc'))) return 7;
  if (names.some(n => n.endsWith('.cue') || n.endsWith('.ccd'))) return 1;
  if (names.every(n => /\.(wav|flac|ogg|mp3|aiff|aif)$/i.test(n))) return 5;
  if (names.some(n => n.endsWith('.iso') || n.endsWith('.bin') || n.endsWith('.img'))) return 4;
  return 0;
}

function readU16LE(bytes, off) { return bytes[off] | (bytes[off + 1] << 8); }
function readU32LE(bytes, off) { return (bytes[off] | (bytes[off + 1] << 8) | (bytes[off + 2] << 16) | (bytes[off + 3] << 24)) >>> 0; }

async function inflateRaw(bytes) {
  if (typeof DecompressionStream !== 'function') {
    throw new Error('ZIP entry is compressed, but this browser lacks DecompressionStream(deflate-raw)');
  }
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

async function expandZipArchive(file) {
  const zipBytes = new Uint8Array(await file.arrayBuffer());
  let eocd = -1;
  for (let p = zipBytes.length - 22; p >= Math.max(0, zipBytes.length - 0x10000 - 22); p--) {
    if (readU32LE(zipBytes, p) === 0x06054b50) { eocd = p; break; }
  }
  if (eocd < 0) throw new Error('ZIP archive has no end-of-central-directory record');
  const entries = readU16LE(zipBytes, eocd + 10);
  const cdSize = readU32LE(zipBytes, eocd + 12);
  let cdOff = readU32LE(zipBytes, eocd + 16);
  if (cdOff + cdSize > zipBytes.length) throw new Error('ZIP central directory is outside the archive');

  const out = [];
  const seen = new Set();
  const decoder = new TextDecoder();
  for (let i = 0; i < entries; i++) {
    if (readU32LE(zipBytes, cdOff) !== 0x02014b50) throw new Error('ZIP central directory entry is corrupt');
    const flags = readU16LE(zipBytes, cdOff + 8);
    const method = readU16LE(zipBytes, cdOff + 10);
    const compSize = readU32LE(zipBytes, cdOff + 20);
    const uncompSize = readU32LE(zipBytes, cdOff + 24);
    const nameLen = readU16LE(zipBytes, cdOff + 28);
    const extraLen = readU16LE(zipBytes, cdOff + 30);
    const commentLen = readU16LE(zipBytes, cdOff + 32);
    const localOff = readU32LE(zipBytes, cdOff + 42);
    const rawName = zipBytes.subarray(cdOff + 46, cdOff + 46 + nameLen);
    const fullName = decoder.decode(rawName).replace(/\\/g, '/');
    cdOff += 46 + nameLen + extraLen + commentLen;
    if (!fullName || fullName.endsWith('/') || fullName.includes('/../') || fullName.startsWith('../')) continue;
    const name = baseName(fullName);
    // Keep companion/sidecar files as well as boot media.  GMAKER HuEXE
    // samples load .AIC/.ACD/.AID assets through PIOLIB at runtime; dropping
    // them from ZIP expansion makes the executable boot with missing graphics.
    if (!name || name === '.' || name.startsWith('.')) continue;
    if (name.toLowerCase() === '.ds_store' || fullName.startsWith('__MACOSX/')) continue;
    const key = name.toLowerCase();
    if (seen.has(key)) throw new Error(`ZIP archive contains duplicate media basename: ${name}`);
    seen.add(key);
    if (readU32LE(zipBytes, localOff) !== 0x04034b50) throw new Error(`ZIP local header is corrupt for ${name}`);
    const localNameLen = readU16LE(zipBytes, localOff + 26);
    const localExtraLen = readU16LE(zipBytes, localOff + 28);
    const dataOff = localOff + 30 + localNameLen + localExtraLen;
    if (dataOff + compSize > zipBytes.length) throw new Error(`ZIP entry data is outside the archive: ${name}`);
    let data = zipBytes.slice(dataOff, dataOff + compSize);
    if (method === 0) {
      if (data.byteLength !== uncompSize) data = data.slice(0, uncompSize);
    } else if (method === 8) {
      data = await inflateRaw(data);
      if (data.byteLength !== uncompSize) throw new Error(`ZIP entry inflated to the wrong size: ${name}`);
    } else {
      throw new Error(`ZIP entry uses unsupported compression method ${method}: ${name}`);
    }
    out.push({ name, size: data.byteLength, bytes: data });
  }
  if (!out.length) throw new Error('ZIP archive contains no usable files');
  return out.sort((a, b) => a.name.localeCompare(b.name));
}

async function expandSelectedMedia(fileList) {
  const files = [...fileList];
  if (files.length !== 1) throw new Error('Select exactly one file: a .chd, a PC-FXGA .ex/.exe, or a .zip archive containing sidecars');
  const file = files[0];
  const name = String(file.name || '').toLowerCase();
  if (name.endsWith('.chd')) return [file];
  if (name.endsWith('.ex') || name.endsWith('.exe')) return [file];
  if (name.endsWith('.zip')) return expandZipArchive(file);
  if (name.endsWith('.cue') || name.endsWith('.bin') || name.endsWith('.iso') || name.endsWith('.toc') || name.endsWith('.m3u')) {
    throw new Error('Browser loading accepts one file only; package cue/bin or other sidecar sets into a .zip archive, or use .chd');
  }
  throw new Error('Unsupported browser media. Use .chd, .ex/.exe, or .zip with sidecars inside.');
}

const HOST_FILE_THRESHOLD = 8 * 1024 * 1024;

async function addFileToWasm(path, bytes) {
  const [pathPtr, pathLen] = stringToWasm(path);
  let ok = 0;
  let handle = 0;
  if (bytes.byteLength >= HOST_FILE_THRESHOLD && wasm.pcfx_wasm_vfs_add_host_file) {
    handle = registerHostFile(bytes);
    ok = wasm.pcfx_wasm_vfs_add_host_file(pathPtr, pathLen, handle, bytes.byteLength >>> 0);
  } else {
    const dataPtr = copyBytesToWasm(bytes);
    ok = wasm.pcfx_wasm_vfs_add_file(pathPtr, pathLen, dataPtr, bytes.byteLength >>> 0);
  }
  if (!ok) {
    if (handle) hostFiles.delete(handle);
    throw new Error(`wasm VFS rejected ${path}`);
  }
  return handle;
}

async function fnv1aFile(file, limit = 2 * 1024 * 1024) {
  const bytes = new Uint8Array(await file.slice(0, Math.min(file.size, limit)).arrayBuffer());
  let h = 0x811c9dc5;
  for (const b of bytes) {
    h ^= b;
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h >>> 0;
}

async function storeBios(system, file) {
  if (!file) return;
  const bytes = new Uint8Array(await file.arrayBuffer());
  if (bytes.byteLength < 256) throw new Error('BIOS file is too small');
  const key = `bios:${system}`;
  const checksum = await fnv1aFile(file);
  await dbPut(key, bytes);
  manifest.bios[system] = { key, name: file.name, size: file.size, checksum, storedAt: new Date().toISOString() };
  saveManifest();
  updateBiosStatus();
  updateStorageStatus(`${displaySystemName(system)} BIOS stored`, 'ok');
}

async function loadStoredBios(system) {
  system = normalizeSystemMode(system);
  const entry = requireSelectedBios(system);
  resetHostFiles();
  wasm.pcfx_wasm_reset_heap();

  const loadOne = async (name) => {
    const biosEntry = manifest.bios?.[name];
    if (!biosEntry) return null;
    const bytes = await dbGet(biosEntry.key);
    if (!bytes) throw new Error(`${displaySystemName(name)} BIOS metadata exists but IndexedDB payload is missing`);
    const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    const ptr = copyBytesToWasm(u8);
    const ok = wasm.pcfx_wasm_load_bios(ptr, u8.byteLength, biosKindForSystem(name));
    if (!ok) throw new Error(`${displaySystemName(name)} BIOS was rejected by wasm backend`);
    return biosEntry;
  };

  let loaded = null;
  if (system === 'auto') {
    const pcfx = await loadOne('pcfx');
    const pcfxga = await loadOne('pcfxga');
    loaded = { name: pcfx && pcfxga ? 'Auto BIOS selection' : (pcfx || pcfxga)?.name || 'Auto BIOS selection', auto: true, pcfx, pcfxga };
  } else {
    loaded = await loadOne(system);
  }
  if (!loaded) throw new Error(`${displaySystemName(system)} BIOS is missing`);
  activeBios = loaded;
  return loaded;
}

async function installAudioWorklet() {
  if (!audioCtx || !audioCtx.audioWorklet) return false;
  if (audioWorkletReady && audioNode) return true;
  if (audioWorkletPromise) return audioWorkletPromise;

  audioWorkletPromise = (async () => {
    const source = `
class PCFXPCMProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.queue = [];
    this.offset = 0;
    this.frames = 0;
    this.frac = 0;
    this.underruns = 0;
    const opts = (options && options.processorOptions) || {};
    const sourceRate = opts.sourceRate || 44100;
    this.step = sourceRate / sampleRate;
    this.port.onmessage = (ev) => {
      const msg = ev.data || {};
      if (msg.type === 'audio' && msg.buffer) {
        const chunk = new Int16Array(msg.buffer);
        this.queue.push(chunk);
        this.frames += chunk.length >> 1;
      } else if (msg.type === 'clear') {
        this.queue = [];
        this.offset = 0;
        this.frames = 0;
        this.frac = 0;
      }
    };
  }
  peekFrame(rel) {
    let index = this.offset + rel;
    for (let q = 0; q < this.queue.length; q++) {
      const chunk = this.queue[q];
      const frames = chunk.length >> 1;
      if (index < frames) {
        const si = index * 2;
        return [chunk[si] / 32768, chunk[si + 1] / 32768];
      }
      index -= frames;
    }
    return null;
  }
  dropFrames(count) {
    let dropped = 0;
    while (count > 0 && this.queue.length) {
      const front = this.queue[0];
      const frames = front.length >> 1;
      const available = frames - this.offset;
      const take = Math.min(count, available);
      this.offset += take;
      count -= take;
      dropped += take;
      if (this.offset >= frames) {
        this.queue.shift();
        this.offset = 0;
      }
    }
    this.frames = Math.max(0, this.frames - dropped);
    return dropped;
  }
  process(inputs, outputs) {
    const out = outputs[0];
    const left = out[0];
    const right = out[1] || out[0];
    let consumed = 0;
    for (let i = 0; i < left.length; i++) {
      if (this.frames < 2) {
        left[i] = 0;
        right[i] = 0;
        this.underruns++;
        continue;
      }
      const a = this.peekFrame(0);
      const b = this.peekFrame(1) || a;
      const t = this.frac;
      left[i] = a[0] + (b[0] - a[0]) * t;
      right[i] = a[1] + (b[1] - a[1]) * t;
      this.frac += this.step;
      const whole = Math.floor(this.frac);
      if (whole > 0) {
        consumed += this.dropFrames(whole);
        this.frac -= whole;
      }
    }
    if (consumed || this.underruns) {
      this.port.postMessage({ type: 'stats', consumed, queued: this.frames, underruns: this.underruns });
      this.underruns = 0;
    }
    return true;
  }
}
registerProcessor('${AUDIO_WORKLET_PROCESSOR}', PCFXPCMProcessor);
`;
    const url = URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
    try {
      try {
        await audioCtx.audioWorklet.addModule(url);
      } catch (e) {
        // Some browsers report duplicate registration if the module was already
        // evaluated in this AudioContext. The processor is then usable, so try
        // constructing the node before falling back.
        if (!/already registered|same name/i.test(String(e && (e.message || e)))) throw e;
      }
      audioNode = new AudioWorkletNode(audioCtx, AUDIO_WORKLET_PROCESSOR, { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2], processorOptions: { sourceRate: audioRate } });
      audioNode.port.onmessage = ev => {
        const msg = ev.data || {};
        if (msg.type === 'stats') {
          audioQueueFrames = Math.max(0, msg.queued || 0);
          audioUnderruns += msg.underruns || 0;
        }
      };
      audioNode.connect(audioCtx.destination);
      audioWorkletReady = true;
      return true;
    } finally {
      URL.revokeObjectURL(url);
    }
  })();

  try {
    return await audioWorkletPromise;
  } catch (e) {
    audioWorkletPromise = null;
    throw e;
  }
}

function installScriptProcessorFallback() {
  if (!audioCtx || audioNode) return;
  audioNode = audioCtx.createScriptProcessor(1024, 0, 2);
  audioNode.onaudioprocess = ev => {
    const left = ev.outputBuffer.getChannelData(0);
    const right = ev.outputBuffer.getChannelData(1);
    for (let i = 0; i < left.length; i++) {
      if (!audioQueue.length) { left[i] = 0; right[i] = 0; audioUnderruns++; continue; }
      const front = audioQueue[0];
      const si = audioQueueOffset * 2;
      left[i] = front[si] / 32768;
      right[i] = front[si + 1] / 32768;
      audioQueueOffset++;
      audioQueueFrames--;
      if (audioQueueOffset >= (front.length >> 1)) {
        audioQueue.shift();
        audioQueueOffset = 0;
      }
    }
  };
  audioNode.connect(audioCtx.destination);
}

function browserHasUserActivation(ev) {
  if (ev && ev.isTrusted === false) return false;
  const ua = navigator.userActivation;
  if (ua && ua.isActive === false) return false;
  return true;
}

function audioSourceRate() {
  if (wasm && wasm.pcfx_wasm_get_audio_rate) {
    const rate = wasm.pcfx_wasm_get_audio_rate();
    if (rate > 0) return rate;
  }
  return 44100;
}

function unlockAudioFromGesture(ev) {
  if (!browserHasUserActivation(ev)) return false;
  const Ctor = window.AudioContext || window.webkitAudioContext;
  if (!Ctor) return false;

  audioRate = audioSourceRate();

  if (!audioCtx) {
    // Keep construction synchronous inside the trusted gesture.  Do not request
    // a fixed sampleRate here; several browsers are stricter about autoplay
    // policy when an AudioContext is constructed later or with extra options.
    try { audioCtx = new Ctor({ latencyHint: 'interactive' }); }
    catch (_) { audioCtx = new Ctor(); }
  }

  if (audioCtx.state !== 'running') {
    const resumePromise = audioCtx.resume();
    if (resumePromise && resumePromise.catch) resumePromise.catch(() => {});
  }

  ensureAudioOutputInstalled();
  return true;
}

function ensureAudioOutputInstalled() {
  if (!audioCtx || audioNode) return;
  audioRate = audioSourceRate();
  installAudioWorklet().catch(() => installScriptProcessorFallback());
}

function clearAudioQueue() {
  if (audioWorkletReady && audioNode?.port) audioNode.port.postMessage({ type: 'clear' });
  audioQueue = [];
  audioQueueOffset = 0;
  audioQueueFrames = 0;
  audioUnderruns = 0;
}

function startAudio() {
  // Do not create or resume AudioContext here. This function runs after async
  // media/BIOS work and may no longer be inside a browser user-activation
  // window. AudioContext creation/resume is handled only by gesture handlers.
  if (audioCtx) ensureAudioOutputInstalled();
  clearAudioQueue();
}

function pullAudio() {
  if (!wasm || !wasm.pcfx_wasm_get_audio_frames || !wasm.pcfx_wasm_get_audio_ptr) return;
  const staleCap = Math.floor(audioRate * AUDIO_STALE_MS / 1000);
  if (audioQueueFrames > staleCap) {
    if (audioWorkletReady) audioNode.port.postMessage({ type: 'clear' });
    audioQueue = [];
    audioQueueOffset = 0;
    audioQueueFrames = 0;
  }
  const frames = wasm.pcfx_wasm_get_audio_frames();
  if (!frames) return;
  const ptr = wasm.pcfx_wasm_get_audio_ptr();
  if (!ptr) return;
  const samples = new Int16Array(memory.buffer, ptr, frames * 2);
  const copy = new Int16Array(samples);
  if (audioWorkletReady) {
    audioNode.port.postMessage({ type: 'audio', buffer: copy.buffer }, [copy.buffer]);
    audioQueueFrames += frames;
  } else {
    audioQueue.push(copy);
    audioQueueFrames += frames;
  }
  if (wasm.pcfx_wasm_audio_consume) wasm.pcfx_wasm_audio_consume(frames);
}

function clearCanvasToBlack(width = 256, height = 240) {
  resizeCanvasIfNeeded(width, height);
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, els.canvas.width, els.canvas.height);
  applyVideoLayout(width, height);
}

async function loadMediaFilesIntoWasm(files, options = {}) {
  const list = await expandSelectedMedia(files);
  const kind = mediaKindForFiles(list);
  if (!kind) throw new Error('unsupported media inside archive; use CHD, PC-FXGA .ex/.exe, or a ZIP containing sidecars');
  if (options.discOnly && kind === 2) throw new Error('PC-FXGA HuEXE programs are boot media, not swappable CD media; use Load new game / reset machine');
  if (config.systemMode === 'pcfxga' && kind === 5) throw new Error('Audio CD media is supported only in PC-FX or Auto mode, not explicit PC-FXGA mode');
  if (kind !== 3 && kind !== 2 && files.length === 1 && !String(files[0].name).toLowerCase().endsWith('.zip')) {
    throw new Error('Sidecar media must be provided as one .zip archive in the browser build');
  }

  const prefix = `/media/session${++mediaSessionId}`;
  let totalSize = 0;
  const hostHandles = [];
  for (const file of list) {
    const bytes = await fileToBytes(file);
    if (!(bytes instanceof Uint8Array)) throw new Error(`media entry did not decode to bytes: ${baseName(file.name)}`);
    totalSize += bytes.byteLength;
    const handle = await addFileToWasm(`${prefix}/${baseName(file.name)}`, bytes);
    if (handle) hostHandles.push(handle);
  }

  const bootFile = mediaBootFile(list, kind);
  const bootPath = `${prefix}/${baseName(bootFile.name)}`;
  return { list, kind, totalSize, hostHandles, bootFile, bootPath, archive: files[0].name };
}

async function storeMedia(files) {
  if (!files || !files.length) return;
  if (!wasm) throw new Error('wasm backend is not ready');
  requireSelectedBios(config.systemMode);

  loadingMedia = true;
  activeMedia = null;
  pressed.clear();
  mouseButtons = 0;
  clearAudioQueue();
  clearCanvasToBlack();
  els.mediaName.textContent = 'loading new game...';
  try {
    wasm.pcfx_wasm_init(systemModeValue());
    resetHostFiles();
    wasm.pcfx_wasm_set_3d_enabled(config.enable3D ? 1 : 0);
    wasm.pcfx_wasm_set_browser_smooth(config.video.smoothUpscale ? 1 : 0);
    wasm.pcfx_wasm_set_controller_type?.(controllerTypeValue());
    await loadStoredBios(config.systemMode);

    const media = await loadMediaFilesIntoWasm(files);
    const [pathPtr, pathLen] = stringToWasm(media.bootPath);
    if (!wasm.pcfx_wasm_set_media_path(pathPtr, pathLen, media.kind)) {
      throw new Error(`media path was rejected by wasm backend: ${media.bootPath}; wasm error ${wasmErrorString(wasm.pcfx_wasm_get_error?.())}`);
    }

    const started = wasm.pcfx_wasm_start();
    if (!started) {
      throw new Error(`emulator failed to start from ${media.bootFile.name}; wasm error ${wasmErrorString(wasm.pcfx_wasm_get_error?.())}`);
    }

    activeMedia = { names: media.list.map(f => baseName(f.name)), bootPath: media.bootPath, totalSize: String(media.totalSize), kind: media.kind, fileCount: media.list.length, archive: media.archive, hostHandles: media.hostHandles };
    manifest.media = { names: activeMedia.names, kind: media.kind, fileCount: media.list.length, archive: media.archive, storedAt: new Date().toISOString() };
    saveManifest();
    els.mediaName.textContent = `${MEDIA_TEXT[media.kind] || 'media'}: ${media.bootFile.name}`;
    updateStorageStatus(`emulator reset and running from session media (${media.list.length} file${media.list.length === 1 ? '' : 's'}, ${Math.round(media.totalSize / 1024).toLocaleString()} KiB, ${hostFiles.size} streamed host file${hostFiles.size === 1 ? '' : 's'}${heapUsedText()})`, 'ok');

    startAudio();
    resetFrameLimiter();
    for (let i = 0; i < 2; i++) {
      wasm.pcfx_wasm_frame(buttonsMask(), 0, 0, 0);
      pullAudio();
    }
    resetFrameLimiter();
    renderOnce();
  } finally {
    loadingMedia = false;
    if (els.mediaFiles) els.mediaFiles.value = '';
  }
}

async function swapDisc(files) {
  if (!files || !files.length) return;
  if (!wasm) throw new Error('wasm backend is not ready');
  if (!activeMedia || wasm.pcfx_wasm_get_status?.() !== 3) throw new Error('start a CD game before using Swap Disc');
  if (!wasm.pcfx_wasm_swap_disc) throw new Error('this WASM backend does not expose runtime disc swapping');

  loadingMedia = true;
  pressed.clear();
  mouseButtons = 0;
  clearAudioQueue();
  els.mediaName.textContent = 'swapping disc...';
  const oldHandles = activeMedia.hostHandles ? [...activeMedia.hostHandles] : [];
  try {
    const media = await loadMediaFilesIntoWasm(files, { discOnly: true });
    const [pathPtr, pathLen] = stringToWasm(media.bootPath);
    const ok = wasm.pcfx_wasm_swap_disc(pathPtr, pathLen, media.kind);
    if (!ok) {
      releaseHostHandles(media.hostHandles);
      throw new Error(`disc swap failed for ${media.bootFile.name}; wasm error ${wasmErrorString(wasm.pcfx_wasm_get_error?.())}`);
    }

    releaseHostHandles(oldHandles);
    activeMedia = { names: media.list.map(f => baseName(f.name)), bootPath: media.bootPath, totalSize: String(media.totalSize), kind: media.kind, fileCount: media.list.length, archive: media.archive, hostHandles: media.hostHandles };
    manifest.media = { names: activeMedia.names, kind: media.kind, fileCount: media.list.length, archive: media.archive, swappedAt: new Date().toISOString() };
    saveManifest();
    els.mediaName.textContent = `swapped to ${MEDIA_TEXT[media.kind] || 'media'}: ${media.bootFile.name}`;
    updateStorageStatus(`disc swapped without reset (${media.list.length} file${media.list.length === 1 ? '' : 's'}, ${Math.round(media.totalSize / 1024).toLocaleString()} KiB, ${hostFiles.size} streamed host file${hostFiles.size === 1 ? '' : 's'}${heapUsedText()})`, 'ok');
    startAudio();
    resetFrameLimiter();
    renderOnce();
  } finally {
    loadingMedia = false;
    if (els.swapDiscFiles) els.swapDiscFiles.value = '';
  }
}

async function startSelectedSystem(system) {
  config.systemMode = system;
  saveConfig();
  applyConfigToControls();
  loadingMedia = true;
  activeMedia = null;
  clearAudioQueue();
  clearCanvasToBlack();
  try {
    wasm.pcfx_wasm_init(systemModeValue());
    resetHostFiles();
    wasm.pcfx_wasm_set_3d_enabled(config.enable3D ? 1 : 0);
    wasm.pcfx_wasm_set_browser_smooth(config.video.smoothUpscale ? 1 : 0);
    wasm.pcfx_wasm_set_controller_type?.(controllerTypeValue());
    await loadStoredBios(system);
    const started = wasm.pcfx_wasm_start();
    if (!started) throw new Error(`BIOS boot failed; wasm error ${wasmErrorString(wasm.pcfx_wasm_get_error?.())}`);
    els.modal.classList.remove('open');
    els.screenFrame.focus();
    updateStorageStatus(`${displaySystemName(system)} BIOS active: ${activeBios.name}`, 'ok');
    resetFrameLimiter();
    renderOnce();
  } finally {
    loadingMedia = false;
  }
}

function controllerTypeValue() {
  return config.controllerType === 'mouse' ? 1 : 0;
}

function resetMouseDeltas() {
  mouseDx = 0;
  mouseDy = 0;
}

function mouseButtonMask(buttons = mouseButtons) {
  let mask = 0;
  if (buttons & 1) mask |= 1;       // browser/SDL left -> PC-FX mouse left
  if (buttons & 4) mask |= 2;       // browser right button is bit 2
  if (buttons & 2) mask |= 4;       // middle, preserved as a third button
  return mask;
}

function buttonsMask(player = 0) {
  let mask = 0;
  const map = player === 1 ? config.keysP2 : config.keys;
  for (const [name, bit] of BUTTONS) {
    if (pressed.has(map[name])) mask |= bit;
    if (player === 0 && virtualPressed.has(name)) mask |= bit;
  }
  return mask;
}

function applyTouchOpacity() {
  const value = Math.max(0.2, Math.min(1.0, Number(config.touchGamepad?.opacity ?? 0.72)));
  if (els.touchGamepad) els.touchGamepad.style.setProperty('--touch-opacity', String(value));
}

function updateTouchControlsAvailability() {
  const hasTouchInput = Number(navigator.maxTouchPoints || 0) > 0 || Number(navigator.msMaxTouchPoints || 0) > 0;
  const ua = navigator.userAgent || '';
  const mobileUserAgent = /Android|iPhone|iPad|iPod|Mobile|Tablet|Kindle|Silk|Windows Phone/i.test(ua);
  const compactViewport = Math.min(window.innerWidth || 0, window.innerHeight || 0) <= 620
    || Math.max(window.innerWidth || 0, window.innerHeight || 0) <= 980;
  const coarsePointer = !!touchControlsMedia?.matches;
  const shouldShow = hasTouchInput || coarsePointer || mobileUserAgent || compactViewport;
  document.body.classList.toggle('touch-controls-active', shouldShow);
}

function setSideMenuOpen(open) {
  const isOpen = !!open;
  els.sideMenu?.classList.toggle('open', isOpen);
  document.body.classList.toggle('menu-open', isOpen);
}

function toggleSideMenu() {
  setSideMenuOpen(!els.sideMenu?.classList.contains('open'));
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

function framebufferToImageData(ptr, w, h, pitch, pixelFormat, bytesPerPixel) {
  resizeCanvasIfNeeded(w, h);
  const mem = memory.buffer;
  const memBytes = mem.byteLength;
  const assertRange = (offset, length) => {
    if (offset < 0 || length < 0 || offset + length > memBytes) {
      throw new Error(`framebuffer outside WASM memory: ptr=${ptr} ${w}x${h} pitch=${pitch} bpp=${bytesPerPixel}`);
    }
  };

  if (pixelFormat === 2 && bytesPerPixel === 4) {
    if (pitch === w) {
      assertRange(ptr, w * h * 4);
      const src = new Uint8ClampedArray(mem, ptr, w * h * 4);
      rgba.set(src);
    } else {
      for (let y = 0; y < h; y++) {
        const row = ptr + y * pitch * 4;
        assertRange(row, w * 4);
        const src = new Uint8ClampedArray(mem, row, w * 4);
        rgba.set(src, y * w * 4);
      }
    }
    return;
  }

  if (pixelFormat === 1 && bytesPerPixel === 2) {
    for (let y = 0, j = 0; y < h; y++) {
      const row = ptr + y * pitch * 2;
      assertRange(row, w * 2);
      const pixels = new Uint16Array(mem, row, w);
      for (let x = 0; x < w; x++, j += 4) {
        const p = pixels[x];
        const r5 = (p >> 11) & 0x1f;
        const g6 = (p >> 5) & 0x3f;
        const b5 = p & 0x1f;
        rgba[j + 0] = (r5 << 3) | (r5 >> 2);
        rgba[j + 1] = (g6 << 2) | (g6 >> 4);
        rgba[j + 2] = (b5 << 3) | (b5 >> 2);
        rgba[j + 3] = 255;
      }
    }
    return;
  }

  throw new Error(`unsupported WASM framebuffer format ${pixelFormat} / ${bytesPerPixel} bytes`);
}

function fitInside(containerW, containerH, aspect) {
  let width = containerW;
  let height = width / aspect;
  if (height > containerH) {
    height = containerH;
    width = height * aspect;
  }
  return [Math.max(1, width), Math.max(1, height)];
}

function applyVideoLayout(w, h) {
  els.canvas.classList.toggle('smooth', config.video.smoothUpscale);
  els.canvas.classList.toggle('nearest', !config.video.smoothUpscale);
  els.screenFrame.classList.toggle('scanlines', config.video.scanlines);
  els.fpsCounter.classList.toggle('hidden', !config.video.showFps);
  els.fpsCounter.setAttribute('aria-hidden', config.video.showFps ? 'false' : 'true');
  els.screenFrame.dataset.aspect = config.video.aspect;

  const rect = els.screenFrame.getBoundingClientRect();
  const frameW = Math.max(1, rect.width || window.innerWidth || w);
  const frameH = Math.max(1, rect.height || window.innerHeight || h);
  let cssW = frameW;
  let cssH = frameH;

  if (config.video.aspect === 'native') {
    const maxScale = Math.min(frameW / Math.max(1, w), frameH / Math.max(1, h));
    const scale = maxScale >= 1 ? Math.max(1, Math.floor(maxScale)) : maxScale;
    cssW = Math.max(1, Math.floor(w * scale));
    cssH = Math.max(1, Math.floor(h * scale));
  } else if (config.video.aspect === '4:3') {
    [cssW, cssH] = fitInside(frameW, frameH, 4 / 3);
  } else {
    cssW = frameW;
    cssH = frameH;
  }

  if (document.fullscreenElement === els.screenFrame) {
    cssW = Math.min(frameW, Math.max(1, Math.round(cssW)));
    cssH = Math.min(frameH, Math.max(1, Math.round(cssH)));
    if (Math.abs(cssW - frameW) <= 1) cssW = frameW;
    if (Math.abs(cssH - frameH) <= 1) cssH = frameH;
  }

  els.canvas.style.width = `${cssW}px`;
  els.canvas.style.height = `${cssH}px`;
  els.canvas.style.maxWidth = 'none';
  els.canvas.style.maxHeight = 'none';
  els.canvas.style.aspectRatio = config.video.aspect === 'stretch' ? 'auto' : `${cssW} / ${cssH}`;
}

function updateFpsCounter(frame) {
  if (!config.video.showFps) return;
  const now = performance.now();
  if (!fpsLastTime) {
    fpsLastTime = now;
    fpsLastFrame = frame;
    els.fpsCounter.textContent = `${fpsValue.toFixed(1)} FPS`;
    return;
  }
  const dt = now - fpsLastTime;
  if (dt >= 500) {
    const df = frame - fpsLastFrame;
    fpsValue = df > 0 ? (df * 1000 / dt) : 0;
    fpsLastTime = now;
    fpsLastFrame = frame;
    els.fpsCounter.textContent = `${fpsValue.toFixed(1)} FPS`;
  }
}

function renderOnce() {
  if (!wasm) return;
  const status = wasm.pcfx_wasm_get_status();
  const frame = wasm.pcfx_wasm_get_frame_count();
  const media = wasm.pcfx_wasm_get_media_kind ? wasm.pcfx_wasm_get_media_kind() : 0;
  let w = wasm.pcfx_wasm_get_width();
  let h = wasm.pcfx_wasm_get_height();

  // Do not paint the emulator fallback/framebuffer before real media is
  // mounted. With BIOS-only startup the core has no rendered frame yet, and
  // displaying fallback memory looked like a red corruption pattern. The
  // visible screen should remain black until a game/disc has actually started.
  if (!activeMedia || media === 0 || status < 3) {
    if (!w) w = config.systemMode === 'pcfxga' ? 344 : 256;
    if (!h) h = 240;
    clearCanvasToBlack(w, h);
    fpsValue = 0;
    if (config.video.showFps) els.fpsCounter.textContent = '0.0 FPS';
  } else {
    const pitch = wasm.pcfx_wasm_get_pitch_pixels();
    const pixelFormat = wasm.pcfx_wasm_get_pixel_format ? wasm.pcfx_wasm_get_pixel_format() : 2;
    const bytesPerPixel = wasm.pcfx_wasm_get_bytes_per_pixel ? wasm.pcfx_wasm_get_bytes_per_pixel() : 4;
    framebufferToImageData(wasm.pcfx_wasm_get_framebuffer(), w, h, pitch, pixelFormat, bytesPerPixel);
    ctx.putImageData(imageData, 0, 0);
    applyVideoLayout(w, h);
    updateFpsCounter(frame);
  }

  els.runtimeStatus.textContent = STATUS_TEXT[status] || String(status);
  els.runtimeResolution.textContent = `${w}x${h}`;
  els.runtimeFrame.textContent = String(frame);
  els.runtimeMedia.textContent = MEDIA_TEXT[media] || String(media);
  if (els.dropHint) els.dropHint.classList.add('hidden');
}

function resetFrameLimiter(now = performance.now()) {
  limiterLastTime = now;
  limiterAccumulator = 0;
  rafIntervalMs = NOMINAL_RAF_INTERVAL_MS;
  fpsLastTime = 0;
  fpsLastFrame = wasm?.pcfx_wasm_get_frame_count?.() || 0;
}

function noteRafInterval(dt) {
  if (Number.isFinite(dt) && dt >= 4 && dt <= 80) {
    rafIntervalMs = (rafIntervalMs * 0.90) + (dt * 0.10);
  }
}

function tick(now) {
  if (wasm) {
    if (!paused && !loadingMedia && wasm.pcfx_wasm_get_status?.() === 3) {
      if (!limiterLastTime) limiterLastTime = now;
      let elapsed = now - limiterLastTime;
      limiterLastTime = now;
      if (!Number.isFinite(elapsed) || elapsed < 0) elapsed = 0;
      if (elapsed > 250) elapsed = 1000 / FRAME_RATE;
      noteRafInterval(elapsed);

      // requestAnimationFrame remains the outer cadence, but the emulator is
      // rate-limited by elapsed presentation time, not by audio queue level.
      // Audio may be unavailable, paused by autoplay rules, or waiting for an
      // AudioWorklet stats callback. Using it as a hard brake can deadlock the
      // machine at a small frame count after drag/drop.
      limiterAccumulator = Math.min(limiterAccumulator + elapsed, (1000 / FRAME_RATE) * 4);
      let ran = 0;
      const frameInterval = 1000 / FRAME_RATE;
      while (limiterAccumulator >= frameInterval && ran < MAX_EMU_FRAMES_PER_RAF) {
        if (wasm.pcfx_wasm_frame2) wasm.pcfx_wasm_frame2(buttonsMask(0), buttonsMask(1), mouseDx | 0, mouseDy | 0, mouseButtonMask() >>> 0);
        else wasm.pcfx_wasm_frame(buttonsMask(0), mouseDx | 0, mouseDy | 0, mouseButtonMask() >>> 0);
        resetMouseDeltas();
        pullAudio();
        limiterAccumulator -= frameInterval;
        ran++;
      }
    } else {
      limiterLastTime = now;
      limiterAccumulator = 0;
      rafIntervalMs = NOMINAL_RAF_INTERVAL_MS;
    }
    renderOnce();
  }
  frameRequest = requestAnimationFrame(tick);
}

function rebuildControlMap() {
  els.controlMap.innerHTML = '';
  for (const player of [0, 1]) {
    const heading = document.createElement('div');
    heading.className = 'map-heading';
    heading.textContent = player === 0 ? 'Player 1 keyboard' : 'Player 2 keyboard';
    els.controlMap.append(heading);
    const map = player === 0 ? config.keys : config.keysP2;
    for (const [name] of BUTTONS) {
      const row = document.createElement('div');
      row.className = 'map-row';
      row.dataset.name = name;
      row.dataset.player = String(player);
      const label = document.createElement('span');
      label.textContent = name;
      const button = document.createElement('button');
      button.textContent = map[name];
      button.addEventListener('click', () => {
        remapTarget = { player, name };
        document.querySelectorAll('.map-row').forEach(r => r.classList.toggle('pending', r.dataset.name === name && Number(r.dataset.player) === player));
        button.textContent = 'press key...';
        els.screenFrame.focus();
      });
      row.append(label, button);
      els.controlMap.append(row);
    }
  }
}

function mediaStateFingerprint() {
  if (!activeMedia) return 'bios';
  const parts = [
    config.systemMode,
    activeMedia.kind || 'media',
    activeMedia.archive || '',
    activeMedia.totalSize || '',
    activeMedia.fileCount || '',
    ...(Array.isArray(activeMedia.names) ? activeMedia.names : []),
  ];
  let h = 2166136261;
  const text = parts.join('|').toLowerCase();
  for (let i = 0; i < text.length; i++) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h.toString(16).padStart(8, '0');
}

function syncStateSlotFromControl() {
  if (!els.stateSlot) return;
  const slot = parseInt(els.stateSlot.value, 10);
  config.stateSlot = Number.isFinite(slot) ? Math.max(0, Math.min(9, slot)) : 0;
}

function stateKey() { return `${STORAGE_STATE_PREFIX}${config.systemMode}.${mediaStateFingerprint()}.${config.stateSlot}`; }
function stateDbKey() { return `${DB_STATE_PREFIX}${stateKey()}`; }

function stateExportMediaName() {
  const fallback = activeMedia ? (MEDIA_TEXT[activeMedia.kind] || 'media') : `${config.systemMode}-bios`;
  const name = activeMedia?.archive || activeMedia?.bootPath || activeMedia?.names?.[0] || fallback;
  return sanitizeFilePart(String(name).split(/[\\/]/).pop().replace(/\.[^.]+$/, '') || fallback);
}

function sanitizeFilePart(text) {
  const clean = String(text || '')
    .normalize('NFKD')
    .replace(/[^a-z0-9._-]+/gi, '_')
    .replace(/^_+|_+$/g, '')
    .slice(0, 96);
  return clean || 'pcfx-state';
}

async function getStoredStateBytesForCurrentSlot(options = {}) {
  if (!db) throw new Error('storage database is not ready');
  syncStateSlotFromControl();

  let bytes = normalizeStoredBytes(await dbGet(stateDbKey()));

  // Compatibility for states saved by older builds before v11.  Those states
  // were base64 in localStorage and commonly failed under quota pressure.
  if (!bytes) {
    const legacyPrefixes = ['pcfx.wasm.savestate.v2.', 'pcfx.wasm.savestate.v1.'];
    for (const prefix of legacyPrefixes) {
      const legacyKey = `${prefix}${config.systemMode}.${mediaStateFingerprint()}.${config.stateSlot}`;
      let raw = null;
      try { raw = localStorage.getItem(legacyKey); } catch (_) { raw = null; }
      if (!raw) continue;
      try {
        bytes = base64ToBytes(raw);
        if (options.migrateLegacy) {
          await dbPut(stateDbKey(), bytes);
          try { localStorage.removeItem(legacyKey); } catch (_) {}
        }
        break;
      } catch (_) {
        bytes = null;
      }
    }
  }

  return bytes;
}

function bytesToBase64(bytes) {
  let text = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) text += String.fromCharCode(...bytes.subarray(i, i + chunk));
  return btoa(text);
}

function base64ToBytes(raw) {
  const text = atob(raw);
  const bytes = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i);
  return bytes;
}

function normalizeStoredBytes(value) {
  if (!value) return null;
  if (value instanceof Uint8Array) return value;
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  return null;
}

async function saveStateToIndexedDB() {
  if (!wasm) return;
  if (!db) return updateStorageStatus('storage database is not ready', 'warn');
  if (wasm.pcfx_wasm_get_status?.() !== 3) return updateStorageStatus('start a system or game before saving state', 'warn');
  syncStateSlotFromControl();
  const ok = wasm.pcfx_wasm_save_state();
  if (!ok) return updateStorageStatus('save failed', 'bad');
  const ptr = wasm.pcfx_wasm_get_save_ptr();
  const size = wasm.pcfx_wasm_get_save_size();
  if (!ptr || !size) return updateStorageStatus('save failed: empty state buffer', 'bad');
  const bytes = wasmU8().slice(ptr, ptr + size);

  // Save states are large enough to exceed browser localStorage quotas once
  // base64-expanded.  Store the binary state in IndexedDB instead; localStorage
  // is kept only for small config/manifest JSON.
  await dbPut(stateDbKey(), bytes);

  // Remove any stale pre-v11 localStorage state with the same modern key so a
  // later failed migration cannot shadow the IndexedDB payload.
  try { localStorage.removeItem(stateKey()); } catch (_) {}
  updateStorageStatus(`state slot ${config.stateSlot} saved (${size.toLocaleString()} bytes)`, 'ok');
}

async function loadStateFromIndexedDB() {
  if (!wasm) return;
  if (!db) return updateStorageStatus('storage database is not ready', 'warn');
  if (wasm.pcfx_wasm_get_status?.() !== 3) return updateStorageStatus('start the matching system or game before loading state', 'warn');

  const bytes = await getStoredStateBytesForCurrentSlot({ migrateLegacy: true });
  if (!bytes) return updateStorageStatus(`no state in slot ${config.stateSlot} for this media`, 'warn');
  const ptr = copyBytesToWasm(bytes);
  const ok = wasm.pcfx_wasm_load_state(ptr, bytes.byteLength);
  updateStorageStatus(ok ? `state slot ${config.stateSlot} loaded` : 'state rejected', ok ? 'ok' : 'bad');
  renderOnce();
}

async function exportStateSlotToFile() {
  const bytes = await getStoredStateBytesForCurrentSlot({ migrateLegacy: true });
  if (!bytes) return updateStorageStatus(`no state in slot ${config.stateSlot} for this media to export`, 'warn');

  const mediaPart = stateExportMediaName();
  const systemPart = sanitizeFilePart(config.systemMode);
  const slotPart = String(config.stateSlot).padStart(2, '0');
  const fileName = `${systemPart}_${mediaPart}_slot${slotPart}.pcfxstate`;
  const blob = new Blob([bytes], { type: 'application/octet-stream' });
  const url = URL.createObjectURL(blob);
  try {
    const a = document.createElement('a');
    a.href = url;
    a.download = fileName;
    a.rel = 'noopener';
    document.body.appendChild(a);
    a.click();
    a.remove();
    updateStorageStatus(`exported state slot ${config.stateSlot} (${bytes.byteLength.toLocaleString()} bytes)`, 'ok');
  } finally {
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
}

function saveStateToLocalStorage() {
  saveStateToIndexedDB().catch(e => updateStorageStatus(`save failed: ${e.message}`, 'bad'));
}

function loadStateFromLocalStorage() {
  loadStateFromIndexedDB().catch(e => updateStorageStatus(`load failed: ${e.message}`, 'bad'));
}

function exportStateToFile() {
  exportStateSlotToFile().catch(e => updateStorageStatus(`export failed: ${e.message}`, 'bad'));
}

async function clearAllStorage() {
  for (const key of Object.keys(localStorage)) {
    if (key.startsWith('pcfx.wasm.')) localStorage.removeItem(key);
  }
  if (db) await dbClear();
  config = structuredClone(DEFAULT_CONFIG);
  manifest = { bios: {}, media: null };
  activeBios = null;
  activeMedia = null;
  saveConfig();
  saveManifest();
  applyConfigToControls();
  updateTouchControlsAvailability();
  updateBiosStatus();
  rebuildControlMap();
  resetHostFiles();
  wasm?.pcfx_wasm_init(systemModeValue());
  clearAudioQueue();
  clearCanvasToBlack();
  updateStorageStatus('browser storage cleared', 'warn');
  els.modal.classList.add('open');
  renderOnce();
}

function canvasMousePosition(event) {
  const rect = els.canvas.getBoundingClientRect();
  return [
    (event.clientX - rect.left) * els.canvas.width / rect.width,
    (event.clientY - rect.top) * els.canvas.height / rect.height,
  ];
}

function setSystemMode(mode) {
  config.systemMode = normalizeSystemMode(mode);
  saveConfig();
  applyConfigToControls();
  if (wasm) wasm.pcfx_wasm_set_system_mode(systemModeValue());
  renderOnce();
}

function applyRuntimeOptions() {
  config.enable3D = els.enable3D.checked;
  config.video.aspect = els.aspectMode.value;
  config.video.smoothUpscale = els.smoothUpscale.checked;
  config.video.scanlines = els.scanlines.checked;
  config.video.showFps = els.showFps.checked;
  config.controllerType = els.controllerType?.value === 'mouse' ? 'mouse' : 'gamepad';
  syncStateSlotFromControl();
  config.touchGamepad = { opacity: Math.max(0.2, Math.min(1.0, (parseInt(els.touchOpacity?.value || '72', 10) || 72) / 100)) };
  applyTouchOpacity();
  saveConfig();
  wasm?.pcfx_wasm_set_3d_enabled(config.enable3D ? 1 : 0);
  wasm?.pcfx_wasm_set_browser_smooth(config.video.smoothUpscale ? 1 : 0);
  wasm?.pcfx_wasm_set_controller_type?.(controllerTypeValue());
  if (config.controllerType !== 'mouse' && document.pointerLockElement === els.canvas) document.exitPointerLock?.();
  resetMouseDeltas();
  mouseButtons = 0;
  renderOnce();
}

function populateStateSlots() {
  els.stateSlot.innerHTML = '';
  for (let i = 0; i < 10; i++) {
    const opt = document.createElement('option');
    opt.value = String(i);
    opt.textContent = `Slot ${i}`;
    els.stateSlot.append(opt);
  }
}

function setFullscreen(full) {
  if (full && !document.fullscreenElement) els.screenFrame.requestFullscreen?.();
  else if (!full && document.fullscreenElement) document.exitFullscreen?.();
}

function updateFullscreenUI() {
  const full = !!document.fullscreenElement;
  els.screenFrame.classList.toggle('fullscreen-active', full);
  document.body.classList.toggle('fullscreen-emulation', full);
  els.menuToggle.classList.toggle('hidden', full);
  els.fullscreenToggle.textContent = full ? '⛶' : '⛶';
  requestAnimationFrame(() => renderOnce());
}

function wireEvents() {
  document.querySelectorAll('input[name="systemMode"]').forEach(r => r.addEventListener('change', () => setSystemMode(selectedMenuSystem())));
  document.querySelectorAll('input[name="startupSystemMode"]').forEach(r => r.addEventListener('change', () => setSystemMode(selectedStartupSystem())));

  els.openStartup.addEventListener('click', () => els.modal.classList.add('open'));
  els.menuToggle.addEventListener('click', () => { if (document.pointerLockElement === els.canvas) document.exitPointerLock?.(); toggleSideMenu(); });
  els.closeMenu.addEventListener('click', () => setSideMenuOpen(false));
  els.fullscreenToggle.addEventListener('click', () => setFullscreen(!document.fullscreenElement));
  document.addEventListener('fullscreenchange', updateFullscreenUI);
  window.addEventListener('resize', () => requestAnimationFrame(() => { updateTouchControlsAvailability(); renderOnce(); }));
  if (touchControlsMedia?.addEventListener) touchControlsMedia.addEventListener('change', updateTouchControlsAvailability);
  else touchControlsMedia?.addListener?.(updateTouchControlsAvailability);
  // Audio is unlocked only from explicit user gestures. Do not call this from
  // async startup/media-completion paths, or browsers will reject/resume-block
  // the AudioContext.
  document.addEventListener('click', ev => { unlockAudioFromGesture(ev); }, { capture: true, passive: true });
  document.addEventListener('keydown', ev => { unlockAudioFromGesture(ev); }, { capture: true, passive: true });

  els.pauseToggle.addEventListener('click', () => {
    paused = !paused;
    els.pauseToggle.textContent = paused ? 'Resume' : 'Pause';
    renderOnce();
  });
  els.softResetButton?.addEventListener('click', () => {
    if (!wasm || wasm.pcfx_wasm_get_status?.() !== 3) return updateStorageStatus('start a system or game before soft reset', 'warn');
    const ok = wasm.pcfx_wasm_soft_reset?.();
    clearAudioQueue();
    pressed.clear();
    virtualPressed.clear();
    resetMouseDeltas();
    resetFrameLimiter();
    updateStorageStatus(ok ? 'soft reset' : `soft reset failed; wasm error ${wasmErrorString(wasm.pcfx_wasm_get_error?.())}`, ok ? 'ok' : 'bad');
    renderOnce();
  });

  els.resetButton.addEventListener('click', async () => {
    cancelAnimationFrame(frameRequest);
    loadingMedia = false;
    activeMedia = null;
    clearAudioQueue();
    clearCanvasToBlack();
    wasm.pcfx_wasm_init(systemModeValue());
    resetHostFiles();
    wasm.pcfx_wasm_set_3d_enabled(config.enable3D ? 1 : 0);
    wasm.pcfx_wasm_set_controller_type?.(controllerTypeValue());
    try {
      await loadStoredBios(config.systemMode);
      wasm.pcfx_wasm_start();
    } catch (_) {}
    activeMedia = null;
    resetFrameLimiter();
    frameRequest = requestAnimationFrame(tick);
    renderOnce();
  });
  els.saveState.addEventListener('click', saveStateToLocalStorage);
  els.loadState.addEventListener('click', loadStateFromLocalStorage);
  els.exportState?.addEventListener('click', exportStateToFile);
  els.clearStorage.addEventListener('click', () => clearAllStorage().catch(e => updateStorageStatus(e.message, 'bad')));

  els.pcfxBiosFile.addEventListener('change', () => storeBios('pcfx', els.pcfxBiosFile.files[0]).catch(e => updateStorageStatus(e.message, 'bad')));
  els.pcfxgaBiosFile.addEventListener('change', () => storeBios('pcfxga', els.pcfxgaBiosFile.files[0]).catch(e => updateStorageStatus(e.message, 'bad')));

  
  els.startButton.addEventListener('click', async () => {
    try {
      els.startupError.textContent = '';
      await startSelectedSystem(selectedStartupSystem());
    } catch (e) {
      els.startupError.textContent = e.message;
      updateStorageStatus(e.message, 'bad');
    }
  });

  
  
  els.mediaFiles.addEventListener('change', () => storeMedia(els.mediaFiles.files).catch(e => {
    els.mediaName.textContent = e.message;
    updateStorageStatus(e.message, 'bad');
    if (els.mediaFiles) els.mediaFiles.value = '';
    loadingMedia = false;
  }));
  
  
  els.swapDiscFiles.addEventListener('change', () => swapDisc(els.swapDiscFiles.files).catch(e => {
    els.mediaName.textContent = e.message;
    updateStorageStatus(e.message, 'bad');
    if (els.swapDiscFiles) els.swapDiscFiles.value = '';
    loadingMedia = false;
  }));

  for (const el of [els.enable3D, els.aspectMode, els.smoothUpscale, els.scanlines, els.showFps, els.controllerType, els.stateSlot, els.touchOpacity].filter(Boolean)) {
    el.addEventListener('change', applyRuntimeOptions);
  }

  window.addEventListener('keydown', (e) => {
    if (remapTarget) {
      const map = remapTarget.player === 1 ? config.keysP2 : config.keys;
      map[remapTarget.name] = e.code;
      remapTarget = null;
      saveConfig();
      rebuildControlMap();
      e.preventDefault();
      return;
    }
    if (e.code === 'F11') { setFullscreen(!document.fullscreenElement); e.preventDefault(); return; }
    if (e.code === 'Escape' && document.pointerLockElement === els.canvas) { document.exitPointerLock?.(); e.preventDefault(); return; }
    if (e.code === 'F1') { if (document.pointerLockElement === els.canvas) document.exitPointerLock?.(); toggleSideMenu(); e.preventDefault(); return; }
    if (e.code === 'F5') { saveStateToLocalStorage(); e.preventDefault(); return; }
    if (e.code === 'F7') { loadStateFromLocalStorage(); e.preventDefault(); return; }
    pressed.add(e.code);
    if (Object.values(config.keys).includes(e.code) || Object.values(config.keysP2).includes(e.code)) e.preventDefault();
  });
  window.addEventListener('keyup', (e) => pressed.delete(e.code));

  function applyFocusPause(shouldPause) {
    if (shouldPause) {
      if (!paused) {
        paused = true;
        autoPausedByFocus = true;
        els.pauseToggle.textContent = 'Resume';
      }
      pressed.clear();
      mouseButtons = 0;
    } else if (autoPausedByFocus) {
      paused = false;
      autoPausedByFocus = false;
      els.pauseToggle.textContent = 'Pause';
      resetFrameLimiter();
    }
    renderOnce();
  }

  window.addEventListener('blur', () => applyFocusPause(true));
  window.addEventListener('focus', () => applyFocusPause(false));
  document.addEventListener('visibilitychange', () => applyFocusPause(document.hidden));

  els.screenFrame.addEventListener('click', () => els.screenFrame.focus());
  els.canvas.addEventListener('contextmenu', e => e.preventDefault());
  document.addEventListener('pointerlockchange', () => {
    resetMouseDeltas();
    if (document.pointerLockElement !== els.canvas) mouseButtons = 0;
    renderOnce();
  });
  els.canvas.addEventListener('mousemove', e => {
    [mouseX, mouseY] = canvasMousePosition(e);
    if (config.controllerType === 'mouse' && document.pointerLockElement === els.canvas) {
      mouseDx += e.movementX || 0;
      mouseDy += e.movementY || 0;
    }
  });
  els.canvas.addEventListener('mousedown', e => {
    [mouseX, mouseY] = canvasMousePosition(e);
    if (config.controllerType === 'mouse' && document.pointerLockElement !== els.canvas) {
      els.canvas.requestPointerLock?.();
      resetMouseDeltas();
    }
    mouseButtons |= (1 << e.button);
    els.screenFrame.focus();
    e.preventDefault();
  });
  window.addEventListener('mouseup', e => { mouseButtons &= ~(1 << e.button); });


  function setVirtualButton(name, down) {
    if (down) virtualPressed.add(name); else virtualPressed.delete(name);
  }

  function updateVirtualStick(clientX, clientY) {
    if (!els.touchStick || !els.touchKnob) return;
    const rect = activeTouchStickRect || els.touchStick.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    const max = Math.max(1, rect.width * 0.34);
    let dx = clientX - cx;
    let dy = clientY - cy;
    const len = Math.hypot(dx, dy);
    if (len > max) { dx = dx * max / len; dy = dy * max / len; }
    els.touchKnob.style.setProperty('--stick-x', `${dx}px`);
    els.touchKnob.style.setProperty('--stick-y', `${dy}px`);
    const dead = rect.width * 0.16;
    setVirtualButton('Left', dx < -dead);
    setVirtualButton('Right', dx > dead);
    setVirtualButton('Up', dy < -dead);
    setVirtualButton('Down', dy > dead);
  }

  function releaseVirtualStick() {
    activeTouchStickId = null;
    activeTouchStickRect = null;
    if (els.touchKnob) {
      els.touchKnob.style.setProperty('--stick-x', '0px');
      els.touchKnob.style.setProperty('--stick-y', '0px');
    }
    for (const name of ['Left', 'Right', 'Up', 'Down']) virtualPressed.delete(name);
    els.touchStick?.classList.remove('active');
  }

  els.touchStick?.addEventListener('pointerdown', e => {
    activeTouchStickId = e.pointerId;
    activeTouchStickRect = els.touchStick.getBoundingClientRect();
    els.touchStick.setPointerCapture?.(e.pointerId);
    els.touchStick.classList.add('active');
    updateVirtualStick(e.clientX, e.clientY);
    e.preventDefault();
  });
  els.touchStick?.addEventListener('pointermove', e => {
    if (e.pointerId === activeTouchStickId) { updateVirtualStick(e.clientX, e.clientY); e.preventDefault(); }
  });
  els.touchStick?.addEventListener('pointerup', e => { if (e.pointerId === activeTouchStickId) releaseVirtualStick(); });
  els.touchStick?.addEventListener('pointercancel', e => { if (e.pointerId === activeTouchStickId) releaseVirtualStick(); });
  document.querySelectorAll('.touch-btn').forEach(btn => {
    const name = btn.dataset.button;
    btn.addEventListener('pointerdown', e => { btn.setPointerCapture?.(e.pointerId); btn.classList.add('active'); setVirtualButton(name, true); e.preventDefault(); });
    const up = e => { btn.classList.remove('active'); setVirtualButton(name, false); e.preventDefault(); };
    btn.addEventListener('pointerup', up);
    btn.addEventListener('pointercancel', up);
    btn.addEventListener('pointerleave', e => { if (e.buttons === 0) { btn.classList.remove('active'); setVirtualButton(name, false); } });
  });

  function handleMediaDrop(e) {
    const files = e.dataTransfer?.files;
    if (!files || !files.length) return;
    e.preventDefault();
    e.stopPropagation();
    unlockAudioFromGesture(e);
    storeMedia(files).catch(err => {
      els.mediaName.textContent = err.message;
      updateStorageStatus(err.message, 'bad');
    });
  }
  function allowMediaDrop(e) {
    if (!e.dataTransfer) return;
    e.preventDefault();
    e.stopPropagation();
    e.dataTransfer.dropEffect = 'copy';
    if (els.screenFrame) els.screenFrame.classList.add('drag-active');
  }
  for (const target of [document, window, els.canvas, els.screenFrame].filter(Boolean)) {
    target.addEventListener('dragenter', allowMediaDrop, true);
    target.addEventListener('dragover', allowMediaDrop, true);
    target.addEventListener('drop', handleMediaDrop, true);
  }
  document.addEventListener('dragleave', e => {
    if (!e.relatedTarget || e.clientX <= 0 || e.clientY <= 0 || e.clientX >= window.innerWidth || e.clientY >= window.innerHeight) {
      if (els.screenFrame) els.screenFrame.classList.remove('drag-active');
    }
  }, true);
  document.addEventListener('drop', () => { if (els.screenFrame) els.screenFrame.classList.remove('drag-active'); }, true);
}

async function boot() {
  populateStateSlots();
  applyConfigToControls();
  updateTouchControlsAvailability();
  updateBiosStatus();
  rebuildControlMap();
  wireEvents();
  db = await openDb();
  await instantiateBackend();
  updateStorageStatus('IndexedDB and LocalStorage ready', 'ok');
  if (audioCtx) ensureAudioOutputInstalled();
  if (config.systemMode === 'auto' ? (manifest.bios?.pcfx || manifest.bios?.pcfxga) : manifest.bios?.[config.systemMode]) {
    try {
      await loadStoredBios(config.systemMode);
      els.modal.classList.remove('open');
    } catch (e) {
      els.modal.classList.add('open');
      els.startupError.textContent = e.message;
    }
  } else {
    els.modal.classList.add('open');
  }
  frameRequest = requestAnimationFrame(tick);
}

boot().catch(err => {
  updateStorageStatus(`startup failed: ${err.message}`, 'bad');
});
