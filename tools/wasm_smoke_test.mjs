import { readFile, readdir } from 'node:fs/promises';
import path from 'node:path';

const [wasmPath = 'web/pcfx_wasm_core.wasm', biosPath, mediaPath, ...extraPaths] = process.argv.slice(2);
const wasmBytes = await readFile(wasmPath);
const module = new WebAssembly.Module(wasmBytes);
const imports = WebAssembly.Module.imports(module);
const expectedImports = new Set(['pcfx.read_host_file']);
const actualImports = imports.map(i => `${i.module}.${i.name}`);
for (const name of actualImports) if (!expectedImports.has(name)) throw new Error(`unexpected wasm import ${name}`);
let e;
let hostFiles = new Map();
let nextHandle = 1;
const importObject = { pcfx: { read_host_file(handle, offset, size, dest) {
  const bytes = hostFiles.get(handle >>> 0);
  if (!bytes || !e) return 0;
  const off = offset >>> 0;
  if (off >= bytes.byteLength) return 0;
  const len = Math.min(size >>> 0, bytes.byteLength - off);
  new Uint8Array(e.memory.buffer).set(bytes.subarray(off, off + len), dest >>> 0);
  return len >>> 0;
} } };
const instance = await WebAssembly.instantiate(module, importObject);
e = instance.exports;
const required = [
  'memory', 'pcfx_wasm_version', 'pcfx_wasm_init', 'pcfx_wasm_malloc', 'pcfx_wasm_reset_heap', 'pcfx_wasm_load_bios',
  'pcfx_wasm_vfs_add_file', 'pcfx_wasm_vfs_add_host_file', 'pcfx_wasm_set_media_path', 'pcfx_wasm_start', 'pcfx_wasm_swap_disc', 'pcfx_wasm_frame',
  'pcfx_wasm_get_framebuffer', 'pcfx_wasm_get_width', 'pcfx_wasm_get_height', 'pcfx_wasm_get_pitch_pixels', 'pcfx_wasm_get_pixel_format', 'pcfx_wasm_get_bytes_per_pixel',
  'pcfx_wasm_get_status', 'pcfx_wasm_get_error', 'pcfx_wasm_set_controller_type', 'pcfx_wasm_get_controller_type'
];
for (const name of required) if (!(name in e)) throw new Error(`missing export ${name}`);
if (e.pcfx_wasm_get_controller_type) { e.pcfx_wasm_set_controller_type(1); if (e.pcfx_wasm_get_controller_type() !== 1) throw new Error('controller type export failed'); e.pcfx_wasm_set_controller_type(0); }
if (e.pcfx_wasm_version() !== 0x00030012) throw new Error(`unexpected ABI version 0x${e.pcfx_wasm_version().toString(16)}`);

// Regression check for browser option ordering.  The web frontend resets the
// heap/VFS while loading BIOS blobs; that must not discard user-selected
// runtime options immediately before pcfx_wasm_start().
for (const name of [
  'pcfx_wasm_set_bios_patches', 'pcfx_wasm_get_bios_patches',
  'pcfx_wasm_set_cd_speed', 'pcfx_wasm_get_cd_speed',
  'pcfx_wasm_set_adpcm_compat', 'pcfx_wasm_get_adpcm_buggy_codec_mode',
  'pcfx_wasm_get_adpcm_suppress_reset_clicks',
  'pcfx_wasm_set_3d_enabled', 'pcfx_wasm_get_3d_enabled'
]) {
  if (!(name in e)) throw new Error(`missing export ${name}`);
}
e.pcfx_wasm_init(2);
e.pcfx_wasm_set_bios_patches(0x7);
e.pcfx_wasm_set_cd_speed(8);
e.pcfx_wasm_set_adpcm_compat(2, 0);
e.pcfx_wasm_set_3d_enabled(0);
e.pcfx_wasm_set_controller_type?.(1);
e.pcfx_wasm_reset_heap();
if (e.pcfx_wasm_get_bios_patches() !== 0x7) throw new Error('BIOS patch flags lost across reset_heap');
if (e.pcfx_wasm_get_cd_speed() !== 8) throw new Error('CD speed lost across reset_heap');
if (e.pcfx_wasm_get_adpcm_buggy_codec_mode() !== 2 || e.pcfx_wasm_get_adpcm_suppress_reset_clicks() !== 0) throw new Error('ADPCM options lost across reset_heap');
if (e.pcfx_wasm_get_3d_enabled() !== 0) throw new Error('3D option lost across reset_heap');
if (e.pcfx_wasm_get_controller_type?.() !== 1) throw new Error('controller option lost across reset_heap');

function bytesView() { return new Uint8Array(e.memory.buffer); }
function copyIn(bytes) {
  const ptr = e.pcfx_wasm_malloc(bytes.byteLength || 1);
  if (!ptr) throw new Error('wasm malloc failed');
  bytesView().set(bytes, ptr);
  return ptr;
}
function copyString(text) {
  return copyIn(new TextEncoder().encode(text));
}
function addFile(vpath, bytes) {
  const p = copyString(vpath);
  let ok;
  if (bytes.byteLength >= 8 * 1024 * 1024) {
    const handle = nextHandle++;
    hostFiles.set(handle, bytes);
    ok = e.pcfx_wasm_vfs_add_host_file(p, new TextEncoder().encode(vpath).byteLength, handle, bytes.byteLength >>> 0);
  } else {
    const d = copyIn(bytes);
    ok = e.pcfx_wasm_vfs_add_file(p, new TextEncoder().encode(vpath).byteLength, d, bytes.byteLength >>> 0);
  }
  if (!ok) throw new Error(`vfs add failed: ${vpath}`);
}
function mediaKind(name) {
  const n = name.toLowerCase();
  if (n.endsWith('.cue')) return 1;
  if (n.endsWith('.ex') || n.endsWith('.exe')) return 2;
  if (n.endsWith('.chd')) return 3;
  if (n.endsWith('.iso') || n.endsWith('.bin') || n.endsWith('.img')) return 4;
  if (n.endsWith('.m3u')) return 6;
  if (n.endsWith('.toc')) return 7;
  return 4;
}

// ABI-only smoke for CI environments without copyrighted BIOS/media assets.
e.pcfx_wasm_init(0);
const dummy = new Uint8Array(1024 * 1024);
dummy.fill(0x42);
if (!e.pcfx_wasm_load_bios(copyIn(dummy), dummy.byteLength, 1)) throw new Error('dummy BIOS load failed');
if (!biosPath || !mediaPath) {
  console.log('wasm core abi smoke ok', { version: `0x${e.pcfx_wasm_version().toString(16)}`, imports: actualImports, status: e.pcfx_wasm_get_status() });
  process.exit(0);
}

// Optional real-core smoke: pass a BIOS plus a boot media file and any sidecar files.
const realMode = biosPath.toLowerCase().includes('ga') ? 1 : 0;
e.pcfx_wasm_init(realMode);
if (e.pcfx_wasm_set_system_mode) e.pcfx_wasm_set_system_mode(realMode);
const bios = await readFile(biosPath);
if (!e.pcfx_wasm_load_bios(copyIn(bios), bios.byteLength, realMode ? 2 : 1)) {
  throw new Error(`real BIOS load failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);
}
const allMedia = [mediaPath, ...extraPaths];
for (const file of allMedia) addFile(`/media/${path.basename(file)}`, await readFile(file));
const boot = `/media/${path.basename(mediaPath)}`;
const bootPtr = copyString(boot);
if (!e.pcfx_wasm_set_media_path(bootPtr, new TextEncoder().encode(boot).byteLength, mediaKind(mediaPath))) {
  throw new Error(`set media failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);
}
if (!e.pcfx_wasm_start()) throw new Error(`start failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);
for (let i = 0; i < 30; i++) if (!e.pcfx_wasm_frame(0, 0, 0, 0)) throw new Error(`frame failed at ${i}: 0x${e.pcfx_wasm_get_error().toString(16)}`);
const w = e.pcfx_wasm_get_width(), h = e.pcfx_wasm_get_height(), pitch = e.pcfx_wasm_get_pitch_pixels(), fb = e.pcfx_wasm_get_framebuffer();
const pixelFormat = e.pcfx_wasm_get_pixel_format(), bytesPerPixel = e.pcfx_wasm_get_bytes_per_pixel();
if (!fb || w < 240 || w > 512 || h < 200 || h > 256 || pitch < w) throw new Error(`invalid framebuffer ${w}x${h} pitch=${pitch} ptr=${fb}`);
if (pixelFormat !== 2 || bytesPerPixel !== 4) throw new Error(`expected RGBA8888 framebuffer, got format=${pixelFormat} bpp=${bytesPerPixel}`);
console.log('wasm real-core smoke ok', { frames: e.pcfx_wasm_get_frame_count(), width: w, height: h, pitch, pixelFormat, bytesPerPixel, status: e.pcfx_wasm_get_status() });
