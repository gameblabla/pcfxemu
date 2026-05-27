import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { inflateRawSync } from 'node:zlib';

const [wasmPath, biosPath, zipPath] = process.argv.slice(2);
if (!wasmPath || !biosPath || !zipPath) {
  console.error('usage: node tools/wasm_zip_smoke_test.mjs web/pcfx_wasm_core.wasm pcfx.rom game.zip');
  process.exit(2);
}
const td = new TextDecoder();
const u16 = (b,o)=>b[o]|(b[o+1]<<8);
const u32 = (b,o)=>(b[o]|(b[o+1]<<8)|(b[o+2]<<16)|(b[o+3]<<24))>>>0;
const base = n => String(n).replace(/\\/g,'/').split('/').pop();
const exts = ['.cue','.ccd','.bin','.img','.iso','.chd','.toc','.m3u','.wav','.flac','.ogg','.mp3','.aiff','.aif','.ex','.exe'];
function mediaKind(files) {
  const names = files.map(f=>f.name.toLowerCase());
  if(names.some(n=>n.endsWith('.ex')||n.endsWith('.exe'))) return 2;
  if(names.some(n=>n.endsWith('.chd'))) return 3;
  if(names.some(n=>n.endsWith('.m3u'))) return 6;
  if(names.some(n=>n.endsWith('.toc'))) return 7;
  if(names.some(n=>n.endsWith('.cue')||n.endsWith('.ccd'))) return 1;
  if(names.some(n=>n.endsWith('.iso')||n.endsWith('.bin')||n.endsWith('.img'))) return 4;
  return 0;
}
function bootFile(files, kind) {
  const pick = xs => files.find(f=>xs.some(x=>f.name.toLowerCase().endsWith(x)));
  return pick(kind===1?['.cue','.ccd']:kind===3?['.chd']:kind===6?['.m3u']:kind===7?['.toc']:kind===2?['.ex','.exe']:['.iso','.bin','.img']) || files[0];
}
function extractZip(bytes) {
  let eocd=-1;
  for(let p=bytes.length-22;p>=Math.max(0,bytes.length-0x10016);p--) if(u32(bytes,p)===0x06054b50){eocd=p;break;}
  if(eocd<0) throw new Error('no EOCD');
  const entries=u16(bytes,eocd+10); let cd=u32(bytes,eocd+16); const out=[]; const seen=new Set();
  for(let i=0;i<entries;i++) {
    if(u32(bytes,cd)!==0x02014b50) throw new Error('bad CDE');
    const method=u16(bytes,cd+10), compSize=u32(bytes,cd+20), nameLen=u16(bytes,cd+28), extraLen=u16(bytes,cd+30), commentLen=u16(bytes,cd+32), local=u32(bytes,cd+42);
    const full=td.decode(bytes.subarray(cd+46,cd+46+nameLen)).replace(/\\/g,'/'); cd += 46+nameLen+extraLen+commentLen;
    if(!full || full.endsWith('/')) continue;
    const name=base(full); if(!exts.some(e=>name.toLowerCase().endsWith(e))) continue;
    if(seen.has(name.toLowerCase())) throw new Error('duplicate basename '+name); seen.add(name.toLowerCase());
    if(u32(bytes,local)!==0x04034b50) throw new Error('bad local '+name);
    const ln=u16(bytes,local+26), le=u16(bytes,local+28), off=local+30+ln+le;
    let data=bytes.slice(off, off+compSize);
    if(method===8) data=new Uint8Array(inflateRawSync(data));
    else if(method!==0) throw new Error('unsupported zip method '+method+' for '+name);
    out.push({name,data});
  }
  return out.sort((a,b)=>a.name.localeCompare(b.name));
}
const wasmBytes = await readFile(wasmPath);
let e;
let hostFiles = new Map();
let nextHandle = 1;
const imports = { pcfx: { read_host_file(handle, offset, size, dest) {
  const bytes = hostFiles.get(handle >>> 0);
  if (!bytes || !e) return 0;
  const off = offset >>> 0;
  if (off >= bytes.byteLength) return 0;
  const len = Math.min(size >>> 0, bytes.byteLength - off);
  new Uint8Array(e.memory.buffer).set(bytes.subarray(off, off + len), dest >>> 0);
  return len >>> 0;
} } };
e=(await WebAssembly.instantiate(wasmBytes,imports)).instance.exports;
const mem=()=>new Uint8Array(e.memory.buffer);
function copy(bytes){const p=e.pcfx_wasm_malloc(bytes.byteLength||1); if(!p) throw new Error('malloc failed'); mem().set(bytes,p); return p;}
function str(s){return copy(new TextEncoder().encode(s));}
function add(name, data){
  const p=str(name); let ok;
  if(data.byteLength >= 8*1024*1024){ const h=nextHandle++; hostFiles.set(h,data); ok=e.pcfx_wasm_vfs_add_host_file(p,new TextEncoder().encode(name).byteLength,h,data.byteLength>>>0); }
  else { const d=copy(data); ok=e.pcfx_wasm_vfs_add_file(p,new TextEncoder().encode(name).byteLength,d,data.byteLength>>>0); }
  if(!ok) throw new Error('vfs add '+name);
}
e.pcfx_wasm_init(0);
const bios=await readFile(biosPath); if(!e.pcfx_wasm_load_bios(copy(bios),bios.byteLength,1)) throw new Error('bios');
const files=extractZip(new Uint8Array(await readFile(zipPath)));
const kind=mediaKind(files); if(!kind) throw new Error('no bootable media in zip');
for(const f of files) add('/media/'+f.name, f.data);
const boot='/media/'+bootFile(files,kind).name;
if(!e.pcfx_wasm_set_media_path(str(boot),new TextEncoder().encode(boot).byteLength,kind)) throw new Error('set media '+(e.pcfx_wasm_get_error()>>>0).toString(16));
if(!e.pcfx_wasm_start()) throw new Error('start '+(e.pcfx_wasm_get_error()>>>0).toString(16));
for(let i=0;i<30;i++) if(!e.pcfx_wasm_frame(0,0,0,0)) throw new Error('frame '+i);
console.log('wasm zip media smoke ok', {files: files.length, boot: path.basename(boot), frames: e.pcfx_wasm_get_frame_count(), heapMiB: Math.round(e.pcfx_wasm_heap_used()/1048576)});
