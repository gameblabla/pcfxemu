# PCFXemu v11: V810 consecutive output timing and KING per-bank slot phase

Date: 2026-10-29. Source baseline: delivered PCFXemu v10. Scope: source-backed timing correction and non-invasive profiling. **No speculative KRAM wait time was added.**

## Authoritative specification

- NEC **V810 Family Architecture User's Manual**, chapter 5, Table 5-11, note 3: parenthesized minimum execution cycles for `ST.B/ST.H/ST.W` and `OUT.B/OUT.H/OUT.W` apply when the **same instruction** is executed consecutively. These minimum cycles presume cache hit, no hazard and no external wait.
- NEC/Hudson **HuC6272 Device Description Part 2**, §§3.2.2–3.2.3: separate priorities during active display and HBlank; five refresh operations per HBlank; request service is priority-based.
- **HuC6272 Part 2**, §3.6.7: one 8-KING-dot access microprogram per KRAM bank, steps 0–7 in bank A and 8–15 in bank B; A/B accesses independently scheduled. Dot-clock timing aligns nominally with four master-clock ticks per 5.37-MHz KING dot.

## Implemented changes

1. The V810 core now tracks the exact preceding store/output opcode width. It applies the instruction table's consecutive surcharge for `OUT.H` after `OUT.H`, but not after `OUT.B` or `OUT.W` (and similarly for the store instructions). Load timing remains grouped as described by the V810 table. Old generic store/output save-state histories are accepted without inventing an unknown prior width.
2. When built with `PROFILE=1`, KING now collects an **8-slot-by-2-bank histogram** for KRAM data-port writes issued in the active 1024-clock video window. Separate counts identify the programmed non-NOP slot, whether its BG priority is enabled, HBlank writes, and writes while the BG microprogram is disabled. Optionally export via `PCFX_KBUS_PHASE_CSV=/tmp/phase.csv`. All this instrumentation is observational; neither CPU timing nor KRAM contents change.
3. Focused static-source invariants and runtime tests live in `tests/validate_v11_timing.py` and `tests/run_v11_phase_test.sh`.

## 300-field original tank measurement (emulator, not silicon)

Using the supplied original tank CD and the same `tank1100.state`, with the v11 profiler:

| Metric | Count |
|---|---:|
| CPU KRAM writes, bank A | 5,676,403 |
| CPU KRAM writes, bank B | 0 |
| Active-display writes, bank A | 4,220,664 |
| HBlank writes, bank A | 1,455,739 |
| Active writes aligned with programmed non-NOP slot | 2,106,733 (49.92%) |
| Active writes aligned with NOP slot | 2,113,931 (50.08%) |
| Bank-A microprogram slots | Non-NOP 0–3, NOP 4–7 |

Per-slot issued writes (A): `529551, 525246, 523551, 528385, 528671, 527429, 529558, 528273`.

These numbers show that the current aggregate *four-busy-slot* fit fails to distinguish two very different CPU request phases. They **do not** demonstrate whether the actual KRAM request was served in its issue slot. The CPU's posted-write wait, unknown K-BUS request-to-service latency, and unknown phase between instruction issue and KING grant prevent such an interpretation. NOP slots offer *potential service windows*, not guaranteed immediate CPU write completion.

## Regression results

- Headless release and V810_PROFILE builds: compiled.
- SDL3 frontend: compiled and stayed up during the offscreen smoke test.
- Dot-width, interlace, KING v4/v5/v9, HuC6230 v4/v6 and V810 state-resume tests: passed.
- 480i photo: v11 1024x480 output identical to v9/v4 reference.
- Tank: v11 300-field screenshot pixel-identical to v10 from same state.
- Game's own `_nframe` / `_zda_timer_count` RAM counters: **170 completed frames / 5,027 timer ticks = 33.817 FPS** over a 300-video-period interval. User reports **18–19 FPS on physical PC-FX**. Discrepancy remains unresolved.

## Important limitations / next work

- The live K-BUS model continues to use a prior experimentally fitted average per-write cost. v11 tracing **does not** replace it with a genuine sub-slot arbiter.
- The microprogram's model slot 0 is referenced to the emulator's active video-window start. **Actual silicon-to-V810 phase alignment is unknown.** Do not promote the phase bins to exact electrical measurements.
- A silicon-accurate scheduler must hold and retire pending CPU writes according to the real posted-write queue, service lower priority requests when higher priorities are absent, and account for refresh/ADPCM/RAINBOW/SCSI requests at their actual times. Merely matching 18–19 FPS is insufficient to validate it.
- Hardware probes required: issue fixed bursts at controlled KING phases with microprogram occupancy shifted among the eight slots, and compare with two-bank interleaving and HBlank. See `HARDWARE_TESTS_REQUIRED.md`, PERF-08.
