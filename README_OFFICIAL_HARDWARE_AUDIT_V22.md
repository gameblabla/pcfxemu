# PCFXemu v22 — resumable RAINBOW Huffman coefficient progress

Continuing the HuC6272/RAINBOW work, v22 adds an actual grant-by-grant JPEG Huffman and signed coefficient state machine alongside the stable block-atomic pixel renderer. It models logical input consumption, not undocumented physical FIFO size or cycle-exact IDCT timing. Both SDL3 and headless share the implementation.

See `docs/RAINBOW_INCREMENTAL_HUFFMAN_V22.md`, `docs/SPEC_CONFORMANCE_V22.md` and `docs/HARDWARE_TESTS_REQUIRED.md` for the implementation, tested boundaries and real-hardware questions. The unified KRAM scheduler remains the only default. No legacy timing or new blanket CPU stalls were added.
