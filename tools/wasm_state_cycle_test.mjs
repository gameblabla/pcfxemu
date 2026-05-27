import { readFile } from 'node:fs/promises';
import path from 'node:path';

const [wasmPath='web/pcfx_wasm_core.wasm', pcfxBios='/mnt/data/bios/pcfx.rom', pcfxgaBios='/mnt/data/bios/pcfxga.rom', psx='/mnt/data/psx_pkg/psxdemo.ex', team='/mnt/data/teaminnocent.chd'] = process.argv.slice(2);
let e; let hostFiles = new Map(); let nextHandle = 1; const te = new TextEncoder();
const importObject = { pcfx: { read_host_file(handle, offset, size, dest) {
  const bytes = hostFiles.get(handle >>> 0); if(!bytes || !e) return 0;
  const off = offset >>> 0, want = size >>> 0, to = dest >>> 0;
  if(off >= bytes.byteLength || !want) return 0;
  const len = Math.min(want, bytes.byteLength - off);
  new Uint8Array(e.memory.buffer).set(bytes.subarray(off, off + len), to);
  return len >>> 0;
} } };
const wasmBytes = await readFile(wasmPath); e = (await WebAssembly.instantiate(wasmBytes, importObject)).instance.exports;
function mem(){ return new Uint8Array(e.memory.buffer); }
function copyIn(bytes){ const p=e.pcfx_wasm_malloc(bytes.byteLength||1); if(!p) throw new Error('malloc'); mem().set(bytes,p); return p; }
function copyString(s){ const b=te.encode(s); return [copyIn(b), b.byteLength]; }
function add(vpath, bytes){ const [p,l]=copyString(vpath); if(bytes.byteLength >= 8*1024*1024){ const h=nextHandle++; hostFiles.set(h, bytes); if(!e.pcfx_wasm_vfs_add_host_file(p,l,h,bytes.byteLength>>>0)) throw new Error('vfs host'); } else { if(!e.pcfx_wasm_vfs_add_file(p,l,copyIn(bytes),bytes.byteLength>>>0)) throw new Error('vfs'); } }
async function loadBios(){ const pcfx=await readFile(pcfxBios); if(!e.pcfx_wasm_load_bios(copyIn(pcfx), pcfx.byteLength, 1)) throw new Error('pcfx bios'); const pcfxga=await readFile(pcfxgaBios); if(!e.pcfx_wasm_load_bios(copyIn(pcfxga), pcfxga.byteLength, 2)) throw new Error('pcfxga bios'); }
async function boot(mode, media, kind, frames){ e.pcfx_wasm_init(mode); e.pcfx_wasm_set_3d_enabled(1); hostFiles.clear(); nextHandle=1; e.pcfx_wasm_reset_heap(); await loadBios(); const bytes=await readFile(media); const vp='/media/'+path.basename(media); add(vp, bytes); const [p,l]=copyString(vp); if(!e.pcfx_wasm_set_media_path(p,l,kind)) throw new Error('set media '+e.pcfx_wasm_get_error().toString(16)); if(!e.pcfx_wasm_start()) throw new Error('start '+e.pcfx_wasm_get_error().toString(16)); for(let i=0;i<frames;i++) if(!e.pcfx_wasm_frame(0,0,0,0)) throw new Error('frame pre '+i); }
function frameHash(){ const w=e.pcfx_wasm_get_width(), h=e.pcfx_wasm_get_height(), pitch=e.pcfx_wasm_get_pitch_pixels(), bpp=e.pcfx_wasm_get_bytes_per_pixel(), ptr=e.pcfx_wasm_get_framebuffer(); let hash=2166136261>>>0; const m=mem(); for(let y=0;y<h;y++){ const row=ptr+y*pitch*bpp; for(let x=0;x<w*bpp;x++){ hash^=m[row+x]; hash=Math.imul(hash,16777619)>>>0; } } return hash>>>0; }
function saveState(){ if(!e.pcfx_wasm_save_state()) throw new Error('save failed'); const p=e.pcfx_wasm_get_save_ptr(), n=e.pcfx_wasm_get_save_size(); if(!p||!n) throw new Error('empty save'); return new Uint8Array(mem().slice(p,p+n)); }
function loadState(bytes){ const p=copyIn(bytes); if(!e.pcfx_wasm_load_state(p, bytes.byteLength>>>0)) throw new Error('load failed'); }
function run(frames){ for(let i=0;i<frames;i++) if(!e.pcfx_wasm_frame(0,0,0,0)) throw new Error('frame post '+i); }
async function cycle(label, mode, media, kind, pre=300, post=120){ console.log(label,'boot'); await boot(mode,media,kind,pre); const state=saveState(); const saved=frameHash(); run(post); const played=frameHash(); loadState(state); run(post); const reload1=frameHash(); loadState(state); run(post); const reload2=frameHash(); if(played!==reload1 || played!==reload2) throw new Error(label+' deterministic mismatch '+[played,reload1,reload2].join(',')); console.log(label,'ok',{stateBytes:state.byteLength,saved,played,frames:e.pcfx_wasm_get_frame_count()}); return state; }
const psxState=await cycle('psxdemo_ex',1,psx,2,600,120);
const teamState=await cycle('teaminnocent',0,team,3,300,60);
console.log('switch back psx'); await boot(1,psx,2,120); loadState(psxState); run(120); console.log('switch back team'); await boot(0,team,3,120); loadState(teamState); run(60); console.log('wasm state cycle ok');
