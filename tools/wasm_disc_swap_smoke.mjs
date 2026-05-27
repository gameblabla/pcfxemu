import { readFile } from 'node:fs/promises';
import path from 'node:path';

const [wasmPath, biosPath, firstBoot, secondBoot, ...sidecars] = process.argv.slice(2);
if (!wasmPath || !biosPath || !firstBoot || !secondBoot) {
  console.error('usage: node tools/wasm_disc_swap_smoke.mjs core.wasm pcfx.rom first.cue second.chd [first/second sidecars...]');
  process.exit(2);
}
const wasmBytes = await readFile(wasmPath);
const module = new WebAssembly.Module(wasmBytes);
let e;
let nextHandle = 1;
const hostFiles = new Map();
const imports = { pcfx: { read_host_file(handle, offset, size, dest) {
  const bytes = hostFiles.get(handle >>> 0);
  if (!bytes || !e) return 0;
  const off = offset >>> 0;
  if (off >= bytes.byteLength) return 0;
  const len = Math.min(size >>> 0, bytes.byteLength - off);
  new Uint8Array(e.memory.buffer).set(bytes.subarray(off, off + len), dest >>> 0);
  return len >>> 0;
}}};
const instance = await WebAssembly.instantiate(module, imports);
e = instance.exports;
if (!e.pcfx_wasm_swap_disc) throw new Error('missing pcfx_wasm_swap_disc export');
const enc = new TextEncoder();
const bytesView = () => new Uint8Array(e.memory.buffer);
function copyIn(bytes) {
  const ptr = e.pcfx_wasm_malloc(bytes.byteLength || 1);
  if (!ptr) throw new Error('wasm malloc failed');
  bytesView().set(bytes, ptr);
  return ptr;
}
function copyString(text) { return copyIn(enc.encode(text)); }
function mediaKind(file) {
  const n = file.toLowerCase();
  if (n.endsWith('.cue') || n.endsWith('.ccd')) return 1;
  if (n.endsWith('.chd')) return 3;
  if (n.endsWith('.m3u')) return 6;
  if (n.endsWith('.toc')) return 7;
  return 4;
}
async function addFile(vpath, file) {
  const data = await readFile(file);
  const pathPtr = copyString(vpath);
  let ok = 0;
  if (data.byteLength >= 8 * 1024 * 1024) {
    const handle = nextHandle++;
    hostFiles.set(handle, data);
    ok = e.pcfx_wasm_vfs_add_host_file(pathPtr, enc.encode(vpath).byteLength, handle, data.byteLength >>> 0);
  } else {
    ok = e.pcfx_wasm_vfs_add_file(pathPtr, enc.encode(vpath).byteLength, copyIn(data), data.byteLength >>> 0);
  }
  if (!ok) throw new Error(`vfs add failed: ${vpath}`);
}
function vpath(prefix, file) { return `${prefix}/${path.basename(file)}`; }

const mode = biosPath.toLowerCase().includes('ga') ? 1 : 0;
e.pcfx_wasm_init(mode);
const bios = await readFile(biosPath);
if (!e.pcfx_wasm_load_bios(copyIn(bios), bios.byteLength, mode ? 2 : 1)) throw new Error(`BIOS load failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);

const firstFiles = [firstBoot, ...sidecars.filter(f => path.dirname(f) === path.dirname(firstBoot) || path.basename(f).toLowerCase().includes(path.basename(firstBoot, path.extname(firstBoot)).toLowerCase()))];
for (const f of firstFiles) await addFile(vpath('/media/first', f), f);
const firstPath = vpath('/media/first', firstBoot);
if (!e.pcfx_wasm_set_media_path(copyString(firstPath), enc.encode(firstPath).byteLength, mediaKind(firstBoot))) throw new Error(`set first media failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);
if (!e.pcfx_wasm_start()) throw new Error(`start failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);
for (let i = 0; i < 10; i++) if (!e.pcfx_wasm_frame(0, 0, 0, 0)) throw new Error(`pre-swap frame failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);
const before = e.pcfx_wasm_get_frame_count();

const secondFiles = [secondBoot, ...sidecars.filter(f => path.dirname(f) === path.dirname(secondBoot) || path.basename(f).toLowerCase().includes(path.basename(secondBoot, path.extname(secondBoot)).toLowerCase()))];
for (const f of secondFiles) await addFile(vpath('/media/second', f), f);
const secondPath = vpath('/media/second', secondBoot);
if (!e.pcfx_wasm_swap_disc(copyString(secondPath), enc.encode(secondPath).byteLength, mediaKind(secondBoot))) throw new Error(`swap failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);
for (let i = 0; i < 10; i++) if (!e.pcfx_wasm_frame(0, 0, 0, 0)) throw new Error(`post-swap frame failed: 0x${e.pcfx_wasm_get_error().toString(16)}`);
console.log('wasm disc swap smoke ok', { version: `0x${e.pcfx_wasm_version().toString(16)}`, before, after: e.pcfx_wasm_get_frame_count(), mediaKind: e.pcfx_wasm_get_media_kind(), status: e.pcfx_wasm_get_status(), hostFiles: hostFiles.size });
