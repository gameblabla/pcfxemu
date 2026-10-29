# PCFXemu v23 — RAINBOW incremental MCU reconstruction

This release extends the v22 grant-fed Huffman parser with per-MCU accurate/fast IDCT **luminance** reconstruction and direct comparison against the current renderer. The optional comparison validates most of the real game stream, but does not yet replace displayed pixels.

- Regression images for Makendou, the tank demo and the 480i homebrew are unchanged.
- 6,236 of 6,240 completed JPEG strips matched all compared luminance samples in a cold-boot 1600-frame Makendou run (accurate decoder); four exceptional strips remain under investigation.
- The quantizer-scale-zero bug was corrected in this experimental pipeline; quantizer rescale codes following the final MCU are also recognized.
- Diagnostics are opt-in with `PCFX_RAINBOW_MCU_COMPARE=1` and `PCFX_RAINBOW_STATS=1`.
- Save state support and old v22 loads remain; default CPU/K-BUS/video behavior is unchanged.

See `docs/RAINBOW_INCREMENTAL_MCU_IDCT_V23.md`, `docs/SPEC_CONFORMANCE_V23.md`, and `docs/HARDWARE_TESTS_REQUIRED.md`.
