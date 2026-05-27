import { readFile } from 'node:fs/promises';
import path from 'node:path';

const wasmPath = process.argv[2] || 'web/pcfx_wasm_core.wasm';
const pcfxBios = process.argv[3] || '/mnt/data/bios/pcfx.rom';
const pcfxgaBios = process.argv[4] || '/mnt/data/bios/pcfxga.rom';
const maze = process.argv[5] || '/mnt/data/maze_ex/Maze2D/MAZE2D.EX';
const team = process.argv[6] || '/mnt/data/teaminnocent.chd';

let e;
let hostFiles = new Map();
let nextHandle = 1;
const te = new TextEncoder();
const importObject = { pcfx: { read_host_file(handle, offset, size, dest) {
  const bytes = hostFiles.get(handle >>> 0);
  if (!bytes || !e) return 0;
  const off = offset >>> 0;
  const want = size >>> 0;
  const to = dest >>> 0;
  if (off >= bytes.byteLength || !want) return 0;
  const len = Math.min(want, bytes.byteLength - off);
  const mem = new Uint8Array(e.memory.buffer);
  if (to + len > mem.byteLength) {
    console.error('READ_HOST OOB', {handle: handle>>>0, off, want, len, to, mem: mem.byteLength});
    throw new Error('read_host_file memory destination out of bounds');
  }
  mem.set(bytes.subarray(off, off + len), to);
  return len >>> 0;
} } };
const wasmBytes = await readFile(wasmPath);
e = (await WebAssembly.instantiate(wasmBytes, importObject)).instance.exports;
function u8(){ return new Uint8Array(e.memory.buffer); }
function copy(bytes){ const ptr = e.pcfx_wasm_malloc(bytes.byteLength || 1); if(!ptr) throw new Error('malloc fail'); u8().set(bytes, ptr); return ptr; }
function str(s){ const b=te.encode(s); return [copy(b), b.byteLength]; }
function addFile(vpath, bytes){ const [p,plen]=str(vpath); if(bytes.byteLength >= 8*1024*1024){ const h=nextHandle++; hostFiles.set(h, bytes); if(!e.pcfx_wasm_vfs_add_host_file(p, plen, h, bytes.byteLength>>>0)) throw new Error('vfs host add fail '+vpath); return h;} else { const d=copy(bytes); if(!e.pcfx_wasm_vfs_add_file(p,plen,d,bytes.byteLength>>>0)) throw new Error('vfs add fail '+vpath); return 0; } }
async function loadBios(mode){
  hostFiles.clear(); nextHandle=1; e.pcfx_wasm_reset_heap();
  const pcfx = await readFile(pcfxBios); let p=copy(pcfx); if(!e.pcfx_wasm_load_bios(p, pcfx.byteLength, 1)) throw new Error('pcfx bios rejected '+e.pcfx_wasm_get_error().toString(16));
  const pcfxga = await readFile(pcfxgaBios); p=copy(pcfxga); if(!e.pcfx_wasm_load_bios(p, pcfxga.byteLength, 2)) throw new Error('pcfxga bios rejected '+e.pcfx_wasm_get_error().toString(16));
}
async function boot(mode, file, kind, frames){
  console.log('boot', {mode, file, kind});
  e.pcfx_wasm_init(mode); hostFiles.clear(); nextHandle=1; e.pcfx_wasm_set_3d_enabled(1); await loadBios(mode);
  const bytes=await readFile(file); const vpath='/media/'+path.basename(file); addFile(vpath,bytes); const [bp, blen]=str(vpath);
  if(!e.pcfx_wasm_set_media_path(bp,blen,kind)) throw new Error('set media fail '+e.pcfx_wasm_get_error().toString(16));
  if(!e.pcfx_wasm_start()) throw new Error('start fail '+e.pcfx_wasm_get_error().toString(16));
  for(let i=0;i<frames;i++){ if(!e.pcfx_wasm_frame(0,0,0,0)) throw new Error('frame fail '+i+' '+e.pcfx_wasm_get_error().toString(16)); }
  console.log('ok', {frames:e.pcfx_wasm_get_frame_count(), heap:e.pcfx_wasm_heap_used?.(), mem:e.memory.buffer.byteLength, w:e.pcfx_wasm_get_width(), h:e.pcfx_wasm_get_height()});
}
await boot(1, maze, 2, 300);
await boot(0, team, 3, 300);
console.log('sequence ok pcfxga EX -> pcfx CHD');
await boot(1, maze, 2, 300);
await boot(1, team, 3, 300);
console.log('sequence ok pcfxga EX -> pcfxga CHD');
