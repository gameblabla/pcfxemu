# PCFXemu v23 — specification conformance update

This is an incremental update on v22; previous v4–v22 documentation remains applicable.

| Behavior | Official doc | v23 | Qualification |
|---|---|---|---|
| HuC6271 16-line strips, compressed input and guard data | HuC6272 part 2 §3.4 | Existing grant-fed implementation | Service-grant phase not hardware-calibrated |
| Huffman DC/AC state across KRAM grants | Derived from existing verified decoder tables | Incremental parser with persistent signed coefficients | Emulator algorithm, not measured silicon latency |
| MCU luminance IDCT when six components complete | Derived from decoder algorithms | New **diagnostic** output and read-only comparison | Not yet used as displayed output |
| Quantization scale including zero and trailing scale commands | Existing decoder control stream | Preserved correctly | Four exceptional blocks still differ |
| Chroma output / interpolation from grant-fed coefficients | Not yet integrated | Still uses established decoder | Pending |
| Bus request ordering: BG, RAINBOW, SCSI, CPU during active display; refresh/ADPCM/RAINBOW/SCSI/CPU in HBlank | HuC6272 | Retains v22 unified K-BUS path | Actual grant times and FIFO depth uncertain |
| SDL3/headless 1024-clock and 480i output | HuC6261 | Preserved | Analog bandwidth and phase need hardware verification |

The diagnostic flag `PCFX_RAINBOW_MCU_COMPARE=1` does not alter emulated KRAM grants, guest CPU timing or pixels. Its host CPU cost is not a hardware timing model.
