# PCFXemu v5 — documentation-grounded changes and status

Base: the supplied v4 source tree. Core in `mednafen/pcfx/king.c`, shared by SDL3 and headless. **No PC-FX BIOS is bundled.**

## Implemented (v5)

### HuC6272 HuC6271 compressed-data transfer register I* latches

Official source: `DOCUMENTATION/ORIGINAL_JPN/C6272_1.WRI` (registers 0x41 and 0x43) and `C6272_2.WRI` §3.4.2.

- `R41` (KRAM start address) and `R43` (16-raster block count) have CPU-write values and effective active values. A write during a 16-raster compressed-image block is **deferred to the block boundary**.
- Repeated writes in the block coalesce, with the last write winning. Writes while idle take effect immediately. `R40` enable and `R42` start raster are left with their documented immediate semantics.
- The new latch fields are included in KING save states. Missing fields in old saves reconstruct effective state from the old CPU-register values. Masks still constrain address to 18 bits and count to five bits.
- **Limit:** Decode is still a block-based approximation, not exact byte-level REQR/refresh/bus arbitration. See `HARDWARE_TESTS_REQUIRED.md` KING-03 and KING-04.

### HuC6272 ADPCM transfer read-enable timing

Official source: `DOCUMENTATION/ORIGINAL_JPN/C6272_1.WRI` REG 0x50 and `C6272_2.WRI` §3.5.4.

- A rising READ EN bit is scheduled for the next falling -HSYNC, while a falling READ EN bit stops transfer immediately. The CPU's last write is retained separately from the active transfer control.
- Enabling initializes the channel's KRAM read address at the latch edge, not at the earlier CPU-write time. A write clearing a queued enable before the edge cancels it.
- The audio side observes the same effective control. Rate bits remain programmable (manual prohibits changing rate during transfers), and HSYNC-latched HuC6230 R10 timing from v4 is preserved.
- New latch state participates in KING save states. Old states recover with no pending edge.
- **Limit:** The decoder still lacks an explicit per-HSYNC two-byte capture FIFO and exact first-nibble latency. See AUD-01 and AUD-02 in `HARDWARE_TESTS_REQUIRED.md`.

## Previously implemented (retained)

- HuC6261 R00 horizontal/vertical latches; palette/key/priority HBlank registers; CPR/CPW port semantics; reset SPBL; front cellophane transparent handling.
- HuC6261 5/7 MHz clocks, internal 1024-tick VDC+KING composition, 256/341 physical-dot presentation and mode-3 approximation; 480 distinct 480i rows.
- KING KRAM 1M/4M page routing, partial mid-scanline BG update, partial contention model, SCSI DMA compatibility switches.
- HuC6230 HSYNC-latched R10 and an approximate PCM volume envelope.

## Validation (build environment of this release)

- `bash tests/run_king_v5_register_test.sh`: pass, including deferred R41/R43 writes and the R50 rising/falling edge distinction.
- `bash tests/run_king_v4_register_test.sh`: pass.
- `bash tests/run_soundbox_v4_timing_test.sh`: pass.
- `bash tests/run_dotwidth_test.sh`, `bash tests/run_interlace_bob_test.sh`: pass.
- `make -f Makefile.headless CHD=NO -j8`: compiled.
- `make -f Makefile.sdl3 CHD=NO SDL3_CFLAGS='-Ideps/SDL3/include' SDL3_LIBS='-L/your/SDL3/build -lSDL3' -j8`: compiled against supplied SDL3 3.4.18 offscreen backend.
- User-provided BIOS + 480i test CUE/BIN, 2400 frames: 1024×480 output, pixel-identical to v4 sample. This picture does not exercise RAINBOW block register changes or ADPCM enabling, and so is only a regression test.
- SDL3 offscreen CLI startup: pass. Desktop GPU/window testing not performed.

## Not represented as completed

Exact analog phase, half-dot signal shift, interlaced sync waveform, KRAM arbitration/refresh slots, RAINBOW byte prefetch boundary, SCSI IRQ and data bus timing, HuC6230 sample capture and transfer-cycle establishment, PC-FXGA 3D shader accuracy. These require distinct implementation effort or measurements and are listed separately in `HARDWARE_TESTS_REQUIRED.md`.

## Build/test commands

```sh
bash tests/run_king_v5_register_test.sh
bash tests/run_king_v4_register_test.sh
bash tests/run_soundbox_v4_timing_test.sh
bash tests/run_dotwidth_test.sh
bash tests/run_interlace_bob_test.sh
make -f Makefile.headless CHD=NO -j4
make -f Makefile.sdl3 CHD=NO SDL3_CFLAGS=-Ideps/SDL3/include SDL3_LIBS='-L/path/to/SDL3 -lSDL3' -j4
```

For the 480i homebrew:

```sh
./pcfx-headless --bios-dir /path/to/pcfx.rom --frames 2400 --auto-run \
  --horizontal clock --analog off --screenshot output.png tetsu_hires_480i.cue
```

See `HARDWARE_TESTS_REQUIRED.md` for the distinction between a specification conformance fix and a hardware-dependent accuracy claim.
