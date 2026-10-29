# PCFXemu v5: official HuC6272 transfer-timing improvements

This is an **incremental update to v4**, with source and SDL3/headless frontends.
NEC/Hudson's November 1995 HuC6272 documentation is treated as authoritative for supported modes. Unknown electrical behavior is **not fabricated**.

**New code:**

1. **RAINBOW/HuC6271 block-transfer register latches.** CPU writes to KING R41 (KRAM start address) and R43 (16-line block count) during an active block take effect after its boundary. Earlier CPU writes are coalesced. Implemented in `mednafen/pcfx/king.c`, with save states and old-state migration. The transfer scheduler is still block-level, not per-byte hardware arbitration.
2. **HuC6230 data transfer enable timing.** KING R50 READ EN rising bits latch on next falling -HSYNC; falling bits clear immediately. Audio is advanced to the register-write/HSYNC boundary under the previous enable state. The existing HuC6230 **R10** HSYNC latch from v4 is preserved. Precise two-byte input capture and first audible nibble remain unmodeled.
3. **Regression tests** exercise the two latch behaviors, existing TETSU palettes/KRAM modes, 256/341/1024 dot-width presentation, interlace bob/weave, and the v4 R10/VCA behavior.

**Two separate documentation files:**

- [`docs/SPEC_CONFORMANCE_V5.md`](docs/SPEC_CONFORMANCE_V5.md): what is implemented, specification sections, testing and limitations.
- [`docs/HARDWARE_TESTS_REQUIRED.md`](docs/HARDWARE_TESTS_REQUIRED.md): prioritized experiments where the official manuals do not determine exact electrical/analog behavior. **These are not claimed bugs or completed emulation work.**

Builds and tests:

```sh
bash tests/run_v5_regression.sh
make -f Makefile.headless CHD=NO -j4
make -f Makefile.sdl3 CHD=NO SDL3_CFLAGS=-Ideps/SDL3/include SDL3_LIBS='-L/path/to/libSDL3 -lSDL3' -j4
```

SDL3 3.4.18 was compiled with an **offscreen** driver in the validation environment; the Linux SDL3 binary requires a suitable system `libSDL3.so.0` for your desktop. Desktop window-system behavior was not checked here. No BIOS is included; the program needs your legitimate PC-FX BIOS file to run retail software.

**Validation summary (2026-10-28):** all five focused tests passed, RGBA8888 headless, RGB565 headless and SDL3 builds succeeded; SDL3 CLI started using the offscreen driver. The supplied user BIOS + photographic 480i homebrew produced 1024×480 and matched v4 pixel-for-pixel. A v4 save-state replay likewise matched v5 at all pixels. The photograph is **not** a direct test of R41/R43 or R50 timing; the focused tests check those.
