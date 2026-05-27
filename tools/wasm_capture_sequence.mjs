import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
const [wasmPath, pcfxBios, pcfxgaBios, firstMedia, firstKindArg, secondMedia, secondKindArg, outPpm, framesArg='300'] = process.argv.slice(2);
let e, hostFiles=new Map(), nextHandle=1; const te=new TextEncoder();
const imports={pcfx:{read_host_file(h,o,s,d){const bytes=hostFiles.get(h>>>0); if(!bytes||!e)return 0; const off=o>>>0,want=s>>>0,to=d>>>0; if(off>=bytes.byteLength||!want)return 0; const mem=new Uint8Array(e.memory.buffer); if(to>=mem.byteLength)return 0; const len=Math.min(want, bytes.byteLength-off, mem.byteLength-to); if(!len)return 0; mem.set(bytes.subarray(off,off+len),to); return len>>>0;}}};
e=(await WebAssembly.instantiate(await readFile(wasmPath), imports)).instance.exports;
function u8(){return new Uint8Array(e.memory.buffer)}; function copy(b){const p=e.pcfx_wasm_malloc(b.byteLength||1); if(!p) throw Error('malloc'); u8().set(b,p); return p}; function str(t){const b=te.encode(t); return [copy(b),b.byteLength]};
async function loadBios(mode){hostFiles.clear(); nextHandle=1; e.pcfx_wasm_reset_heap(); for(const [file,kind] of [[pcfxBios,1],[pcfxgaBios,2]]){const b=await readFile(file); const p=copy(b); if(!e.pcfx_wasm_load_bios(p,b.byteLength,kind)) throw Error('bios reject '+kind);}}
async function add(v,b){const [p,l]=str(v); if(b.byteLength>=8*1024*1024){const h=nextHandle++; hostFiles.set(h,b); if(!e.pcfx_wasm_vfs_add_host_file(p,l,h,b.byteLength>>>0))throw Error('vfs host');} else {const d=copy(b); if(!e.pcfx_wasm_vfs_add_file(p,l,d,b.byteLength>>>0))throw Error('vfs');}}
async function boot(mode, media, kind, frames){e.pcfx_wasm_init(mode); await loadBios(mode); const b=await readFile(media); const vp='/media/'+path.basename(media); await add(vp,b); const [mp,ml]=str(vp); if(!e.pcfx_wasm_set_media_path(mp,ml,kind)) throw Error('set media'); if(!e.pcfx_wasm_start()) throw Error('start '+e.pcfx_wasm_get_error().toString(16)); for(let i=0;i<frames;i++){ if(!e.pcfx_wasm_frame(0,0,0,0)) throw Error('frame '+i); }}
await boot(1, firstMedia, +firstKindArg, 300);
await boot(1, secondMedia, +secondKindArg, +framesArg);
const w=e.pcfx_wasm_get_width(), h=e.pcfx_wasm_get_height(), pitch=e.pcfx_wasm_get_pitch_pixels(), ptr=e.pcfx_wasm_get_framebuffer(), bpp=e.pcfx_wasm_get_bytes_per_pixel();
const mem=u8(); const rgb=Buffer.alloc(w*h*3); let j=0;
for(let y=0;y<h;y++) for(let x=0;x<w;x++){ const i=ptr+(y*pitch+x)*bpp; rgb[j++]=mem[i]; rgb[j++]=mem[i+1]; rgb[j++]=mem[i+2]; }
await writeFile(outPpm, Buffer.concat([Buffer.from(`P6\n${w} ${h}\n255\n`), rgb]));
console.log('captured', {outPpm,w,h,frames:e.pcfx_wasm_get_frame_count()});
