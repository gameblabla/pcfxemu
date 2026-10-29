# PCFXemu v8: V810 pipeline + KING K-BUS investigation

**Status:** official-documentation audit and instrumentation, NOT an FPS correction or a cycle-accurate bus model.

## Normative documentation

1. NEC V810 Family Architecture UM (`DOCUMENTATION/CPU/v810-cpu-doc.pdf`, chapter 5 §5.4.1, table 5-11, printed pages 107–109): listed instruction durations assume cache hit, no hazard, no wait. `OUT.H` minimum 1 clock, parenthesized 2-clock timing for consecutive operations. Page 2 describes register/flag hazard interlocks and pipelined execution. It does **not** specify the PC-FX's external KING posted-write buffer topology.
2. NEC/Hudson HuC6272 Device Description (`DOCUMENTATION/ORIGINAL_JPN/C6272_2.WRI`, §3.1.2, §3.2.2–3.2.3 and §3.6.7): the CPU's KRAM write starts immediately if prior access is complete; otherwise `-BUSY` holds the CPU while the request waits. During display the arbitration order is BG > HuC6271 > SCSI > CPU. During HBlank: refresh > ADPCM > HuC6271 > SCSI > CPU. Five refreshes per HBlank. BG KRAM microprogram has 8 independently scheduled slots per bank (A entries 0..7, B entries 8..15), and bank A and B can be accessed simultaneously.
3. The official manuals do NOT establish the exact V810 write-buffer size, K-BUS arbitration microcycle phase, slot service latency or the time-to-fence for a posted CPU write. These cannot be filled in with arbitrary constants.

## What v7 actually does

- V810 `OUT.H` base timing and issue history (`v810_oploop.inc`); flag-dependency interlock; 1 KiB I-cache; 2 KiB DRAM page timing.
- `KING_KRAMWriteWaitCycles()` charges an approximate drain/posted-write latch (12 V810 cycles remaining after call time), followed by `KING_KRAMWriteContentionCycles()` applying an average measured write-time curve indexed by *total active microprogram fetch slots* and whether a wide-color mode is enabled. This curve is deliberately a throughput fit, not slot arbitration.
- An I/O register write incurs an extra 2 clocks in the PC-FX port handler. Exact overlap between that overhead, pipeline issue and external KING wait is not modeled.
- v7 does not save the `v810_flagset` immediately-previous-instruction flag history, even though the pipeline model uses it. v8 now saves it; old states default to 0.

## Reproduction, v8 (measured emulated counters)

From the original v7 `tank1100.state`, run the original user-supplied `mytest_cd.cue` for 300 emulated video periods, in a build with `PROFILE=1`:

```
make -f Makefile.headless PROFILE=1 -j4
./pcfx-headless --bios-dir /path/to/pcfx.rom \
  --state-in /path/to/tank1100.state --frames 300 /path/to/mytest_cd.cue \
  > /tmp/tank_v8_profile.txt 2>&1
```

The profile for this input reports:

| Emulator measurement | Per video period |
|---|---:|
| CPU instructions | 90,206 |
| CPU attributed cycles | 357,621 |
| CPU KRAM 16-bit writes | 18,761.92 |
| Modeled posted-write WAIT | 37,925.66 cycles |
| Modeled BG contention overhead | 105,609.11 cycles |
| Combined modeled external KRAM stall contributions | 143,534.77 cycles (~40.1% of video-period budget) |

The original `OUT.H` attribution already includes most of these costs; **do not add the 143,535 cycles to the 357,621 total**.

All 5,628,577 writes in this sample target bank **A**. All four selected BG microprogram fetch slots also target bank **A** (0 scheduled for B). The current timing fit sees four wide slots for every write; the trace's CPU issue clock phase modulo 8 is fairly evenly distributed. This modulo is **not** a calibrated physical K-BUS phase.

This is much more specific than saying "the CPU is too fast". The key unproven question is how the four occupied A-bank fetch slots are arranged within the eight-slot cycle, and exactly when CPU writes and pending buffered stores can claim the remaining slots.

## Why 18–19 FPS cannot be derived directly

- The displayed 33–34 counter is a software-derived value; the separately measured 35–39 *visible changes/s* are not necessarily completed engine frames. A CPU stall profile requires completed engine-frame instrumentation or a physical same-binary test.
- A model calibrated to some real KRAM throughput benchmarks already charges 105,609 additional cycles per video period in this demo. Simply multiplying the waits would discard those measurements.
- No instruction in the official V810 architecture user manual provides the missing PC-FX external bus service schedule. The KING manual specifies priorities and requests, not all electrical arbitration latencies.
- Neither the V810 manual nor K-BUS manual proves how many V810 cycles an older store may overlap with later ALU or cache fetch instructions, or whether a V810 write buffer can retire the next store on an immediately preceding KING burst.

## What was changed in v8

- Added a `PROFILE=1` KING bus accounting report (write count and bank, drain wait cycles and histogram, modeled BG contention, active vs blank, A/B fetch slot count, CPU phase bins, and reads). It does not affect emulated timing or the screenshot.
- Made `v810_flagset` save-state persistent; absent in old-state files -> 0 as a conservative compatibility fallback.
- No changes to default KRAM clock delays, CPU instruction durations, analog video, video fields, RAINBOW or SCSI.

## Next physical-console experiments (see HARDWARE_TESTS_REQUIRED.md)

1. A vs B CPU KRAM write bursts, with 0/1/2/4 BG fetch slots **on each bank separately**, and with clustered vs interleaved slots.
2. Sweep a timestamped CPU KRAM write over the 8-slot microprogram sequence and map `-BUSY` duration. Repeat during display and HBlank; characterize the five refreshes.
3. Isolate V810 write buffer: `OUT.H; OUT.H`, `OUT.H; ADD; OUT.H`, `OUT.H; IN.H` fence, and RAM store mixed with KRAM write; time each with the free-running timer and code in I-cache.
4. Run the *same* `3Dtankdemo.zip` on retail PC-FX and use a completed-frame marker (rather than merely visible scene change) for true 18–19 FPS comparison.

Only after reconciling the queue and slot schedule with these tests should an arbiter replace the v7/v8 empirical throughput curve.
