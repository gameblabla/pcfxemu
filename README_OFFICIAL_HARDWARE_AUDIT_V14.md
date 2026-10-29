# v14 — KING deferred-write scheduler enabled by default

This revision selects v12/v13's bank-local four-grant scheduler and deferred KRAM write visibility by default in both SDL3 and headless. The historical average-contention model remains an explicit compatibility fallback.

- Default: `--kram-timing slot` (no option required).
- Compatibility: `--kram-timing legacy`, or `PCFX_KRAM_TIMING=legacy`.
- KRAM timing is selected once when the emulator starts. Switch between models only across separate runs.
- Reference: `docs/KRAM_TIMING_DEFAULT_V14.md` and `docs/V810_HUC6272_AUDIT_V14.md`.
- Physical-console timing requirements: `docs/HARDWARE_TESTS_REQUIRED.md`.

The default runs the supplied tank at 20.489 game FPS, versus 33.817 using the legacy model and the user-reported 18–19 on console. No cache miss penalties or refresh/arbiter service phases were tuned to target the FPS; those remain unverified. The 480i mixed-rate rendering and save-state regression remain unchanged.
