# PCFXemu v20 — official HuC6272/HuC6271 conformance

| Behavior | Status | Basis |
|---|---|---|
| RAINBOW word read through shared KING K-BUS | Implemented | HuC6272 §3.4.2; grant phase still estimated |
| Captured data visible only after bus grant | Implemented in v19 | Hudson access arbitration model; phase unmeasured |
| RLE token expansion while data is still arriving | Implemented in v20 | Token-stream semantics, F0–F3; exact per-cycle decode unmeasured |
| RLE stuffing, dummy, and guard bytes | Implemented and tested | Hudson §3.4.2 and established MPCONV packet format |
| RLE partial progress in save states | Implemented and tested at frame boundary | Emulator feature, not a chip requirement |
| JPEG Huffman resumability mid-symbol | **Not implemented** | Requires refactoring entropy backends |
| Explicit physical FIFO depth and -REQR edge timing | **Not verified** | Requires physical measurements |
| Precise K-BUS service-grant phases | **Not verified** | Modeled four-CPU-cycle quantum |
| Frame-buffer swap at 16H boundary | Existing behavior maintained | HuC6272 documentation |

v20 never reintroduces the legacy KRAM timing selector. Both SDL3 and headless share the same v20 core.
