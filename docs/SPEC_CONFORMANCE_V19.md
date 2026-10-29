# PCFXemu v19 specification conformance

Official NEC/Hudson documentation remains authoritative. The following is a software implementation audit, not a measurement of physical PC-FX bus timings.

| Feature | v19 status | Specification / qualification |
|---|---|---|
| RAINBOW compressed KRAM input | **Grant-fed and deferred** | HuC6272 §3.4.2; all normal decoder bytes come from staged grant-time snapshots |
| Two inner dummy bytes and three post-block guard words | **Modeled** | Hudson §3.4.2(4), mandatory three-word KRAM guard for inter-block read-ahead |
| HuC6271 Huffman/RLE suspend/resume mid-block | **Not implemented** | Decode is block-atomic after input staging; no symbol-level -REQR FIFO yet |
| Unknown physical input FIFO capacity, grant phase, exact -REQR timing | **Uncalibrated** | Four-clock grant quantum and 128 KiB stage are software model decisions |
| SCSI real DMA byte counter / REQ-ACK backpressure | Retained from v18 | No eager fallback when pending write queue full |
| SCSI DMA KRAM visibility | Deferred | Data becomes visible on modeled K-BUS grant |
| ADPCM KRAM input | Deferred | Grant-time delivery to HuC6230 input latch |
| CPU write latch and per-bank KRAM arbitration | Retained | Single default model; bus phase still approximated |
| KING + dual VDC 1024-clock digital composition, 480i | Preserved | Not a true 1024-wide framebuffer |
| Mode 3 half-dot shift | Existing approximation | Physical half-dot timing not calibrated |
| Old save-state compatibility | Implemented defensively | If new v19 stage fields absent, reconstruct idle input state |
