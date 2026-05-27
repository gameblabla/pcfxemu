import { readFile } from 'node:fs/promises';

const source = await readFile('web/pcfx-wasm.js', 'utf8');
if (source.includes('BigInt(')) throw new Error('browser media loader should not use BigInt for byte-size accounting');
if (!source.includes("typeof file.bytes === 'function'")) throw new Error('fileToBytes does not handle File.bytes() method');
if (!source.includes('file.bytes instanceof Uint8Array')) throw new Error('fileToBytes does not preserve ZIP Uint8Array entries');

const markerStart = source.indexOf('async function fileToBytes(file)');
const markerEnd = source.indexOf('\n\nfunction stringToWasm', markerStart);
if (markerStart < 0 || markerEnd < 0) throw new Error('could not locate fileToBytes body');
const helperSource = `function baseName(name){ return String(name || '').replace(/\\\\/g, '/').split('/').pop() || 'file'; }\n${source.slice(markerStart, markerEnd)}\nreturn fileToBytes;`;
const fileToBytes = new Function(helperSource)();

const zipped = new Uint8Array([1,2,3,4]);
if (await fileToBytes({ name: 'z.bin', bytes: zipped }) !== zipped) throw new Error('Uint8Array ZIP entry was not returned as-is');

const direct = await fileToBytes({ name: 'N-nyuu_pcfxga.chd', bytes: async () => new Uint8Array([5,6,7]) });
if (!(direct instanceof Uint8Array) || direct.byteLength !== 3 || direct[0] !== 5) throw new Error('File.bytes() method case failed');

const fallback = await fileToBytes({ name: 'fallback.chd', arrayBuffer: async () => new Uint8Array([8,9]).buffer });
if (!(fallback instanceof Uint8Array) || fallback.byteLength !== 2 || fallback[1] !== 9) throw new Error('arrayBuffer fallback case failed');

console.log('browser fileToBytes unit ok');
