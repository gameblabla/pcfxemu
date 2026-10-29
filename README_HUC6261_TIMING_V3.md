# PCFXemu HuC6261 video timing v3 (SDL3 and headless)

This update applies to the existing 1024-clock/native-dot output implementation.
It was implemented against the **original HuC6261 C6261 documentation** bundled
with the PC-FX toolkit (`DOCUMENTATION/ORIGINAL_JPN/C6261.WRI` and its English
translation at `DOCUMENTATION/ENGLISH_TRANSLATION/C6261/C6261.md`). This does
not assert hardware-verified analog performance or an independently programmable
1024-pixel mode.

## Changes

1. **Control-register (R00) latches:** the CPU reads the value it wrote; output
   bits **8–14** are latched at the next H display period; scan/clock/mode bits
   are latched at the next V period. H-latched plane enables are applied to the
   priority cache; output and scanline timing use the separately latched mode.
   Pending values are saved in save states. Loading previous-state formats
   initializes the new latch from the CPU-written R00 rather than causing a
   black field. **See C6261 section 2.1.3.**
2. **Mode-3 horizontal half-dot shift:** the second interlaced field's pixels
   are shifted by half a native dot, downstream of TETSU composition and
   HuC6273 mixing, leaving 480 independently generated scanlines unchanged.
   A subpixel interpolation/edge-mask approximation is required when converting
   that fractional horizontal phase to an integer RGB framebuffer. In 7MHz mode
   the assumed shift is 1.5 of the 1024 mixer-clock samples; no precise 7MHz
   shift phase is published by the supplied Japanese documentation. Calibration
   remains necessary. **See C6261 section 2.6.3.**
3. **Calibratable VDC/KING HSYNC phase:** the historical, unverified 18-clock
   offset remains the default. `--vdc-phase -64..64` changes the VDC's HSYNC
   timing while keeping total scanline time constant (the earlier `173-18` and
   `120+18` hblank intervals). This is *not* a claim that 18 is measured on
   real hardware; use your own captures to select the phase. **See sections
   2.7 and 2.10** for spatial relationships.
4. **Analog display approximations:** `--analog off|component|svideo` in SDL3
   and headless. `off` is the default, preserving the exact digital mixer.
   The two optional filters apply separate, symmetric luma/chroma kernels to
   a **separate presentation buffer** prior to dot-width resampling. These are
   adjustable *illustrative* approximations: neither the original documentation
   nor our test hardware gives measured PC-FX DAC/cable/encoder/CRT transfer
   functions. Both S-Video and component are analog, not fully digital.
   `pcfx_headless_get_raw_pixels()` always returns the unfiltered 1024-pitch
   mixer framebuffer. **See section 2.8** for analog YUV DAC behaviour.
5. **Interlace timing/field metadata:** retained odd/even 263/262 alternating
   fields and 480-line weave. Field-change tracking exposes the second field's
   half-line vertical offset via
   `pcfx_headless_get_field_half_line_offset()`. This is metadata for a video
   signal encoder; framebuffers/SDL textures **do not synthesize NTSC sync,
   equalisation or serration waveforms**. See section 2.6.2.
6. **HuC6273 priority buffer overflow:** priority data is stored in an
   allocated **1024-byte** buffer, matching the maximum 1024-clock mixer width
   rather than the previous 512 bytes. Layer priority is composed before
   half-dot displacement so the entire final line shifts consistently.

## Frontend use

```sh
./pcfx-headless --bios-dir /path/to/pcfx.rom --frames 1800 --auto-run \
  --horizontal auto --analog off --vdc-phase 18 \
  --screenshot capture.png /path/to/test.cue

./pcfx-sdl3-ui --bios-dir /path/to/pcfx.rom --horizontal clock \
  --analog svideo --vdc-phase 18 /path/to/test.cue
```

SDL3 Video menu: Horizontal output, Analog filter, VDC HSYNC phase (menu
increments calibrated phase within 12..24; CLI supports the entire -64..64
range). SDL3 persists the selected filter and phase in its config.

The core always composes on the fine 1024-clock grid in 7MHz mode. `auto`
retains 1024 samples when both 3-clock and 4-clock edges actually appear in
final mixed output, otherwise presents 256 or 341 dots. `dots` forces native
presentation; it does **not** lower the internal mixer resolution. Analog
filtering is done at the original mixed width **before** optional downsampling.

## Tested

- Both headless RGBA8888 and RGB565 builds compile.
- SDL3 build compiles and links against supplied SDL3 3.4.18 offscreen library;
  starts without SDL errors in offscreen mode. Real desktop windows untested.
- C tests for dot-width selection and field bob pass.
- PC-FX BIOS + homebrew save state: the unfiltered mode-2 output is
  pixel-identical to v2 (1024×480). Old save states load without a half-dark
  field, after two executed fields.
- The test homebrew's pad-II mode-3 toggle modifies exactly **240** interlaced
  output rows, leaving the other 240 unchanged.
- `--vdc-phase 0`, `--analog component`, and `--analog svideo` generate
  different 1024×480 presentation captures as expected. All filters operate
  after source composition, not on the source VDC/KING pixels.

## Still not verified on hardware

Exact horizontal video phase, exact half-dot phase polarity and 7MHz behavior,
DAC/encoder/display frequency response, full NTSC analog sync waveform,
external sync locking, all out-of-range modes and all mid-frame palette
edge cases. Hardware A/B capture is required for those. The filtering presets
are demonstrations rather than calibrated video standards.

## Build

```sh
make -f Makefile.headless CHD=NO -j8
make -f Makefile.sdl3 CHD=NO -j8
sh tests/run_dotwidth_test.sh
sh tests/run_interlace_bob_test.sh
```

Requires a desktop SDL3 development installation for a windowed frontend.
The supplied test library is offscreen-only and must not be mistaken for a
normal desktop SDL3 installation. PC-FX BIOS is **not** included.
