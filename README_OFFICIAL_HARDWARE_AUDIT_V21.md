# v21 audit: HuC6271 compressed JPEG input

v21 implements a save-state-compatible input-side JPEG parser for the documented
F8/FF compressed stream format. It tracks quantization bytes and entropy FF
stuffing atomically as K-BUS grants complete. This moves the input pipeline
forward while preserving both existing JPEG renderers and the existing unified
KING bus arbitration model. See `docs/RAINBOW_JPEG_GRANT_INPUT_V21.md` for
measurements, the exact scope, and the required real-hardware tests.

**Not yet fixed:** actual mid-Huffman-symbol suspension, incremental MCU/IDCT
rendering, physical HuC6271 input FIFO depth and analog timing. These require a
separate backend refactor; v21 intentionally does not fabricate missing bits
or invent bus latencies.
