# PCFXemu v15 — KING K-BUS request accounting and single timing path

Date: 2026-10-29. Documents used as the specification: official Hudson/NEC
`DOCUMENTATION/ORIGINAL_JPN/C6272_2.WRI` (HuC6272 part 2, §§3.1.2,
3.2.1–3.2.3, 3.3.4, 3.4.2, 3.5, 3.6.7) and `C6272_1.WRI` (registers).
English markdown translation in the supplied toolkit was used for navigation;
if it differs from the Japanese original, the Japanese source takes precedence.

## Why `legacy` was removed

The aggregate empirical busy-slot fit assigned extra CPU cycles after an
already-posted KING write, and treated BG load as a per-bank average rather
than ordered bus grants. It could not express the input latch draining while
V810 executes other instructions, or subsequent writes stalling on a pending
one. v12–v14 introduced a more physical one-entry posted latch and late
KRAM visibility. Its tank-demo result (20.489 FPS) is closer to the
user-observed real-PC-FX 18–19 FPS than the legacy fit (~33.82 FPS).
That **does not prove accuracy**; it is a structural reason to maintain one
model rather than two competing, divergent code paths. `--kram-timing` and
`PCFX_KRAM_TIMING` no longer select emulation behavior. Earlier reports are
retained as historical measurements.

## Rules established by official documentation

- The HuC6272 K-BUS arbiter chooses the highest-priority pending request,
  then arbitrates again after that request is serviced (§3.2.2).
- During active display: BG > HuC6271/RAINBOW > SCSI DMA > CPU.
- During blanking: REF > ADPCM > HuC6271/RAINBOW > SCSI DMA > CPU.
- There are **five refresh operations in each HBlank** (§3.2.3).
- The two KRAM banks have independent eight-step BG microprograms (§3.6.7).
- A previous unfinished KRAM access makes the CPU encounter -BUSY (§3.1.2).
- DMA writes one KRAM halfword for every two received SCSI bytes (§3.3.4).

## Implementation in `mednafen/pcfx/king.c`

- CPU posted writes keep the v13 `(bank,address,value,retirement)` latch,
  independent of the CPU-visible address auto-increment; four grantable KING
  dots per CPU halfword remain the v12 model assumption.
- CPU KRAM reads now obtain one available model grant after an outstanding
  posted write; the retained extra one-clock return overhead is not given by
  the manual and requires calibration.
- BG microprogram slots continue to reserve the corresponding bank during
  active video, with enabled BG priority checked at each candidate slot.
- RAINBOW requests are recorded on KRAM *word* reads, not once per byte.
  An active request competes with a same-bank CPU grant.
- SCSI DMA creates a K-BUS request only upon a completed two-byte KRAM word.
- ADPCM creates a K-BUS request when `KING_GetADPCMHalfWord` reads the word.
- HBlank reserves five refresh opportunities, paired across the two model
  banks. Same-timestamp collisions select the documented higher-priority
  requester, independently for each bank.
- Source reservations and CPU post-latch deadlines persist in save states;
  loading older states initializes missing reservation arrays as empty.
- This core is common to headless and SDL3, independent of native-dot/
  1024-clock presentation and video filters.

## Important approximations / NOT verified as silicon behavior

1. The official text specifies requester priority and refresh **count**, but
   not the absolute grant phase relative to the V810, the refresh positions
   within HBlank, the service duration, or whether each refresh command pairs
   the banks. **This model places five paired grants at HBlank entry.**
2. One grant is approximated as four V810 clocks (one KING dot). Four grants
   per CPU halfword retain the prior v12 experimental observation, not a
   published HuC6272 value.
3. The RAINBOW decoder can fetch an entire compressed-data chunk in a single
   host rendering callback; its KRAM requests are observed but not yet
   synchronized to the real HuC6271 -REQR handshake. This can queue requests
   too densely compared with silicon.
4. SCSI DMA and ADPCM data accesses still become visible to their consumer
   immediately in the emulator; only CPU KRAM writes have deferred data
   commits. Non-CPU request reservations represent contention, **not** fully
   deferred DMA and decoder data transfers.
5. A late-arriving higher-priority request can reserve a future slot, but the
   CPU's already-booked posted-write completion is not retrospectively
   rescheduled. The emulation core's graphics and SCSI loops also advance
   at slightly different event granularity. This is NOT a cycle-exact K-BUS
   scheduler; a truly ordered event queue must ultimately replace these
   approximations.
6. The 1024-clock picture, half-dot shift and analog-filter models are
   separate from K-BUS arbitration, and remain independently uncalibrated.

## Regression evidence (this build)

- Headless build and unit tests for BG/CPU, word request ordering, RAINBOW /
  SCSI / ADPCM / REF requester classes and per-bank isolation passed.
- From the saved tank workload, guest `_nframe` delta = 103 over 5,027
  1-kHz ticks, or **20.489 FPS**. Physical PC-FX reported by the user:
  **18–19 FPS**. This game alone is insufficient for calibration.
- 1024×480 KING + dual-VDC still-image frame is pixel-identical to v14.
- Windows desktop SDL3 and real-console measurements are not covered by
  the headless/offscreen execution tests.

## Next necessary hardware measurements

Measure the K-BUS -BUSY/wait distribution under simultaneous SCSI DMA,
RAINBOW decode, ADPCM playback and five REF operations at eight offsets
around HBlank. Repeat each bank with and without BG fetches. Compare CPU
read-after-write, DMA-write-followed-by-CPU-read, and RAINBOW compressed
block-boundary behavior. Cross-check any revised model against the
reported double-sprite regression rather than optimizing solely for FPS.
