# PCFXemu SDL3 interlace + bob (local v35 patch)

## Base and commit identity

This Git history has **two local commits**, not an invented upstream commit ID:
1. `pcfxemu-v2-local-base` imports the user-supplied PCFXemu source archive after reversing its documented v32 headless interlace patch; this is a **v2-based local snapshot**, not cryptographic verification of the official upstream v2 tag.
2. `v35-sdl3-interlace` reapplies the v32 HuC6261 headless changes and adds this SDL3 integration, bob filter, and tests.

The supplied `git format-patch` applies to `pcfxemu-v2-local-base` (verify with `git apply --check`). Applying it to an arbitrary GitHub revision may require rebasing due to intervening code changes. The Git bundle includes both local commits.

## Emulated interlace behavior

- TETSU `picture_mode & 2` enables 480i with alternating 263/262-line fields; KING/TETSU status bit 14 reflects the field parity. The half-dot shift of mode 3 is not modeled.
- Headless core preserves 512x480 pixels and writes each freshly decoded field only to its own parity rows; switching back to progressive restores 240 active lines.
- SDL3 now allocates a 512x480 texture rather than truncating to 240. It honors the returned display height. The `Interlace output` video-menu item offers **weave** or **bob**. `--bob` and `--weave` override the saved preference. Default is weave.
- Bob duplicates **only the last actually mixed odd/even scanlines** vertically to fill the presentation buffer. This prevents temporal combing at the cost of half vertical detail per displayed field. It does not create additional source image detail.
- SDL rendering retains the original vsync setting. Vsync reduces host presentation tearing but is not a guarantee on all GPU drivers/displays.
- Headless screenshots save original woven emulated pixels independent of SDL3 bob display. The SDL3 menu screenshot command likewise uses the emulated framebuffer.

## Build and validation

Compile SDL3 first (for example SDL3 3.4.18):

    make -f Makefile.sdl3 -j8 CHD=NO SDL3_CFLAGS='-I/path/to/SDL3/include' SDL3_LIBS='-L/path/to/lib -lSDL3'
    ./pcfx-sdl3-ui --bios-dir /path/to/bios-directory --bob game.cue

Run bob helper tests with `sh tests/run_interlace_bob_test.sh`. The SDL3 frontend was compiled against SDL3 3.4.18 in a console build because X11/Wayland development headers were not installed; native desktop window/physical-hardware behavior has not been validated.

GPL-2.0 licensing of the original PCFXemu project is retained. No BIOS, game disc or proprietary game data is included.
