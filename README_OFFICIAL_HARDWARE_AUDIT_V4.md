# PCFXemu v4 — documentation-driven hardware corrections

This is the v4 continuation of `README_HUC6261_TIMING_V3.md` and
`PCFXemu_official_documentation_audit_2026-10-28.md`. It is an **incremental**
patch on the supplied v3 source, **not** a fully cycle-accurate emulator.

## Implemented in this revision

| Audit | Change | Limitation |
|---|---|---|
| A1 | HuC6261 palette offsets and RAINBOW Y/U/V keying use horizontal-latched render-cache values, not live write-side registers | Access contention still not simulated |
| A2 | HuC6261 SPBL R0F resets to FFFFh; shared reset helper | — |
| A3 | KING MOD=0 redirects all modeled KRAM page selections to physical page zero: CPU, DMA, RAINBOW, BG fast/slow, ADPCM. MOD changes recompute page pointers. | Undocumented aliasing/refresh side effects not modeled |
| A4 | KING BG R10 writes during an active raster re-render the remaining 256-dot portion, preserving already scanned dots | BG/microprogram memory fetch is still line-based rather than per-clock; raster split is a visual approximation |
| A6 (partial) | KING fitted contention model excludes microprogram slots belonging to priority-zero BGs | Bus slots, five HBlank refreshes, precise CPU/DMA arbitration remain approximate |
| A7 | Front cellophane with all three source planes transparent outputs unscaled CCR, expanded from YUV 8:4:4 | No independent physical signal capture |
| A8 | R03 is the palette read/auto-increment register; reading the write-only R02 no longer increments; undocumented register reads return a 0xFFFF sentinel | Actual undriven-bus value needs hardware measurement |
| S1 | HuC6230 R10 becomes effective at falling -HSYNC, coalescing multiple writes before that edge; queues and reset pending state in save states; synchronizes KING to CPU-write time before queuing the new value | ADPCM transfer timing is still approximate |
| S2 (approximate) | CDDA electronic VCA progresses in variable-time 1/1023 amplitude steps (near documented 1.49µs/step at high levels, ~7.5ms full fade); write timestamps and HSYNC advance envelope | Not an oscilloscope-calibrated analog VCA; quantized to CPU-write/HSYNC update opportunities |

V3 support retained: 1024-clock HuC6261 composition, 256/341 native-dot
presentation with 1024 raw internal resolution, mode-3 fractional shift
approximation, tunable VDC/KING phase, optional illustrative analog filters,
SDL3 and headless frontends, and corrected HuC6273 priority buffer length.

## Findings still not exactly implementable from the available documentation

**Do not treat these as completed by v4:**

- **A5:** active-display CPR/CPW reads/writes produce display noise, but the official notes do not specify the electrical contention/noise pattern. No artificial random noise has been invented.
- **A6:** exact KING arbitration and five timed HBlank DRAM refresh operations require finer-grained fetch scheduling than the current throughput model. The documented priority-zero improvement is partial only.
- **A9:** half-dot shift remains the v3 1024-clock RGB fractional reconstruction; exact 5/7 MHz analog pulse phase is not established.
- **A10:** framebuffer weave and field metadata exist; analog NTSC equalization/serration/half-line sync waveform generation is outside SDL3/headless RGB output.
- **A11:** VDC/KING phase remains adjustable; the default 18-tick phase is historical, not measured on a console.
- **A12:** SCSI RESET interrupt and raw subchannel/data-bus timing remain the documented uncertainty, not guessed away.
- **A13:** undefined/invalid KING BG sizes and malformed microprogram modes are still approximate; there is no official target behavior for some cases.
- **A14:** component/S-Video presets remain presentation-only bandwidth approximations, not measured retail PC-FX transfer functions.
- **S3:** HuC6272-to-HuC6230 last-two-byte HSYNC capture and both-channel start-up transfer timing remain uncalibrated; R10 control latching is not a replacement for the input FIFO model.
- **G1/G2:** HuC6273 shader, hidden-clear and swap timing approximations relate to PC-FXGA, not stock retail PC-FX; no new claim of exactness is made.

## Tests

From project root:

```sh
bash tests/run_dotwidth_test.sh
bash tests/run_interlace_bob_test.sh
bash tests/run_king_v4_register_test.sh
bash tests/run_soundbox_v4_timing_test.sh
make -f Makefile.headless -j4
make -f Makefile.sdl3 SDL3_CFLAGS='-Ideps/SDL3/include' SDL3_LIBS='-L/path/to/SDL3 -lSDL3' -j4
```

The new tests compile the actual core `king.c` and `soundbox.c` with linker
section garbage collection to exercise their register and control logic
without requiring a BIOS. Runtime image and save-state checks also passed
using the user-provided BIOS (never included).

Example with a locally owned PC-FX BIOS and the supplied example disc:

```sh
./pcfx-headless --bios-dir /path/to/pcfx.rom --frames 2400 --auto-run \
  --horizontal clock --analog off --screenshot frame.png /path/to/tetsu_hires_480i.cue
```

Sample image boot: 1024×480. The v4 boot capture differed from v3 in 65
pixels, while loading a v3 still-image save state and rendering six fields
produced a pixel-identical capture. This is **not** evidence of complete
hardware conformance; it is a continuity/regression check.

SDL3 was compiled with the SDL3 3.4.18 offscreen build and started without
startup errors. Window-system rendering has not been validated on a real
desktop within this environment.

## Applying the source patch

The standalone `pcfxemu_v3_to_v4_hardware.patch` applies to the exact v3
source tree from the previous release. Run from that tree:

```sh
patch -p1 < pcfxemu_v3_to_v4_hardware.patch
```

The supplied ZIP contains the complete v4 source tree (excluding caches
and generated object files), prebuilt Linux x86-64 headless/SDL3 binaries,
tests and a test homebrew. No PC-FX BIOS is distributed.

License remains that of the original Mednafen/PCFXemu files, including GPL
terms and any licenses in the existing dependencies.
