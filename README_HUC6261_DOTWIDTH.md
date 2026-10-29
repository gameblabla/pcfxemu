# PC-FX HuC6261 1024-clock output with native dot-width presentation

This patch modifies the PCFXemu **SDL3 and headless** frontends in the provided
PC-FX toolkit. It does **not** modify the PC-FX's hardware capabilities or
claim that 1024 arbitrary-color pixels can be independently programmed.

## Internal video and output modes

In 7.16 MHz high-dot-clock mode TETSU composites at 1024 master-clock samples
per reference picture span. VDC pixels hold for 3 samples, KING and RAINBOW
pixels for 4.  This fine timing is always kept in the 1024-pixel-pitch internal
framebuffer. The 5.37 MHz low-clock mode uses 256 effective picture samples.

`--horizontal auto|dots|clock` (both frontends) selects output conversion:

- **auto** (default): inspects *visible changes in the final mixed signal*,
  rather than just configured layers.  A 5 MHz picture outputs 256 samples;
  a VDC-only high-clock picture outputs 341 samples; a KING/RAINBOW-only
  high-clock picture outputs 256 samples. If the final picture has edges on
  both 3-clock and 4-clock grids, or has non-grid transitions, retains the full
  **1024** clock samples. Ambiguous edges at multiples of 12 do not count as
  proof of mixed clocks. Auto scans the full current interlaced frame.
- **dots**: always present 256 dots for low clock, 341 dots for high clock,
  even if some mixed-clock detail is lost in the **presentation**. Internal
  mixing remains full resolution. Appropriate for low-resolution screenshots
  and integer-dot scaling.
- **clock** (or `1024`): always present all 1024 samples in high-clock mode.
  5 MHz content remains 256; it has no extra high-clock detail to expose.

The 341-wide conversion samples the three-clock pixels at `3*x+1`;
the 256-wide conversion samples the four-clock pixels at `4*x+2`.
This avoids inventing interpolation artifacts across hardware layer
boundaries. Screenshots/Y4M and SDL3 use the same presentation conversion.

**Independent access to the raw framebuffer** is possible through the new
`pcfx_headless_get_raw_pixels()` accessor, which returns a 1024-pitch 480-row
framebuffer regardless of presentation. Its pitch is in pixels and format is
identical to `pcfx_headless_get_pixels()`; choose raw output if developing an
analog filtering/scaler or video-analysis tool.

### SDL3 geometry

The SDL3 frontend uses a 1024x480 texture (rather than truncating to 512)
with full 480i weave/bob support. The Video menu adds **Horizontal output**
(automatic/native dots/1024 mixer clocks), configurable through CLI and saved
in `~/.pcfxemu/` with other frontend preferences.

`--integer-dots` (also `--native-aspect`) maps 1024 mixer ticks to the
**341-dot physical span** for integer scaling rather than treating 1024 clocks
as 1024 physical-width picture pixels. `--integer-scale` provides integer
vertical scaling **with the correct 4:3 display aspect ratio**. Default
aspect is normal 4:3, which is generally the best approximation of a TV.
`--nearest` / `--linear`, `--bob` / `--weave` remain supported.

### Headless examples

```
./pcfx-headless --bios-dir /path/to/pcfx.rom --auto-run --frames 1550 \
  --horizontal auto --screenshot image-auto.png test.cue
./pcfx-headless --bios-dir /path/to/pcfx.rom --auto-run --frames 1550 \
  --horizontal dots --screenshot image-native.png test.cue
./pcfx-headless --bios-dir /path/to/pcfx.rom --auto-run --frames 1550 \
  --horizontal clock --screenshot image-1024.png test.cue
```

### SDL3 examples

```
./pcfx-sdl3-ui --bios-dir /path/to/BIOS-dir --horizontal auto test.cue
./pcfx-sdl3-ui --horizontal clock --integer-scale test.cue
./pcfx-sdl3-ui --horizontal dots --integer-dots test.cue
```

### Build

```
make -f Makefile.headless -j8 CHD=NO
# Requires SDL3 3.4.x installed; the user-supplied SDL3 3.4.18 works:
make -f Makefile.sdl3 -j8 CHD=NO
sh tests/run_dotwidth_test.sh
sh tests/run_interlace_bob_test.sh
```

On the test container SDL3 was built with `SDL_UNIX_CONSOLE_BUILD=ON` because
X11/Wayland development dependencies are unavailable. Therefore its included
SDL3 binary was build- and offscreen-smoke-tested, **not tested in a desktop
window**. Rebuild SDL3 for the destination desktop environment and relink the
frontend to enable a normal UI.

### Verified in the supplied test environment

- BIOS + original interlaced KING/dual-VDC homebrew: auto -> 1024x480
- Same saved game state: dots -> 341x480; clock -> 1024x480
- auto and clock screenshots pixel-identical for the still image
- dots screenshot pixel-identical to 3-clock-center samples of clock image
- automated unit tests: 256/341/1024 modes, simulated mixed signal,
  sample alignment, RGB565/RGBA8888 and ambiguous coincident edges
- existing interlace bob test passes
- SDL3 UI and headless builds successful (including headless RGB565)

This patch does not implement TETSU's mode-3 half-dot shift or simulate real
PC-FX analog DAC / CRT filtering. Actual analog resolution remains a separate
hardware measurement. It also does not pretend that a 1024-clock mixed signal
is an arbitrary 1024-pixel graphics mode.
