# PCFXemu v22 conformance status

Official source of truth: Hudson Soft HuC6272 and HuC6261 manuals in the provided toolkit. The existing JPEG entropy codebook implementation is inherited from PCFXemu; electrical timing unaddressed by the official documents remains explicitly uncertain.

| Feature | Status in v22 | Validation |
|---|---|---|
| KING bank-A/B requester scheduler | Single default implementation retained | Prior priority tests |
| Deferred SCSI/ADPCM transactions | Retained from v16-v18 | Prior grant tests |
| RAINBOW grant-fed JPEG block input | Retained from v19-v21 | Existing grant input tests |
| RAINBOW incremental RLE | Retained | Prior RLE test/game |
| RAINBOW JPEG stuffing and FF quantizer header | Incremental grant-fed | v21 split-pair test |
| **JPEG Huffman DC and AC coefficient parser** | **New grant-fed resumable parallel parser** | Synthetic byte-grant test; Makendou Z 6,255 blocks |
| JPEG pixel-producing decoder / IDCT | **Still block-atomic** | Pixel identity to v21 |
| Physical RAINBOW input FIFO depth | Unknown / not emulated precisely | Hardware measurement required |
| REQR electrical and 16H starvation timings | Partly modeled; uncalibrated | Hardware measurement required |
| 480i / 1024-clock output | Unchanged | Photographic homebrew |
| V810 and KING CPU timing | Unchanged | Tank visual regression |

The `RBH_State` snapshot stores a native-C raw state structure; it is compatible with current same-architecture state tests, but cross-architecture padding/endianness portability is not yet established. No real hardware verification was performed.
