# Why `eris_cd_read_dma()` (CD→RAM KRAM-bounce) was slow — root cause, fix, numbers

**Status: root-caused and fixed in the working tree (libpcfx + one bench example;
NOTHING COMMITTED YET — see "What is changed on disk" below).** The prior
session's observation ("measurably slower per read than expected for small,
scattered reads, root cause not isolated" — `docs/cd-loading-survey.md`) is
resolved: the chunked DMA-bounce path had **no throughput problem at all**. It
was losing two independent **per-chunk-boundary race conditions**, each of which
turned a healthy read into the multi-second retry/bus-reset mill (or a permanent
wedge). With both fixed, `eris_cd_read_dma()` is now the **fastest CD→RAM path
in every measured shape** — including the many-small-reads WAD-lump shape it was
reported slow on — while remaining byte-exact against the CPU-PIO path.

## How it was measured (the missing controlled benchmark)

The original "slower" observation came from ad-hoc doom level-load runs. This
session built a controlled A/B bench ROM, **`libpcfx/examples/031_cd_ram_read_bench`**
(cloned from the 026/027 probe scaffolding):

- Same LBA sequences and same SCSI command counts per A/B pair; each condition
  preceded by an untimed positioning read so both paths start from the same
  head position.
- Timing via a VBLANK-IRQ 32-bit field counter. **The 16-bit ITU timer cannot
  time `eris_cd_*` calls at all**: libpcfx's own `cd_timer_guard_begin()`
  pauses the interval timer around every CD command, freezing exactly the
  window under measurement. (Worth remembering for any future CD bench.)
- Data integrity: every RAM-destination condition u32-byte-sums what it read;
  PIO and DMA read identical data, so checksums must match exactly.
- Results land on screen AND in a magic-tagged RAM block (`0xBE9C0031`),
  parsed from `pcfx-headless --dump ram` — no screenshot OCR.
- Run: `make cd` in the example (V810GCC=/opt/v810-gcc), then
  `./pcfx-headless --bios-dir .../pcfxbios.bin --frames 10000 --commands run.cmd
  --dump ram out.ram cd_ram_read_bench_cd.cue` (stock build, PIO erratum
  default ON; ~16 s wall).

## Root cause 1: DRQ latch wedge on re-arm (fix: `eris_scsi_pause_dma()`)

pcfxemu's KING pseudo/real-DMA pump (`king.c`, `KING_Update`) latches a byte
into its `DRQ`/`data_cache` hold register whenever the drive raises REQ while
`DMAStatus` bit 0 is clear — exactly the window between one count-bounded chunk
retiring and the driver arming the next. **Only a reg-2 DMA-mode-bit 1→0
transition clears that latch** (`SCSI_Reg2_Write`). The original
`eris_cd_read_dma()` deliberately skipped `eris_scsi_finish_dma()` between
chunks (its trailing `eat_data_in` would consume the next chunk's in-flight
bytes) — but that also skipped the reg-2 toggle. So a bare `begin_dma` re-arm
(reg2 stays 2→2, no transition) left `DRQ` latched, and the re-armed pump could
never take another byte: neither pump branch can fire with `DRQ` set and the
retire path idle. Permanent wedge, hit as soon as the sector-arrival phase
drifted onto the copy-out window (observed: 14 clean chunks, then silence).

**This is what real retail titles do differently, verified empirically this
session**: running Makeruna! Makendou Z under the instrumented `CD_XFER_DEBUG`
build shows its driver performs the reg-2 1→0 toggle before every one of its
re-arms (837 toggles vs 804 count-bounded arms in one boot trace) — and the
trace shows Makeruna *also* takes the DRQ gap-latch between chunks and survives
precisely because the toggle clears it. The fix therefore also makes libpcfx's
chunk re-arm shape match shipped commercial software, which matters for the
real-hardware prognosis.

Fix: new **`eris_scsi_pause_dma()`** (`libpcfx/src/scsi.S`) = `finish_dma`'s
reg2←0/reg3←0 writes *without* the data-consuming drain, called after every
count-bounded chunk completes (also leaves the failure path with a disarmed
pump instead of leaking an armed DMA into the retry logic).

## Root cause 2: `eris_scsi_check_dma()` is not idempotent after completion

KING reg 0x0B's low-halfword **read is an IRQ acknowledge** in pcfxemu
(`king.c` `KING_Read16 case 0x0B`): after a transfer completes it returns 1
exactly once (then clears `DMAInterrupt`). `eris_scsi_check_dma()` treats that
1 as "DMA still in progress". The old chunk loop was:

```c
while (spins-- && eris_scsi_check_dma()) { }
if (eris_scsi_check_dma())   /* redundant re-check — THE BUG */
        ok = 0;
```

If the chunk retires in the ~20-cycle window between one poll iteration's reg
0x0B read and its reg 0x0A (count) read, the loop exits "done" with the
completion flag still pending — and the re-check consumes it, reads "busy", and
condemns a perfectly completed chunk (`ok = 0` → whole-attempt failure → drain
+ bus reset + retry, ~1.15 s each, ~24 attempts, then a failed call). Captured
directly in the instrumented trace:

```
DMA retire (count reached 0)
t=116784 reg0x0A(count) read -> 0      ← poll iteration exits "done"
t=116815 reg0x0B read -> 1             ← redundant re-check eats the pending IRQ flag → ok=0
READ(10) lba=9 sectors=32              ← healthy transfer condemned; attempt retried
```

Odds ≈ 1-in-5 per chunk boundary, so a 32-chunk read essentially always died;
single-chunk transfers (which use phase polling, not `check_dma`) never did —
which is why `eris_cd_read_kram()` and whole-transfer-in-one-chunk calls were
"fine" and the blame initially looked like a small-reads throughput problem.

Fix (`libpcfx/src/cd.c`): derive success from **how the poll loop exits**, and
never call `check_dma` again after it has returned "done":

```c
spins = CD_SPIN_LONG;
while (eris_scsi_check_dma()) {
        if (!--spins) { ok = 0; break; }
}
eris_scsi_pause_dma();
```

(On real hardware — where reg 0x0B bit 0 is presumably a true busy bit — this
form is behaviorally identical to the old one minus the redundant read, so the
fix is safe regardless of which semantics real silicon has.)

## Results (pcfx-headless, stock settings, fields @ ~16.7 ms; 2 reps for big, 32/16 reads for sml/sm4/sct)

| condition | shape | before fixes | after fixes | vs PIO |
|---|---|---|---|---|
| bigKRM  | 64 KB → KRAM, pure DMA (floor) | 43 | 43 | — |
| bigPIO  | 64 KB → RAM, CPU-PIO | 59 | 59 | baseline |
| bigDMA2k | 64 KB → RAM, 2 KB scratch (32 chunks) | **never completed** (err, ~1652 fld/call) | **43** | **−27 %** |
| bigDMA8k | 64 KB → RAM, 8 KB scratch | 446 (worked, crawling) | **44** | −25 % |
| bigDMA32 | 64 KB → RAM, 32 KB scratch (1 chunk) | 45 | 46 | −22 % |
| smlPIO  | 32 × 2 KB abutting | 38 | 38 | baseline |
| smlDMA  | 32 × 2 KB abutting | 23 | **23** | **−40 %** |
| sm4PIO  | 16 × 8 KB abutting | 53 | 53 | baseline |
| sm4DMA8k | 16 × 8 KB, 8 KB scratch | 38 | 39 | −26 % |
| sm4DMA2k | 16 × 8 KB, 2 KB scratch (4 chunks/read) | (blocked by bigDMA2k) | **34** | **−36 %** |
| sctPIO  | 16 × 2 KB scattered | — | 285 | baseline |
| sctDMA  | 16 × 2 KB scattered | — | **278** | seek-bound, parity |

Every DMA checksum is byte-identical to its PIO twin (`007f9074`, `007f8778`,
`00fead21`, `003fa197`); zero errors, zero retries. Chunked DMA now sits on the
pure CD→KRAM DMA floor (bigDMA2k 43 = bigKRM 43): the KRAM copy-out is fully
hidden behind the emulated drive's 307,200 B/s delivery. PIO loses ~27 % on
bulk because its per-byte CPU handshake (~75–90 cycles/byte) is *slower* than
the drive, and ~40 % on single-sector commands where its fixed overhead weighs
more. Scattered reads are seek-model-bound and equal, as expected. Notably the
scratch size barely matters any more (2 KB ≈ 8 KB ≈ 32 KB), so a small
KRAM scratch window is enough.

## What is changed on disk (ALL UNCOMMITTED)

- `libpcfx/src/scsi.S` — new `eris_scsi_pause_dma` (+ `.global`).
- `libpcfx/include/eris/scsi.h` — its declaration.
- `libpcfx/src/cd.c` — `eris_cd_read_dma()`: per-chunk `pause_dma` + the
  loop-exit-derived `ok` (no redundant `check_dma`).
- `libpcfx/include/eris/cd.h` — KNOWN ISSUE paragraph replaced with the fix
  summary.
- `libpcfx/examples/031_cd_ram_read_bench/` — new bench/probe ROM (source,
  Makefile, cdlink.txt, run.cmd, generated testdata.bin + build artifacts).
- `pcfxemu/mednafen/pcfx/king.c` — gated `CDXFERDBG` event prints only (DMA
  retire, DRQ gap-latch, pump-stalled-on-latched-DRQ, reg-2 1→0 disarm);
  compiled out without `CD_XFER_DEBUG=1`. No behavioral change.
- `pcfxemu/mednafen/cdrom/scsicd.c` — gated `CDXFERDBG` print on the
  FIFO-full sector-delay path. No behavioral change.
- `pcfxemu/docs/cd-loading-survey.md` — known-issue paragraph updated.
- Both `pcfx-headless` (stock) and `pcfx-headless-cddbg` (CD_XFER_DEBUG=1)
  rebuilt from this tree; `/opt/v810-gcc/lib/libpcfx.a` reinstalled via
  `make install` (matches `libpcfx/libpcfx.a`).
- doom-pcfx: **untouched** (it doesn't call `eris_cd_read_dma` yet).

## How to proceed

1. **Commit the libpcfx fix** (scsi.S + scsi.h + cd.c + cd.h) and the bench
   example, and the pcfxemu debug-print/docs changes, as separate commits per
   repo (the usual unsigned WIP style).
2. **Real-hardware validation before any doom adoption.** The two fixes are
   emulator-verified; on real silicon the reg-2 toggle matches what Makeruna
   demonstrably ships, so it should be safe — but burn
   `031_cd_ram_read_bench`'s cue and check: all conditions `err=0`, checksums
   equal between PIO/DMA rows of the same shape, and the DMA rows at least as
   fast as PIO. The ROM needs no emulator-side anything; results are on
   screen. (Row layout: `cond fld ok err cks` where cks is checksum mod 10000.)
3. **Then wire doom-pcfx's WAD/map-pack CD→RAM readers**
   (`platform/pcfx_wad.c`, `pcfx_mappack.c`, currently on timer-guarded
   `eris_cd_read()` PIO) to `eris_cd_read_dma()` with a small page-0 KRAM
   scratch (the waifu-style free scratch at 0x18000-class addresses; even 2 KB
   suffices per the numbers above — but keep the scratch off any KRAM the
   renderer/text/HUD uses, and remember the caller must route
   `king_set_page_setting`/`king_set_kram_pages` so the SCSI page equals the
   CPU-readback page). Keep PIO behind a fallback define like the existing
   `PCFX_CD_KRAM_USE_PIO` pattern. Expected win: ~25–40 % on level-load CD
   transfer time (the map-pack single-big-read shape is the −27 % bulk case),
   plus immunity to the real-hw CPU-PIO timer erratum without the timer guard
   (the guard can stay; it's harmless for DMA).
4. Optional emulator follow-up (not needed for the fix): decide whether KING
   reg 0x0B low-read should return the DMA busy bit rather than acting as a
   destructive IRQ acknowledge. Real-hardware truth is unknown; the current
   semantics date to upstream mednafen and other titles (e.g. the Anime Freak
   FX hack nearby) may depend on adjacent behavior — leave as-is unless a
   hardware probe (031 extended with a reg 0x0B read-back test) says otherwise.
5. Housekeeping: `031`'s `testdata.bin` is generated (1 MB LCG pattern,
   seed 12345, 512-sector); regenerate with the one-liner in the session log or
   any fixed pattern — content is irrelevant to timing, only to checksums.

## Gotchas worth keeping (for future CD work)

- The ITU-timer guard in cd.c silently freezes ITU-based timing across every
  CD command — use the VBLANK field counter for CD benchmarks.
- `eris_scsi_check_dma()` must be treated as **destructive/one-shot** after a
  completed transfer under pcfxemu; never call it "one more time to be sure".
- Between count-bounded chunks of one transfer: `pause_dma` yes,
  `finish_dma` no (it eats the next chunk's bytes), bare re-arm never.
- pcfxemu arms print (`CD_XFER_DEBUG=1`) only when reg 2 bit 1 is set at the
  reg 7 write — a "missing arm" in a trace can mean the arm was rejected, not
  that the driver didn't try.
