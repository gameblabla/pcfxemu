# PCFXemu v6 — documentation-grounded HuC6230 transfer timing

Incremental update to v5. Both frontends use the same updated `soundbox.c` and `king.c` and retain the 1024-clock mixed-rate TETSU renderer, 256/341-dot output modes, mode-3 interlace approximation, and previously implemented fixes.

**v6 change:** the HuC6230 input device models a *shared falling-HSYNC-synchronized 16-bit capture cycle*. KING fetches bytes into a separate input word per ADPCM channel, and decoder nibble consumption occurs later. Data loss/hold, enable/disable, reset/restart and new save-state fields are handled separately. This resolves the major direct-KRAM-on-nibble behavior in v5, though electrical timing remains approximate.

Specification: NEC/Hudson `C6230.WRI` chapter 4.2 and `C6272_2.WRI` §3.5.4. See [the detailed implementation notes](docs/SPEC_CONFORMANCE_V6.md) and [hardware experiments still required](docs/HARDWARE_TESTS_REQUIRED.md).

Build and regression:

```sh
bash tests/run_v6_regression.sh
make -f Makefile.headless CHD=NO -j4
make -f Makefile.sdl3 CHD=NO SDL3_CFLAGS=-Ideps/SDL3/include SDL3_LIBS='-L/path/to/SDL3 -lSDL3' -j4
```

The Linux binaries were built with SDL3 3.4.18 (offscreen driver). Desktop-window operation and real PC-FX electrical timings are not verified. Use an existing legitimately obtained PC-FX BIOS separately; it is not included here.

**Validation caution:** the supplied 480i photo homebrew has no meaningful ADPCM workload. Pixel-identical video verifies only preservation of its video output, not audio fidelity. A dedicated ADPCM homebrew test and analog capture remain necessary.
