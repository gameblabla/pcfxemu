# PCFXemu v6 — HuC6230 shared ADPCM transfer-cycle model

Base: v5, October 29, 2026. Reference: original NEC/Hudson `DOCUMENTATION/ORIGINAL_JPN/C6230.WRI`, chapter 4.2 (English translation `DOCUMENTATION/ENGLISH_TRANSLATION/C6230/C6230.md`), plus `C6272_2.WRI` §3.5.4. Japanese original governs where the translation is unclear.

## Implemented

1. **Separate source and consumer:** `KING_GetADPCMHalfWord()` is now called from a shared -HSYNC transfer boundary, not from the HuC6230 nibble-decoder loop. The KRAM transfer and the decoder are allowed to run at different instants.
2. **One captured 16-bit word per channel:** Each channel holds an incoming 2-byte word; a later capture replaces unread data. The decoder consumes four low-to-high nibbles from this latch. On absence of a complete captured word, it holds the current output instead of fetching arbitrary newer KRAM data.
3. **Shared transfer-cycle phase:** A single -HSYNC counter drives both channel captures. The model captures at a 2, 4, 8, or 16 HSYNC period depending on the KING ADPCM sampling-divider setting, following the manual's relationship between four nibbles per transfer and the ADPCM sample rate. Disabling one channel doesn't rephase the other.
4. **Reset and startup:** A simultaneous two-channel software reset clears the established transfer phase; the new cycle begins only after reset release. A channel's reset clears its in-flight decoding delta and incomplete word. R10 updates remain deferred until falling -HSYNC, and R50 enables are latched before transfer capture.
5. **Save-state state:** The two input latches, valid flags, shared phase, and established flag are serialized. Older save states start a fresh capture cycle on the next available -HSYNC while preserving in-progress decoder data.

## Tested

- `bash tests/run_soundbox_v6_capture_test.sh`: shared phase; 2-HSYNC fastest period; last captured word; no decoder-origin KRAM fetch; disable; slower common rate; reset and release.
- `bash tests/run_v6_regression.sh`: previous TETSU, KING and audio tests plus new tests.
- Headless RGBA8888 and RGB565 builds; SDL3 3.4.18 offscreen-linked build.
- User BIOS + supplied still-image homebrew, 2400 frames, 1024×480: output matches v5 pixel-for-pixel. The image is silent and is **not** an ADPCM fidelity test.
- v4 still-image save-state loads under v6 with identical output; no compatibility claim for an externally acquired mid-stream ADPCM save has been verified.

## Important limits

- **This is an HSYNC-period functional model, not a measured bus-strobe implementation.** The manual specifies the shared cycle and two-byte capture, but precise KING request/ack strobes, the first audible nibble after reset, and byte-level transfer races still need a physical-console trace.
- The model samples one complete 16-bit KING word at a cycle boundary. It does not emulate individual `-CS`, `RH/-L` byte strobes or partial-byte racing writes. CPU KRAM arbitration, SCSI and RAINBOW bus contention remain approximations.
- A silent still-image playback validates video compatibility only; an ADPCM test ROM and audio waveform comparison are required before claiming corrected game audio fidelity.

See `docs/HARDWARE_TESTS_REQUIRED.md` section AUD for the falsifiable hardware probes and remaining limitations. No PC-FX BIOS is bundled.
