# v16 — hardware documentation / implementation status

Official Japanese NEC/Hudson HuC6272 and HuC6230 documents govern specified behavior; unstated electrical details require real-console measurements.

| Client | Official rule | v16 implementation | Qualification |
|---|---|---|---|
| CPU writes | BUSY on unfinished access; lower priority than internal clients | One posted latch, later KRAM write visibility | Exact CPU WAIT and grant phase approximate |
| CPU reads | CPU lowest-priority K-BUS access | Wait for prior write and modeled grant | Return timing approximate |
| BG0–3 | Eight microprogram slots per KRAM bank during active display | Per-bank fetch reservations | Host line-batch timing approximated |
| REF | Five refresh accesses per HBlank; highest priority then | Five paired model-bank reservations | Time positions/pairing unmeasured |
| SCSI real DMA | Two received bytes written as one low-byte-first KRAM word | **Deferred data write** at grant completion; priority displacement retimes word | IRQ/count ordering not fully synchronized to grant |
| ADPCM | KRAM word transfer synchronized to HSYNC; higher than RAINBOW during blank | **Deferred KRAM sample and audio input-latch delivery** | Sample rate / exact strobe pending calibration |
| HuC6271 / RAINBOW | Compressed-data requests through priority-arbitrated K-BUS | Bus slot reservation only; **still reads KRAM eagerly** | Requires resumable decoder and request FIFO |
| 1024-clock video / 480i | Separate TETSU video path | Preserved, same capture in regression | Video analog/half-dot phase still approximated |

No legacy timing selector was restored. SDL3 and headless use the same source implementation.

The v16 specific report is `docs/DEFERRED_KBUS_V16.md`, and the hardware-only measurements live in `docs/HARDWARE_TESTS_REQUIRED.md`.
