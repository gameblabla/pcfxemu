# PCFXemu v13 — deferred KING KRAM write visibility

Based on v12, with experimental `PCFX_KRAM_TIMING=slot` preserved as an opt-in. The default remains `legacy`.

- A pending KING R0E write captures physical page/address/data and becomes KRAM-visible at the modeled drain deadline, rather than at acceptance.
- The pending retirement deadline is saved independently of the CPU WAIT deadline, and survives per-field timestamp rebasing and save states.
- Fixes bank-selection in the scheduler and legacy bank contention accounting: uses R0D bit 31 (and 1-Mbit page forcing), not bit 17.
- Adds pending-data and bit-17/bank-isolation unit tests.
- Does not change the video mixer, SDL3 rendering, 480i behavior, or the estimated slot-grant duration.

Verification: headless and SDL3/offscreen build; seven focused unit scripts; 1024×480 still unchanged; tank demo ~20.49 guest FPS in slot mode; save-state split/continuous matches. See `docs/KRAM_DEFERRED_COMMIT_V13.md`, `docs/SPEC_CONFORMANCE_V13.md` and `docs/HARDWARE_TESTS_REQUIRED.md`.

Not a full HuC6272 bus scheduler; grant phase, refresh, DMA/ADPCM arbitration and V810 pipeline remain incompletely modeled. Physical hardware validation is required before enabling slot mode by default.
