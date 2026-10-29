# PCFXemu v21 — HuC6271/HuC6272 conformance

| Behavior | v21 status | Documentation/evidence |
|---|---|---|
| Two KRAM banks, priority-scheduled RAINBOW compressed data | Existing implementation retained | Hudson HuC6272 §3.4.2; exact electrical grant timing unverified |
| FF quantization-table bytes arriving across K-BUS grants | Implemented as incremental input parsing | FF block framing, 128 quant bytes |
| F8 entropy byte stuffing across 16-bit word boundary | Implemented as atomic two-byte step | Existing JPEG bitstream convention; test covers split input |
| 16H block inner dummy bytes and three guard words | Existing behavior retained | HuC6272 §3.4.2 |
| Incremental RLE tokens | Implemented in v20 and preserved | F0–F3 compressed data |
| Incremental JPEG Huffman DC/AC coefficient machine | **Not implemented** | Existing accurate/fast decoders still block-atomic |
| Bounded physical input FIFO and -REQR watermark | **Not implemented** | Physical capacity and phase require measurement |
| Save-state JPEG input parser state | Implemented | Emulator design; v21 continuation regression |
| Real PC-FX timing calibration | **Not performed** | Requires console measurements |

The unified KRAM timing path remains the only supported scheduler; v21 adds no legacy mode. No fixed additional wait state was introduced to match a specific game's FPS. Exact internal buffer size and -REQR phase are explicitly not claimed to come from the official documentation.
