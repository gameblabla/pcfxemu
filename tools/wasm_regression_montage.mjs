import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { inflateRawSync } from 'node:zlib';

const args = process.argv.slice(2);
if (args.length < 4) {
  console.error('usage: node tools/wasm_regression_montage.mjs web/pcfx_wasm_core.wasm pcfx.rom pcfxga.rom out_dir [--nnyuu N-nyuu.zip] [--maze Maze2D.zip] [--psx psxdemo.zip] [--same samegame.chd] [--team teaminnocent.chd] [--sprite sprite_pcfxga.zip] [--seconds 20] [--mode auto|pcfx|pcfxga]');
  process.exit(2);
}
const [wasmPath, pcfxBiosPath, pcfxgaBiosPath, outDir] = args.splice(0, 4);
let seconds = 20;
let selectedMode = 2; // default: Auto mode
const paths = new Map();
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--seconds') seconds = Number(args[++i] || '20');
  else if (a === '--mode') {
    const modeName = String(args[++i] || 'auto').toLowerCase();
    if (modeName === 'auto') selectedMode = 2;
    else if (modeName === 'pcfx') selectedMode = 0;
    else if (modeName === 'pcfxga') selectedMode = 1;
    else throw new Error(`unsupported --mode ${modeName}; use auto, pcfx, or pcfxga`);
  }
  else if (a.startsWith('--')) paths.set(a.slice(2), args[++i]);
  else throw new Error(`unexpected argument: ${a}`);
}
const cases = [
  { key: 'nnyuu', label: 'nnyuu', mode: selectedMode, schedule: 'nnyuu' },
  { key: 'maze', label: 'maze2d', mode: selectedMode, schedule: 'autorun' },
  { key: 'psx', label: 'psxdemo', mode: selectedMode, schedule: 'autorun' },
  { key: 'same', label: 'samegame', mode: selectedMode, schedule: 'autorun' },
  { key: 'team', label: 'teaminnocent', mode: selectedMode, schedule: 'autorun' },
  { key: 'sprite', label: 'sprite_pcfxga', mode: selectedMode, schedule: 'none' },
].filter(c => paths.has(c.key));
if (!cases.length) throw new Error('no media cases selected');
await mkdir(outDir, { recursive: true });

const td = new TextDecoder();
const te = new TextEncoder();
const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const base = n => String(n).replace(/\\/g, '/').split('/').pop();
const mediaExts = ['.cue', '.ccd', '.bin', '.img', '.iso', '.chd', '.toc', '.m3u', '.wav', '.flac', '.ogg', '.mp3', '.aiff', '.aif', '.ex', '.exe'];
function mediaKindFromNames(names) {
  const lower = names.map(n => n.toLowerCase());
  if (lower.some(n => n.endsWith('.ex') || n.endsWith('.exe'))) return 2;
  if (lower.some(n => n.endsWith('.chd'))) return 3;
  if (lower.some(n => n.endsWith('.m3u'))) return 6;
  if (lower.some(n => n.endsWith('.toc'))) return 7;
  if (lower.some(n => n.endsWith('.cue') || n.endsWith('.ccd'))) return 1;
  if (lower.some(n => n.endsWith('.iso') || n.endsWith('.bin') || n.endsWith('.img'))) return 4;
  return 0;
}
function bootFile(files, kind) {
  const pick = xs => files.find(f => xs.some(x => f.name.toLowerCase().endsWith(x)));
  return pick(kind === 1 ? ['.cue', '.ccd'] : kind === 3 ? ['.chd'] : kind === 6 ? ['.m3u'] : kind === 7 ? ['.toc'] : kind === 2 ? ['.ex', '.exe'] : ['.iso', '.bin', '.img']) || files[0];
}
function extractZip(bytes) {
  let eocd = -1;
  for (let p = bytes.length - 22; p >= Math.max(0, bytes.length - 0x10016); p--) if (u32(bytes, p) === 0x06054b50) { eocd = p; break; }
  if (eocd < 0) throw new Error('zip has no EOCD');
  const entries = u16(bytes, eocd + 10);
  let cd = u32(bytes, eocd + 16);
  const out = [];
  const seen = new Set();
  for (let i = 0; i < entries; i++) {
    if (u32(bytes, cd) !== 0x02014b50) throw new Error('bad central directory');
    const method = u16(bytes, cd + 10), compSize = u32(bytes, cd + 20), nameLen = u16(bytes, cd + 28), extraLen = u16(bytes, cd + 30), commentLen = u16(bytes, cd + 32), local = u32(bytes, cd + 42);
    const full = td.decode(bytes.subarray(cd + 46, cd + 46 + nameLen)).replace(/\\/g, '/');
    cd += 46 + nameLen + extraLen + commentLen;
    if (!full || full.endsWith('/')) continue;
    const name = base(full);
    // Preserve sidecars in ZIPs.  HuEXE/GMAKER programs can open .AIC/.ACD/.AID
    // files at runtime through PIOLIB, so the VFS must receive more than just
    // the bootable .EX/.CUE/.CHD members.
    if (!name || name === '.' || name.startsWith('.') || full.startsWith('__MACOSX/')) continue;
    const key = name.toLowerCase();
    if (seen.has(key)) throw new Error(`duplicate basename in zip: ${name}`);
    seen.add(key);
    if (u32(bytes, local) !== 0x04034b50) throw new Error(`bad local header: ${name}`);
    const ln = u16(bytes, local + 26), le = u16(bytes, local + 28), off = local + 30 + ln + le;
    let data = bytes.slice(off, off + compSize);
    if (method === 8) data = new Uint8Array(inflateRawSync(data));
    else if (method !== 0) throw new Error(`unsupported zip method ${method} for ${name}`);
    out.push({ name, data });
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
}
async function mediaFiles(filePath) {
  const bytes = new Uint8Array(await readFile(filePath));
  if (filePath.toLowerCase().endsWith('.zip')) return extractZip(bytes);
  return [{ name: base(filePath), data: bytes }];
}

let e;
let hostFiles = new Map();
let nextHandle = 1;
const imports = { pcfx: { read_host_file(handle, offset, size, dest) {
  const bytes = hostFiles.get(handle >>> 0);
  if (!bytes || !e) return 0;
  const off = offset >>> 0, want = size >>> 0, to = dest >>> 0;
  if (off >= bytes.byteLength || !want) return 0;
  const mem = new Uint8Array(e.memory.buffer);
  if (to >= mem.byteLength) return 0;
  const len = Math.min(want, bytes.byteLength - off, mem.byteLength - to);
  mem.set(bytes.subarray(off, off + len), to);
  return len >>> 0;
} } };
e = (await WebAssembly.instantiate(await readFile(wasmPath), imports)).instance.exports;
function mem() { return new Uint8Array(e.memory.buffer); }
function copy(bytes) { const p = e.pcfx_wasm_malloc(bytes.byteLength || 1); if (!p) throw new Error('wasm malloc failed'); mem().set(bytes, p); return p; }
function str(s) { const b = te.encode(s); return [copy(b), b.byteLength]; }
function addVfs(name, data) {
  const [p, len] = str(name);
  let ok = 0;
  if (data.byteLength >= 8 * 1024 * 1024) {
    const h = nextHandle++;
    hostFiles.set(h, data);
    ok = e.pcfx_wasm_vfs_add_host_file(p, len, h, data.byteLength >>> 0);
  } else {
    const d = copy(data);
    ok = e.pcfx_wasm_vfs_add_file(p, len, d, data.byteLength >>> 0);
  }
  if (!ok) throw new Error(`VFS add failed: ${name}`);
}
async function loadBioses() {
  for (const [file, kind] of [[pcfxBiosPath, 1], [pcfxgaBiosPath, 2]]) {
    const b = await readFile(file);
    if (!e.pcfx_wasm_load_bios(copy(b), b.byteLength, kind)) throw new Error(`BIOS rejected: ${file}`);
  }
}
function padForFrame(schedule, frame) {
  if (schedule === 'nnyuu') return ((frame >= 60 && frame < 72) || (frame >= 700 && frame < 712)) ? 0x0080 : 0;
  if (schedule === 'none') return 0;
  return (frame % 120) < 12 ? 0x0080 : 0;
}
function captureRgb() {
  const w = e.pcfx_wasm_get_width(), h = e.pcfx_wasm_get_height(), pitch = e.pcfx_wasm_get_pitch_pixels(), ptr = e.pcfx_wasm_get_framebuffer(), bpp = e.pcfx_wasm_get_bytes_per_pixel();
  const src = mem();
  const rgb = Buffer.alloc(w * h * 3);
  for (let y = 0, j = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = ptr + (y * pitch + x) * bpp;
      rgb[j++] = src[i];
      rgb[j++] = src[i + 1];
      rgb[j++] = src[i + 2];
    }
  }
  return { w, h, rgb };
}
async function writePpm(file, img) {
  await writeFile(file, Buffer.concat([Buffer.from(`P6\n${img.w} ${img.h}\n255\n`), img.rgb]));
}
async function runCase(c) {
  e.pcfx_wasm_init(c.mode);
  if (e.pcfx_wasm_set_system_mode) e.pcfx_wasm_set_system_mode(c.mode);
  if (e.pcfx_wasm_set_3d_enabled) e.pcfx_wasm_set_3d_enabled(1);
  hostFiles.clear(); nextHandle = 1; e.pcfx_wasm_reset_heap();
  await loadBioses();
  const files = await mediaFiles(paths.get(c.key));
  const kind = mediaKindFromNames(files.map(f => f.name));
  if (!kind) throw new Error(`${c.label}: unsupported media`);
  for (const f of files) addVfs('/media/' + f.name, f.data);
  const boot = '/media/' + bootFile(files, kind).name;
  const [bp, bl] = str(boot);
  if (!e.pcfx_wasm_set_media_path(bp, bl, kind)) throw new Error(`${c.label}: set_media failed 0x${e.pcfx_wasm_get_error().toString(16)}`);
  if (!e.pcfx_wasm_start()) throw new Error(`${c.label}: start failed 0x${e.pcfx_wasm_get_error().toString(16)}`);
  const totalFrames = Math.max(240, Math.round(seconds * 60));
  const captures = [];
  for (let f = 0; f < totalFrames; f++) {
    const pad = padForFrame(c.schedule, f);
    if (!e.pcfx_wasm_frame(pad, 0, 0, 0)) throw new Error(`${c.label}: frame ${f} failed 0x${e.pcfx_wasm_get_error().toString(16)}`);
    if ((f + 1) % 240 === 0) {
      const img = captureRgb();
      const sec = String((f + 1) / 60).padStart(2, '0');
      const file = path.join(outDir, `${c.label}_${sec}s.ppm`);
      await writePpm(file, img);
      captures.push({ ...img, file, sec });
    }
  }
  console.log(`${c.label}: captured ${captures.length} frames from ${boot}`);
  return { ...c, captures };
}
const results = [];
for (const c of cases) results.push(await runCase(c));

const tileW = Math.max(...results.flatMap(r => r.captures.map(c => c.w)));
const tileH = Math.max(...results.flatMap(r => r.captures.map(c => c.h)));
const cols = Math.max(...results.map(r => r.captures.length));
const rows = results.length;
const montage = Buffer.alloc(cols * tileW * rows * tileH * 3);
for (let r = 0; r < rows; r++) {
  for (let c = 0; c < results[r].captures.length; c++) {
    const img = results[r].captures[c];
    for (let y = 0; y < img.h; y++) {
      const dst = ((r * tileH + y) * cols * tileW + c * tileW) * 3;
      img.rgb.copy(montage, dst, y * img.w * 3, (y + 1) * img.w * 3);
    }
  }
}
const montagePath = path.join(outDir, 'wasm_regression_montage.ppm');
await writeFile(montagePath, Buffer.concat([Buffer.from(`P6\n${cols * tileW} ${rows * tileH}\n255\n`), montage]));
console.log(`wrote ${montagePath}`);
