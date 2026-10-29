# HuC6271 / RAINBOW: grant-fed asynchronous input (v19)

**Normative hardware reference:** Hudson Soft's Japanese `DOCUMENTATION/ORIGINAL_JPN/C6272_2.WRI`, section 3.4.2, especially paragraph (4) describing the read-ahead beyond -REQR and three zero guard words. The English translation is under `DOCUMENTATION/ENGLISH_TRANSLATION/C6272_2/C6272_2.md`, lines 319-368.

## Scope: asynchronous transfer with block-atomic decoding

The v18 JPEG and RLE readers called `KING_RB_Fetch()` synchronously. When no three-word read-ahead entry matched, that function read KRAM immediately, then charged a K-BUS reservation *after the fact*. Such a read could observe a CPU/SCSI write that had not yet retired on the modeled bus.

**v19 replaces that data path.** When a 16-raster RAINBOW block is requested, a producer begins requesting 16-bit KRAM words through `KING_KBusReserveTagged()` with requester priority `KBUS_RAINBOW`, respecting BG access slots, competing requests and the two physical KRAM banks. The producer carries its own source pointer independent of the decoder's consumed-byte pointer.

Each granted word is sampled at **grant completion**, appended to the compressed-input staging buffer and used by a small packet-header scanner. The scanner recognizes the existing emulator's `FF F0/F1/F2/F3/F8/FF` block markers, reads the two-byte stored length, and waits for the declared payload plus two inner dummy bytes and the **three zero guard words** required by the manual. It does not synthesize missing compressed bytes to satisfy a grant deadline.

Only after all the packet bytes and guard bytes have arrived does it invoke the existing accurate or fast Huffman/RLE backend. Within that invocation, `KING_RB_Fetch()` reads **only the grant-fed captured buffer**, never live KRAM. Bytes beyond those consumed by the backend are retained for the next 16H block. Source changes discard the older stream; pending grants and the entire 128 KiB staging buffer are included in emulator save states. Field-time rebasing also preserves future grant deadlines.

**Important boundary:** this is a **block-staged asynchronous producer / consumer model**, NOT a cycle-accurate HuC6271 decoder capable of suspending halfway through a Huffman symbol or RLE run. The entropy decoder executes as one block-atomic operation after input staging; quantizer, DC predictors and bit reservoirs are not yet serializable at partial-block granularity. The selected staging capacity, emulator grant quantum (4 CPU cycles), request phase, FIFO depth and exact -REQR assertion/deassertion points are implementation choices, not hardware specifications. v19 does not claim the decoder's actual physical packet latency or FIFO depth.

The old separate `RainbowAhead` state remains only for compatibility with pre-v19 tests and save states. Active v19 decoding grants its trailing guard words through the main asynchronous input producer, so those reads are not reserved a second time. `PCFX_RAINBOW_STATS=1` reports grant-fed words, grant retries, underflows and remaining staged bytes. On unsupported/malformed packets the current backend may still reach its diagnostic underflow fallback; this is **not a substitute for complete resumable decoding** and must not be treated as authentic device behavior.

## Checks performed

* Focused `tests/run_king_v19_rainbow_async_test.sh`: a pending read displaced by HBlank refresh; no early KRAM capture; input bytes independent of later memory mutation; a complete RLE block does not decode until its two dummy and six guard bytes have arrived.
* **Makeruna! Makendou Z**, user's CHD and BIOS, **2,400 fields**, accurate backend: **19,127,415 grant-fed words, zero synchronous decoder KRAM reads, zero underflows**, final 1024×480 frame identical to v18.
* Same game, 1,600 fields, fast backend: **6,922,737 grant-fed words, zero underflows**, final 1024×480 frame identical to v18 fast rendering.
* Continuous 1,200-field game versus 900 fields + save/load + 300 fields: final PNG identical. This is an image-level check, not a proof of all guest RAM/registers and sound samples.
* Photographic 480i homebrew at 2,400 fields: pixel-identical 1024×480 frame against v18.
* Tank demo at 1,500 fields: pixel-identical frame against v18; CPU timing code unchanged.
* Existing RAINBOW read-ahead, SCSI backpressure, ADPCM and bank-arbitration tests passed.
* Headless and SDL3 frontends compile. SDL3 is linked to the included offscreen-capable SDL3 runtime; graphical desktop support requires a desktop-capable SDL3 library.

## Next implementation steps

1. Preserve a true **bounded HuC6271 FIFO** with request/producer watermarks and explicit -REQR on/off state instead of staging a whole packet.
2. Convert both backends' `GetBits`, marker parsing and RLE runs into resumable substates, including quantizer/DC predictors, Huffman bit reservoir and 16-column output progress. Decode only when enough bytes are available for the *next atomic parser operation*.
3. Associate the 16H block-ready, buffer-swap and line-presentation stages with exact documented events. Confirm starvation/recovery behavior under concurrent BG, SCSI and ADPCM traffic.
4. Test malformed headers, variable-length JPEG and RLE, field-boundary starts, transfer-control resets, address/page changes, and split playback.
5. Calibrate electrical grant phase and internal FIFO depth on real PC-FX hardware; record differences between documented and observed behavior as errata, not guessed timing constants.
