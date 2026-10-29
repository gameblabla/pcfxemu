# PCFXemu v11: phase-aware K-BUS profiling and V810 instruction timing

This release builds upon v10. It corrects the V810 store/output consecutive-instruction timing interpretation in NEC's manual and adds **observational-only** bank-A/B × eight-slot microprogram profiling.

The tank demo still runs at about **33.8 in-game FPS** (real hardware: **18–19 FPS, user-reported**) because the verified changes do not justify altering CPU/KRAM wait constants. The main result is the isolation of busy versus idle *issue phases* and a better basis for designing a future validated bus-grant scheduler.

See `docs/TANK_KBUS_PHASE_V11.md`, `docs/SPEC_CONFORMANCE_V11.md`, `docs/HARDWARE_TESTS_REQUIRED.md` for hardware-versus-emulator distinctions, test results and exact manual sections.

Profiler example:

```sh
make -f Makefile.headless PROFILE=1 BUILD_DIR=build/headless-prof-v11 PRGNAME=pcfx-headless-profile
PCFX_KBUS_PHASE_CSV=phase.csv PCFX_KBUS_PC_CSV=writers.csv \
  ./pcfx-headless-profile --bios-dir /path/to/your/bios --state-in tank.state \
  --frames 300 /path/to/tank.cue
```

Unit and runtime check:

```sh
python3 tests/validate_v11_timing.py
bash tests/run_v11_phase_test.sh /path/to/your/bios /path/to/tank.cue /path/to/tank.state
```

No modifications to 1024-clock video, KING palette, audio, or normal controller logic.

### Bundled SDL3 build note

The v11 release archive included a `pcfx-sdl3-ui` executable linked to an
SDL3 3.4.18 **offscreen-only** build in `runtime_libs/libSDL3.so.0`. Those are
generated package files; this source checkout keeps the launcher but does not
track the executable or runtime library. Build `pcfx-sdl3-ui` locally before
using `run-sdl3-offscreen.sh`. For desktop windowing, rebuild/relink against an
SDL3 build with the destination system's X11, Wayland, Windows, or macOS
backend.
