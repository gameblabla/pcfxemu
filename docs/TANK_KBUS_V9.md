# PCFXemu v9: tank demo and the CPU-to-KRAM timing path

**Baseline:** user reports the supplied 3Dtankdemo runs **18–19 FPS on physical PC-FX**. We accept this as the working hardware reference (user-reported, not independently timed here). The game's own red counter reads **34** in the v9 capture after 300 video periods from `tank1100.state` (and 33–34 in v8 captures). This is a simulation-speed discrepancy, not evidence that the host PC is running too quickly.

## Official documentation used as normative specification

- HuC6272 Device Description (`DOCUMENTATION/ORIGINAL_JPN/C6272_2.WRI`, English translation `DOCUMENTATION/ENGLISH_TRANSLATION/C6272_2/C6272_2.md`) §3.1.2: CPU KRAM data access uses -BUSY while an access remains outstanding. Address registers provide CPU access setup; a write is held if a prior KRAM access is incomplete.
- §3.2.2: display arbitration is BG > HuC6271 > SCSI > CPU; HBlank is REF > ADPCM > HuC6271 > SCSI > CPU. §3.2.3: five refreshes every HBlank.
- §3.6.7: 8 microprogram requests per KRAM bank, banks A/B serviced independently and simultaneously. MPROG slots 0–7 target bank A, 8–15 target B.
- NEC V810 Family Architecture UM, chapter 5 §5.4.1: instruction timings are minimums with cache hit, no hazards and no external waits. The instruction table does not specify the PC-FX peripheral write-buffer service clock or KING's K-BUS phase.

## Corrections made in v9

1. **Per-bank BG contention.** For a CPU KRAM write, count BG microprogram requests on only the target bank (KRAMWA bit 17); do not count requests for the other bank. Classify wide-color requests for that bank only. The preexisting calibrated throughput curve is still used for the busy-slot class because an exact arbitration latency is undocumented.
2. **HBlank BG contention.** No BG fetch-slot surcharge in horizontal blanking. The preexisting 0-slot baseline remains charged as an empirical write-port overhead; REF, ADPCM, RAINBOW and SCSI exact service phases are not synthesized.
3. **KRAM data-read ordering.** Wait for an outstanding KRAM data write and the preexisting nominal 5-cycle read service *before* performing the read. A read of the KRAM write-address register no longer silently resets the pending KRAM write busy-until timestamp (the official manual does not state that reading the address register is a drain/fence).

Each correction is covered by a focused source-level unit test. Neither an unverified V810 pipeline queue depth nor an artificial FPS multiplier has been inserted.

## Controlled 300-field comparison

All data are **emulator-derived**, with the same physical ROM, BIOS and saved state (`tank1100.state`). Fields and CPU cycles are those of the model, not a measurement of the physical system.

| Metric | v8 | v9 |
|---|---:|---:|
| Game's red FPS counter | around 33–34 | **34** at +300 |
| KRAM CPU writes / field | 18,761.9 | 18,893.7 |
| Modeled busy-latch waits / field | 37,925.7 cycles | 47,618.6 cycles |
| Modeled extra write/contention / field | 105,609.1 cycles | 94,600.7 cycles |
| Both modeled costs / field | 143,534.8 cycles | 142,219.3 cycles |
| CPU cycles attributed / field | 357,621 | 357,622 |
| Writes to KRAM bank A / bank B | 100% / 0% | 100% / 0% |

The change mostly shifts the modeled stall attribution (BG slots are no longer charged during HBlank); it **does not** solve the ~1.7–1.9× FPS discrepancy. These cost counters are separate categories *inside* the CPU cycle attribution; do not add them to the total.

v9 intentionally changes some gameplay pixels and timing relative to v8. The same 480i still-image homebrew produces a byte-identical 1024×480 PNG compared to the earlier reference (`pcfx_v4_final_1024x480.png`).

## What remains unknown and should not be hardcoded

- Actual K-BUS sub-slot phase, minimum and maximum CPU word service time, posted-write queue size and the CPU's ability to execute subsequent instructions before that queue retires. See `docs/HARDWARE_TESTS_REQUIRED.md` PERF-02/03/04 and KING-01.
- KRAM refresh's five exact positions during HBlank, RAINBOW/SCSI/ADPCM contention and whether multiple outstanding requests span the period boundary.
- The real CPU's load-use / write-after-read pipeline stalls under external BUSY and its interaction with 1 KiB I-cache refill and DRAM page miss latency.
- The game-defined FPS counter's exact averaging window and the precise completed-frame marker. The red counter is more useful than blindly counting changed display fields, but must not be equated with the number of distinct screen hashes.

## Reproduction

```sh
# From the v9 tree, with your own BIOS and demo, neither included in the package:
./pcfx-headless --bios-dir /path/to/pcfx.rom --state-in /path/to/tank1100.state \
    --frames 300 --screenshot /tmp/v9_tank.png /path/to/mytest_cd.cue

# Profile separately (it does not change emulated timing):
make -f Makefile.headless PROFILE=1 BUILD_DIR=build/headless-profile-RGBA8888 \
    PRGNAME=pcfx-headless-profile -j4
./pcfx-headless-profile --bios-dir /path/to/pcfx.rom --state-in /path/to/tank1100.state \
    --frames 300 /path/to/mytest_cd.cue 2> /tmp/v9_profile.txt

# Focused tests:
tests/run_king_v9_bank_contention_test.sh
tests/run_v6_regression.sh
```

**Status:** per-bank and HBlank behavior and KRAM read-request ordering improved in source; exact external bus cycle calibration and 18–19 FPS parity are pending.
