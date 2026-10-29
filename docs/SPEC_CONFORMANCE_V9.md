# PCFXemu v9 specification conformance increment

The **official original Japanese HuC6272 and NEC V810 manuals** are the normative specification. Translations are helpful indexes. User-reported physical FPS is a baseline observation, not a register-timing specification.

| Feature | v9 behavior | Confidence |
|---|---|---|
| BG microprogram A/B bank selection | 0–7 fetch A; 8–15 fetch B; only target CPU bank charged BG contention | High, documented HuC6272 §3.6.7 |
| HBlank priority | No BG contention surcharge during HBlank; 0-slot measured baseline retained | High for *request classes*, not for latency |
| Incomplete CPU KRAM write preceding a data read | Data read stalls until busy-until then receives the previous empirical 5-cycle latency | Documented request ordering, approximate latency |
| Read of KRAM write-address register | Does not clear an outstanding KRAM write | High-level spec consistent; exact register read side effects unmeasured |
| KING+dual-VDC 1024-clock 480i still | Pixel-identical to previous known capture | Emulation regression |
| Tank real-hardware FPS | 18–19 reported by user | Physical observation supplied by user |
| Tank v9 emulator counter | 34 (after 300 fields from frame-1100 saved state) | Reproduced in emulator |
| Exact per-bank K-BUS arbitration cycles | **Not implemented**; empirical write-throughput curves retained | Hardware testing required |
| Exact V810 posted-write queue | **Not implemented** | Hardware testing required |

See `TANK_KBUS_V9.md` and `HARDWARE_TESTS_REQUIRED.md`. Both SDL3 and headless use the same corrected KING core; no new command-line knobs or display modes were added.
