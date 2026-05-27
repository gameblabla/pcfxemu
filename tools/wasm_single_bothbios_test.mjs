import { readFile } from 'node:fs/promises';
import path from 'node:path';
const [wasmPath, pcfxBios, pcfxgaBios, mediaPath, modeArg='1', framesArg='300'] = process.argv.slice(2);
let e, hostFiles=new Map(), nextHandle=1; const te=new TextEncoder();
const imports={pcfx:{read_host_file(h,o,s,d){const bytes=hostFiles.get(h>>>0); if(!bytes||!e)return 0; const off=o>>>0,want=s>>>0,to=d>>>0; if(off>=bytes.byteLength||!want)return 0; const mem=new Uint8Array(e.memory.buffer); if(to>=mem.byteLength)return 0; const len=Math.min(want, bytes.byteLength-off, mem.byteLength-to); mem.set(bytes.subarray(off,off+len),to); return len>>>0;}}};
e=(await WebAssembly.instantiate(await readFile(wasmPath), imports)).instance.exports;
function u8(){return new Uint8Array(e.memory.buffer)}; function copy(b){const p=e.pcfx_wasm_malloc(b.byteLength||1); if(!p) throw Error('malloc'); u8().set(b,p); return p}; function str(t){const b=te.encode(t); return [copy(b),b.byteLength]};
function add(v,b){const [p,l]=str(v); if(b.byteLength>=8*1024*1024){const h=nextHandle++; hostFiles.set(h,b); if(!e.pcfx_wasm_vfs_add_host_file(p,l,h,b.byteLength>>>0))throw Error('vfs host');} else {const d=copy(b); if(!e.pcfx_wasm_vfs_add_file(p,l,d,b.byteLength>>>0)) throw Error('vfs');}}
const mode=+modeArg; e.pcfx_wasm_init(mode); hostFiles.clear(); nextHandle=1; e.pcfx_wasm_reset_heap();
let b=await readFile(pcfxBios); let p=copy(b); console.log('load pcfx', e.pcfx_wasm_load_bios(p,b.byteLength,1));
b=await readFile(pcfxgaBios); p=copy(b); console.log('load pcfxga', e.pcfx_wasm_load_bios(p,b.byteLength,2));
const mb=await readFile(mediaPath); const vp='/media/'+path.basename(mediaPath); add(vp,mb); const [mp,ml]=str(vp); console.log('set media', e.pcfx_wasm_set_media_path(mp,ml,3)); console.log('start...'); console.log(e.pcfx_wasm_start(), e.pcfx_wasm_get_error().toString(16)); const frames = +framesArg; for(let i=0;i<frames;i++) { if(!e.pcfx_wasm_frame(0,0,0,0)) throw new Error('frame failed '+i+' '+e.pcfx_wasm_get_error().toString(16)); } console.log('ok', {frames:e.pcfx_wasm_get_frame_count(), width:e.pcfx_wasm_get_width(), height:e.pcfx_wasm_get_height(), heap:e.pcfx_wasm_heap_used?.(), mem:e.memory.buffer.byteLength});
