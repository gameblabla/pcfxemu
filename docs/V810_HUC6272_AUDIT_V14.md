# V14 V810 / HuC6272 timing conformance audit

Authoritative source for specified behavior: NEC V810 Family Architecture User's Manual and Hudson/NEC HuC6272 Part 1 and Part 2 in the supplied Japanese-development toolkit. The English text is a translation and should not supersede ambiguous Japanese wording.

| Feature / reference | Existing implementation | Finding / required follow-up |
|---|---|---|
| V810 cache configuration, manual chapter 7 | `v810_cpu.c`: 128 direct-mapped 8-byte entries, two 4-byte subblock validity bits, tag lookup | Structurally matches the specified 1 KiB format; miss/halfword-bus refill timing is still a heuristic, not established by the manual's instruction-cycle table. |
| V810 CHCW ICE/ICC/ICD/ICR, §§2.2.7, 7 | Cache enable, partial invalidation, dump and restore handlers present | Partial invalidation clamps to entry 127. The exact instruction-fetch activation edge after LDSR and interrupt hold while cache maintenance runs are not cycle-modeled; avoid changing until independent timing tests. |
| V810 normal instruction cycles, table 5-11 | Existing v11 width-sensitive consecutive `OUT`/`ST` surcharge | Minimum timings are documented only with cache hit, no conflict, no external WAIT. CPU posted writes to peripherals outside KING R0E are still approximated (see `shell/emu/core.c` FIXME). |
| V810 status/flag hazard | Existing +2-cycle flag-use stall | Sourced from hardware-observer experiments, not from the minimum-cycle table; treat it as an empirical model requiring independent probes. |
| KING independent banks, HuC6272 §3.6.7 | `king.c` slot model uses distinct 8-step bank schedules and bit-31 page routing | Structurally implemented. The arbitration origin phase relative to CPU issue remains unmeasured. |
| KING request priority, HuC6272 §3.2.2 | BG priority and free/NOP opportunities estimated during active picture | Incomplete: HBlank five refresh grants and dynamic SCSI/RAINBOW/ADPCM interactions are not fully scheduled. Cannot mark cycle exact. |
| KING CPU R0E write latch | Pending `(page,address,value)` and separate deadline; CPU WAIT if overwritten before drain | Reported one-entry hardware behavior is approximated. Exact service cycles and peripheral visibility are unverified. |
| Interlace/mixed-clock video | Existing 1024-clock TETSU composite and 480i fields preserved | No new video-source changes in v14. Both default/explicit slot still-image captures match byte for byte. |

## New code in this revision

1. Default `KING_KRAMSlotTimingEnabled()` to slot; opt out with `PCFX_KRAM_TIMING=legacy`.
2. Explicit, validated `--kram-timing slot|legacy` command-line setting in SDL3 and headless.
3. Dedicated default-selection tests, and an explicit legacy-mode environment in the earlier v9 per-bank empirical-model regression.
4. Build, guest-FPS, still-image and save-state tests as described in `KRAM_TIMING_DEFAULT_V14.md`.

No V810 cache constants or missing K-BUS grant phases were fabricated in this revision. Accuracy measurements remain open in `HARDWARE_TESTS_REQUIRED.md`.
