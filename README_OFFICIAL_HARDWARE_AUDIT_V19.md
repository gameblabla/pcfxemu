# PCFXemu v19 — HuC6271 asynchronous input staging

The KING RAINBOW input path now honors **modeled K-BUS grant-time visibility for ordinary compressed JPEG/RLE words**, in addition to the three trailing guard words. The two existing decoding backends consume a completed captured block, not live KRAM. This change is shared by SDL3 and headless; no legacy bus scheduler was reintroduced.

The normative source is Hudson Soft's Japanese `C6272_2.WRI`, §3.4.2. `docs/RAINBOW_ASYNC_INPUT_V19.md` details the architecture and why this is still block-atomic, not a resumable Huffman engine. `docs/HARDWARE_TESTS_REQUIRED.md` records the remaining silicon-calibration questions separately.

The RAINBOW case provided by the user, *Makeruna! Makendou Z*, is used for validation but is **not redistributed**, nor is the supplied PC-FX BIOS. The complete tested outputs include the 2,400-frame 1024×480 image (pixel-identical to v18), zero synchronous compressed-data reads, zero decoder underflows, and save/load image equivalence. The fast decoder backend, tank demo and 480i still image also pass their corresponding comparison tests. See `docs/SPEC_CONFORMANCE_V19.md`.
