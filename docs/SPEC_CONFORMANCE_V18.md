# PCFXemu v18 specification conformance

**Normative source:** Hudson Soft/NEC original Japanese developer documentation. Emulation behavior derived from that documentation is implemented unless marked otherwise. Do not infer that uncalibrated 4-clock grants, 128-entry reservation arrays or analog timing are physical specifications.

| Feature | v18 status | Evidence / caveat |
|---|---|---|
| SCSI real DMA: two received bytes → one KRAM word | Implemented | HuC6272 §3.3.4; `DoRealDMA()` |
| SCSI DMA byte count per received byte | Corrected | HuC6272 §3.3.4, step 7 |
| SCSI REQ/ACK backpressure on unavailable pending write | Implemented at emulator queue boundary | No eager commit, retry same byte; physical latch capacity unknown |
| SCSI DMA data visible only on scheduled K-BUS grant | Implemented in v16, retained | Grant quantization unverified |
| RAINBOW three-word post-block read-ahead | Partial, retained from v17 | Hudson §3.4.2; grant-time snapshots, not a complete FIFO |
| Deferred HuC6271 Huffman/RLE input | **Not implemented** | Parser still reads stream synchronously |
| ADPCM grant-time word capture | Implemented from v16 | Sampling phase not silicon calibrated |
| Per-bank arbitration and refresh | Approximate | Priority specified; absolute phases/durations unknown |
| KRAM posted CPU write retirement | Implemented from v13 onward | Physical latch depth and grant phase need validation |
| Video modes (480i, 1024 mixer, half-dot approximation) | Preserved | Half-dot/analog behavior still approximate |
| Full save/load determinism during overlapping transfers | Incomplete | Pending structures are saved; exhaustive cross-client testing outstanding |
