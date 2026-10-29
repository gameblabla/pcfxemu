# PCFXemu v15 — official HuC6272 K-BUS audit and single timing model

This revision removes the historical measured-average KRAM timing fork. Both
headless and SDL3 use the same per-bank posted CPU write latch and late KRAM
visibility. `--kram-timing` and `PCFX_KRAM_TIMING` are no longer accepted
selectors; old reports mentioning `legacy` are archival, not current behavior.

Following Hudson/NEC HuC6272 §§3.2.2–3.2.3, this revision accounts for actual
KRAM requests from SCSI DMA, RAINBOW word reads, ADPCM word reads, CPU reads,
and five REF operations during each HBlank. Per-bank BG microprogram fetches
have priority during active display. Same-time source conflicts use the
manual's documented priorities.

**This is still not cycle-accurate K-BUS emulation.** Four CPU grant slots per
posted write, a four-V810-clock service quantum, exact refresh placement and
some request arrival timing are unverified. SCSI/ADPCM/RAINBOW memory values
are still consumed immediately by their device even when model reservations
compete for KRAM service. See `docs/KBUS_REQUESTS_V15.md` for the precise
implemented behavior and `docs/HARDWARE_TESTS_REQUIRED.md` for falsifiable
hardware tests. The official documentation is ground truth where it states
behavior; implementation guesses are never presented as facts about silicon.

**Current tests:** unit tests for requester priority and bank isolation pass;
headless RGBA8888, RGB565 and SDL3 offscreen builds pass; guest-reported tank
FPS remains 20.489 in the tested 300-field interval against the user's
reported 18–19 on retail PC-FX. The 1024×480 still-image output matches v14.
The split-save tank test reproduced the screenshot but differed by 18 bytes in
upper guest RAM/stack; the byte-level difference is unresolved and tracked as a
regression item, not falsely marked as a complete deterministic-state pass.
