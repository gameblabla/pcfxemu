# PCFXemu by Gameblabla

This is an improved fork of Mednafen, aiming for accuracy and testing,
mostly aimed for helping me develop my own homebrew games for the PC-FX.

Features :
- World's first PC-FXGA emulator (for the 2 games + homebrew games that can leverage the hardware)
- Entirely C-based
- Webassembly version that does not leverage Emscripten (no annoying startup time)
- Some accuracy improvements for homebrew devs and commercial games (it no longer runs twice as fast as real hardware !)
- Has a headless mode and can be ported to other devices

# TODO

- Redo/remove some of the provided tests for headless mode, some are now broken
- Pipeline improvements. David Shadoff made early findings on his PC-FX worth implementing.
- Allowing cartridge boot of binary executables (as used for PCFXUploader for instance and related), namely the PCFXBoot images.
- MCP Server for AI tools, like GearSystem. There's a headless mode that can do some things but not as flexible
- A proper Qt-like UI, for those that neither like the Web or the basic SDL3 ui.
### HuC6272 K-BUS v9 timing audit

This revision applies bank-local BG contention and removes BG fetch penalties during HBlank, while retaining the existing measured average write-throughput curve. KRAM data reads also respect outstanding CPU KRAM writes; reading the write-address register no longer erases that pending busy period. Both SDL3 and headless share the same core. See `docs/TANK_KBUS_V9.md`, `docs/SPEC_CONFORMANCE_V9.md`, and `docs/HARDWARE_TESTS_REQUIRED.md`. The tank game's red FPS counter remains ~33–34 in the emulator compared to the user's 18–19 FPS physical-console reference; exact arbitration and V810 pipeline timing are still unresolved.


## v11 timing investigation

See [`README_OFFICIAL_HARDWARE_AUDIT_V11.md`](README_OFFICIAL_HARDWARE_AUDIT_V11.md) for the V810 consecutive-width timing fix and non-invasive KRAM microprogram-phase profiler. The 18–19 FPS tank hardware discrepancy remains open.

## v15: one KING timing model and documented K-BUS requests

Both SDL3 and headless now use **only** the posted-write, deferred-commit
KRAM scheduler. The empirical `legacy` model and its command-line/environment
selectors have been removed. The HuC6272 bus model now records word requests
from SCSI DMA, RAINBOW and ADPCM, models the manual's requester priority, and
reserves five refresh operations each HBlank. The read port also arbitrates
rather than bypassing source reservations.

The exact K-BUS electrical phase, refresh distribution between banks,
RAINBOW handshake pacing and non-CPU data-visibility timing remain
**unverified assumptions**; see `docs/KBUS_REQUESTS_V15.md` and
`docs/HARDWARE_TESTS_REQUIRED.md` before interpreting the results as
cycle-accurate. The tank demo measures 20.489 FPS in the emulator against
18–19 FPS reported on physical hardware. Older README and docs sections
referencing the removed legacy option are historical records only.

## v18 documentation-backed SCSI DMA handling

The SCSI receive path now holds REQ/ACK on an unavailable K-BUS word reservation rather than writing KRAM eagerly; its DMA byte count follows HuC6272 §3.3.4. Test with `PCFX_SCSI_BACKPRESSURE_STATS=1` and the user's own RAINBOW game. See `docs/SCSI_BACKPRESSURE_V18.md`, `docs/SPEC_CONFORMANCE_V18.md` and `docs/HARDWARE_TESTS_REQUIRED.md`. Fully deferred RAINBOW decoding is **not** yet implemented.

## HuC6271 RAINBOW async input, v19

The HuC6271 compressed JPEG/RLE source now fetches data through the unified K-BUS grants, capturing bytes on modeled bus completion. It stages the full compressed block (including the documented inner dummy and three zero guard words) before invoking the existing decoder. **Huffman/RLE parsing is not yet symbol-level resumable**, and the model's bus phase and staging capacity are not hardware-verified. See `docs/RAINBOW_ASYNC_INPUT_V19.md` and `docs/HARDWARE_TESTS_REQUIRED.md`. Use `PCFX_RAINBOW_STATS=1` to log the asynchronous producer counters. Both headless and SDL3 use the same implementation.

## v20 RAINBOW incremental RLE

RAINBOW F0–F3 compressed blocks now decode complete run tokens as K-BUS data arrives, instead of delaying all RLE output until the whole 16H block has been fetched. The accurate and fast backends share the incremental path and serialize partial-block progress. JPEG entropy decoding still requires a separate resumable bit-reservoir refactor. The official spec and experimental limits are documented in `docs/RAINBOW_INCREMENTAL_RLE_V20.md`. No legacy KRAM scheduler has been reintroduced.

## v21 RAINBOW JPEG grant-fed input parsing

The v21 core adds incremental F8/FF JPEG input framing, capture of FF's 128
quantization bytes, and atomic handling of entropy `0xFF` stuffing split across
K-BUS grants. The accurate and fast backends still perform JPEG Huffman/IDCT
at full-block granularity; this revision **does not** claim a resumable JPEG
coefficient decoder. See `docs/RAINBOW_JPEG_GRANT_INPUT_V21.md`,
`docs/SPEC_CONFORMANCE_V21.md`, and `docs/HARDWARE_TESTS_REQUIRED.md`.

Set `PCFX_RAINBOW_STATS=1` for JPEG input/grant counters.

## v22 RAINBOW incremental Huffman state

The new grant-fed JPEG Huffman/DC/AC coefficient parser runs alongside the existing JPEG renderer. Missing KRAM data pauses the parser without zero-filling bits; partial symbol progress, DC predictors, AC coefficient positions and data are saved. Pixel production still occurs at the old block boundary, so **do not confuse this with fully asynchronous JPEG rendering**. For details, see `README_OFFICIAL_HARDWARE_AUDIT_V22.md` and `docs/RAINBOW_INCREMENTAL_HUFFMAN_V22.md`.

### v23 HuC6271 grant-fed MCU/IDCT diagnostics

See [v23 implementation and measured parity](docs/RAINBOW_INCREMENTAL_MCU_IDCT_V23.md). Enable `PCFX_RAINBOW_MCU_COMPARE=1` with `PCFX_RAINBOW_STATS=1` to compare incremental luminance IDCT columns with the established accurate/fast RAINBOW decoder. This is disabled by default and does **not** replace displayed pixels until the remaining exceptional strips are resolved. There is still only one unified K-BUS timing model.

## v24 — HuC6261 palette and RAINBOW/ KING color output

The final shared YUV DAC-to-RGB reconstruction has been changed from a
JPEG YCbCr matrix to a conventional analog-YUV matrix. HuC6261 §2.8
specifies linear 8-bit Y/U/V, U/V neutral=0x80, and palette chroma
expanded by zero-filling four low bits. `tests/run_king_v24_yuv_color_test.sh`
covers the component and palette math. This affects all video planes, not
just RAINBOW, and does **not** change KING K-BUS/CPU timing or the 1024-clock
sampling grid. The exact PC-FX analog encoder and CRT colorimetry remain
hardware-test items. See `docs/TETSU_COLOR_CONVERSION_V24.md`,
`docs/SPEC_CONFORMANCE_V24.md`, and `docs/HARDWARE_TESTS_REQUIRED.md`.

## v25 — commit d344a0ad color regression / concealment audit

The shared HuC6261 YUV DAC approximation remains as in v24; NEC does not
specify sufficient motherboard-level RGB colorimetry to invent a new matrix.
The 2026-10-04 commit d344a0ad also introduced an undocumented RAINBOW
"confetti" concealment heuristic which can replace valid JPEG YUV strips
with earlier frames. It is now **off by default**. For controlled comparison
only, set `PCFX_RAINBOW_CONCEALMENT=1` on either frontend.
See `docs/COLOR_REGRESSION_COMMIT_AUDIT_V25.md` and
`tests/run_rainbow_concealment_test.sh`.


## v27 — one inverse-YUV matrix, nearest integer

The full six-coefficient inverse of the conventional analog RGB-to-YUV
matrix is now the **only** TETSU display conversion. Chrominance offsets
are rounded with `lrint()` when the shared 65,536-entry UV table is built.
This is the v26 `inverse-lrint` output, now unconditional.

There is no `PCFX_YUV_MATRIX` runtime selector and no simplified-v25 or
truncation alternatives in the production conversion code. The conversion
applies equally to KING, RAINBOW, and palette-based planes. It remains a
**display approximation**, not a verified PC-FX analog board calibration.

See `docs/YUV_INVERSE_MATRIX_V27.md` and
`tests/run_king_v27_yuv_matrix_test.sh`.

## SDL3 interlace and Mode 2 Form 2 work

The SDL3 frontend also includes pair-locked interlace presentation and sharp
integer scaling; see `README_V35_INTERLACE_SDL3.md`. Mode 2 Form 2 FMV playback
and TRELLIS tooling are documented in
`README_PC_FX_Mode2_Form2_Trellis.md`.
