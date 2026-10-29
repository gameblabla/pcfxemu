# PCFXemu v20 — resumable RAINBOW RLE, documented limits

v20 extends the v19 grant-fed asynchronous K-BUS input path with **incremental, token-atomic RLE decoding** for RAINBOW F0–F3 packets. It keeps the same unified KING bus scheduler and the 1024-clock video mixer. Both SDL3 and headless share the change, with no legacy timing option. JPEG F8/FF remains block-atomic.

See `docs/RAINBOW_INCREMENTAL_RLE_V20.md` for implementation and validation, `docs/SPEC_CONFORMANCE_V20.md` for manual-to-emulator status and `docs/HARDWARE_TESTS_REQUIRED.md` for measurements the official documentation doesn't determine.

The test game, console BIOS, PC-FX CD data and user's still-image assets are NOT redistributed. This release contains source, binaries, tests and validation captures only.
