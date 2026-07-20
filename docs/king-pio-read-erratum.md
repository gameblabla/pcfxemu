# KING CPU-PIO SCSI read failure (doom-pcfx boot hang)

**Status: modelled by default (hardware-accuracy).** Reproduces the real PC-FX
behaviour that hangs the *current* `doom-pcfx` at boot while leaving CPU-PIO
homebrew that reads correctly (e.g. `waifu` / *Shattered Decks*) working. Set
`PCFX_KING_PIO_ERRATUM=0` to disable it (all reads succeed; doom boots to E1M1) —
the pre-accuracy behaviour, and the mode to use once a doom/libpcfx workaround
exists.

This is a **sibling** of `king-dma-erratum.md`, for a different transfer path.
That erratum models the KING **real-DMA-to-KRAM** engine; this one models the
**CPU-driven PIO** DATA-IN path (libpcfx `eris_scsi_data_in` / the chunk loop in
`eris_cd_read_kram_pio`: the CPU reads the SCSI data register per byte and
toggles ACK itself).

## What the model must get right (two homebrews, opposite outcomes)

| program | how it reads bulk CD data | interval timer at read time | real hardware |
|---|---|---|---|
| **doom-pcfx** | one ~200 KB / 100-sector CPU-PIO read (ADPCM SFX bank) | level-9 IRQ **on**, period 1432 (~1 ms) | **fails** → `pcfx_fatal_blink` |
| **waifu** (*Shattered Decks*) | count-0 phase-driven KING **DMA** (auto-ACK); only small metadata via PIO | timer **off** | **loads fine** |

Both toggle ACK in DATA IN (waifu's `eris_scsi_finish_dma` → `eris_scsi_eat_data_in`
drains leftover bytes by asserting ACK), so **"CPU asserted ACK in DATA IN" cannot
discriminate them** — an earlier model latched on that and wrongly killed waifu.

The true, measured discriminator is **duration under a fast timer IRQ**. On real
hardware a ~1 ms level-9 timer IRQ (doom's `pcfx_time_init`, started *before* the
first CD read) fires inside the tight CPU PIO DATA-IN loop and corrupts a REQ/ACK
handshake. A **short** transfer (a command reply, REQUEST SENSE, TOC — a single
DATA-IN reply) finishes within one IRQ gap and survives; a **large** multi-sector
read spans hundreds of IRQ periods and is corrupted. waifu reads its bulk via the
KING DMA engine (hardware-paced, immune) and its few PIO reads with the timer off,
so nothing of it is ever flagged (probes 027/028/029; the emulator otherwise
delivers whole sectors atomically and never sees the corruption).

## The symptom being modelled

The reference is the real-hardware capture `DoomPCFX_VID_20260719_142731107.mp4`
(the disc image built 17 minutes before it, `DoomPCFX/doom_pcfx.bin` of
2026-07-19 14:10, is the byte-exact binary it ran). Frame analysis of the video:

- BIOS hands off, screen goes black for ~0.7 s (the SFX-bank load window — its
  ~100 chunked PIO reads are failing *fast* here, invisibly; that build ignores
  the return value).
- The empty (grey) boot bar appears and sits empty ~0.25 s.
- The bar takes **3 quick partial fill ticks ~30 ms apart** (≈10 % → 20 % → 23 %):
  `W_Init`'s first reads, each one an entire failed 8-attempt libpcfx retry loop
  completing in ~30 ms.
- ~30–60 ms after the third tick the bar jumps to full: that is already
  `pcfx_fatal_blink()` — a validation `I_Error` fired on the garbage the failed
  reads returned. The bar then oscillates full ↔ empty at ~1.75 s per phase
  (`fatal_blink_delay`'s 0x300000-iteration busy-wait) forever.

The user-visible summary: *empty bar → fills quickly → a couple of seconds
"full" (the blink's first bright phase) → visibly blinking forever.* The load
never progresses slowly and never stalls — every failure is **fast**.

Probe 027 (real hardware) pins the mechanism class: a failed CPU-PIO read dies
so fast that all 8 driver attempts fit inside a single vblank field (`fld=0`);
once one attempt fails, **every** remaining attempt fails too (`rty == fails*7`
— per-attempt recovery, REQUEST SENSE and SEEK repositioning never help); the
only thing that restores the bus is a **full SCSI bus reset** between reads.
That is a poisoned/wedged bus, not a per-command error status.

## Implementation (`mednafen/cdrom/scsicd.c` + `mednafen/pcfx/king.c`)

The discriminator has to be the **transfer method** (CPU-PIO vs KING real-DMA),
not just "timer hot" — DMA is hardware-paced and immune on real silicon, and
retail titles do DMA reads under hot timers routinely (see the regression list
below). But the transfer method genuinely isn't known before the first sector
of a command is delivered: `dma_receive_active` is FALSE for every read at that
point, including phase-driven DMA reads, because the driver only arms KING DMA
*after* observing `PHASE_DATA_IN` (`eris_cd_read_kram`'s
`eris_scsi_begin_dma` call happens post-phase-change). A check placed before
`PHASE_DATA_IN` begins can therefore never exclude a DMA transfer — it would
degrade the model from "CPU-PIO under a hot timer" to "any read under a hot
timer," which is wrong and untestable against the DMA-based doom fix this model
exists to eventually validate.

So the command's first sector always delivers, and `PHASE_DATA_IN` begins
normally for every read. Three hooks split the work:

- **Latch the transfer method** — `KING_Read16` (`king.c`), `AR == 0` / `case
  0x00`, the SCSI-data-register read. If the CPU reads it directly while
  `dma_receive_active` is FALSE and the phase is DATA_IN (`IO=1, CD=0, MSG=0`),
  set `king->PIOReadSeen`. A KING real-DMA pump drains the FIFO through its
  own hardware handshake and never goes through this CPU-facing register, so
  a DMA transfer can never set the latch, while any CPU-PIO transfer sets it
  on its very first sector.
- **Poison the bus at the next sector boundary** — `RunCDRead()` (`scsicd.c`),
  right before it would queue the *next* sector of an already-under-way
  transfer (`CurrentPhase == PHASE_DATA_IN`, i.e. not the command's first
  sector):

  ```c
  if(CurrentPhase == PHASE_DATA_IN && KING_PIOTransferShouldFail())
  {
  	KING_PIOWedgeBus();        /* poisoned until RST */
  	PIOWedgeDropToBusFree();   /* silent disconnect  */
  }
  else if(TrayOpen) { ... }
  ```

  `KING_PIOTransferShouldFail()` is true when `king_pio_erratum_enabled()`
  (env `PCFX_KING_PIO_ERRATUM`, default ON), `king->PIOReadSeen`, and
  `king_pio_timer_irq_hot()` (interval timer **and** its IRQ enabled, control
  bits `0x3`, effective period below ~4 ms — doom is ~1 ms) are all true.
  `king->PIOReadSeen` is reset per command (`KING_PIOReadResetLatch()`, called
  from `DoREADBase` when a new `SectorCount` is programmed) so a driver retry
  is judged on its own transfer method, not the previous attempt's.

  The failure is a **silent disconnect** (`PIOWedgeDropToBusFree()`: flush the
  FIFO, cancel the read, `ChangePhase(PHASE_BUS_FREE)` — the same cleanup as
  the MESSAGE OUT/ABORT path), **not** `CommandCCError`. CHECK CONDITION is
  exactly wrong here: it invites the driver's REQUEST SENSE + SEEK-reposition
  retry mill, which under the emulated seek model costs ~seconds per failed
  read — while the measured hardware failure completes its whole 8-attempt
  loop inside one field. On BUS FREE every libpcfx handshake wait bails
  immediately (`scsi.S` checks BSY-drop first in every loop), so the driver
  fails as fast as silicon does.
- **Keep failing until RST** — `king->PIOBusWedged` is set alongside the
  drop. While it is set, the command dispatch in `SCSICD_Run()` accepts each
  new CDB and then drops it with the same silent disconnect — READ retries,
  REQUEST SENSE, SEEK, everything (probe 027: per-attempt recovery never
  recovers). The KING RST write (`KING_Write16`, the site that resets the
  SCSI registers) clears the wedge — a **full SCSI bus reset is the one
  recovery hardware responds to**, and it is what the fixed libpcfx retry
  loop (`e41278e`) performs between attempts. `KING_Reset()`/power-on also
  clear it; it is in save-states as `PIOBusWedged`.

This boundary is safe for the same reason the old mid-byte abort wasn't (see
History below): at a sector boundary the FIFO is drained and REQ is
deasserted, so the driver is between sectors in its wait-for-REQ/phase-poll
loop — not mid-handshake inside `eris_scsi_data_in` between a data-byte read
and its ACK. There is no in-flight CPU byte-handshake state to desync.

Net effect for doom (verified against the 2026-07-19 14:10 binary — the
byte-exact image from the hardware video — with pcfx-headless `--y4m`):
BIOS hands off → black → empty bar → a sub-second quick fill as the wedged
W_Init reads fast-fail while ticking the bar → `pcfx_fatal_blink` full ↔
empty at ~1.6 s per phase (hardware: ~1.75 s). The same empty → quick-fill →
blink timeline as the CRT capture, with the blink starting ~0.4 s after the
bar appears in both. One deliberate simplification: each failed read costs
~1–2 ms emulated vs ~30 ms on silicon (real drive command latency isn't
modelled), so the 3 partial fill ticks the video resolves at 30 fps compress
into about one field here.

A structural consequence of observability: the **first** PIO read that
qualifies still delivers (its first sector must land for the CPU to reveal
the transfer method; a 1-sector command therefore completes whole). Hardware
is intermittent (~37 % of reads succeed), so "the first read succeeds, the
bus is poisoned from the first multi-sector one onward" is within the
observed behaviour envelope — 027's tiny-read row failing 0/8 is consistent
with those reps running on a bus already poisoned by the earlier big-read
rows, which is exactly what this models.

Because `RunCDRead()` only runs the per-sector fetch loop for genuine
`READ(10)`/`READ(12)` commands (it's driven by `CDReadTimer`, only armed by
`DoREADBase`), the driver's REQUEST SENSE / mode / TOC replies (`DoSimpleDataIn`)
never reach either hook at all.

### History: earlier, less-correct models

1. **`SCSICD_PendingReadSectors() > 1`** (mid-transfer, latched at the DATA-IN
   data-register read, corrupting the STATUS byte once the driver reached STATUS
   on its own). Intended to exclude REQUEST SENSE/TOC/mode-sense replies, but
   those already read as `SectorCount == 0`; the `>1` bound instead let every
   *single-sector* CD data read through unfailed, and doom-pcfx's boot streams
   most of its UI as many small 1-sector reads — so almost nothing tripped the
   erratum and the game booted to gameplay in the emulator while real hardware
   still blinks.
2. **`SCSICD_PendingReadSectors() > 0`**, same latched/deferred shape (fixed the
   threshold bug above). This correctly reached fatal_blink, but only after the
   loading bar **filled once per attempt** — the driver completes its whole
   undisturbed transfer and only sees CHECK CONDITION once it reaches STATUS —
   a cosmetic "slow load, then blink" instead of hardware's immediate
   straight-to-blink boot.
3. **Up-front refusal before `PHASE_DATA_IN`** (`KING_PIOReadWouldFail()`,
   checked when `CurrentPhase != PHASE_DATA_IN`). Fixed the "fills once, then
   blinks" cosmetics of (2) by refusing before any sector ever lands. But it
   evaluated `!king->dma_receive_active` at a point where that term is *always*
   true, for PIO and phase-driven DMA reads alike (DMA only arms itself after
   observing `PHASE_DATA_IN`, which this check ran before) — silently making the
   predicate vacuous. The model degenerated into "any CD-data read under a hot
   timer fails," which contradicts hardware (DMA is immune) and would falsely
   fail a correct DMA-based doom fix and any retail game whose CD load overlaps
   a fast timer. Not caught earlier because no tested title did a DMA read with
   a hot timer — the one case that separates the two models.
4. **CHECK CONDITION at the sector boundary** (the first sector-boundary
   rework: same latch and decision point as the current model, but the failed
   read got `CommandCCError(MEDIUM_ERROR)`). Structurally sound on the PIO/DMA
   discrimination, but the failure *semantics* contradicted the measured
   timeline: CHECK CONDITION invites libpcfx's REQUEST SENSE (works fine!) and
   SEEK-reposition retry mill — ~seconds of emulated seek per failed read,
   with 1-sector reads never failing at all — where the hardware video shows
   every failed read (retries included) resolving in ~30 ms and probe 027
   shows all 8 attempts of a failed read fitting in a single vblank field with
   recovery never succeeding. The user-visible result was a slow, stalling
   near-empty bar instead of hardware's *empty → quick fill → blink*.

A prior attempt to fail *fast* by synchronously aborting the DATA-IN transfer
mid-byte (`SCSICD_AbortDataInMediumError()`, since removed) was **unsafe**: it
flipped the SCSI phase out from under the CPU while it was *mid-handshake* inside
`eris_scsi_data_in` (between reading a data byte and asserting its ACK), desyncing
the driver's REQ/ACK bookkeeping from the phase the KING had already jumped to,
and wedged the bus permanently in MESSAGE_IN with REQ stuck asserted (verified via
`--auto-run`: millions of busy-wait register reads, zero forward progress).

The current model gets all four properties at once — fails fast, stays safe,
keeps the PIO/DMA discriminator meaningful, **and** matches the measured
failure semantics — by deciding at a sector boundary (late enough that the
transfer method has been observed, early enough that no CPU byte-handshake is
in flight to desync) and by making the consequence a bus poisoning that only
RST clears (matching 027's recovery finding and the video's timeline) instead
of a per-command error status.

## What is measured vs. what is modelled

Measured on real hardware (homebrew probe 027): CPU-PIO CD reads succeed only
~37 % per attempt and the driver's retry does not recover without a full SCSI
reset; failure is orthogonal to display state, KRAM page, and transfer *size when
the timer is quiescent*. The **timer-IRQ-during-DATA-IN** mechanism is the leading
hypothesis (probes 028/029) for why doom (timer hot) fails where waifu (timer off)
does not; the exact SCSI-level corruption was never captured, so this models the
**observed result** — a big PIO read under a fast timer IRQ fails — deterministically.

Deliberate divergence from hardware: hardware is flaky (~37 % succeed); this model
fails **every** qualifying read (kept deterministic by explicit user decision —
reproducible headless runs, md5-comparable frames), so doom **always** reaches
fatal_blink. The wedge does clear on RST, matching 027's recovery finding — but
because the very next qualifying PIO read deterministically re-poisons the bus,
the emulator still **cannot validate the reset-between-retries libpcfx fix**
(each post-reset retry fails again; on silicon it would likely succeed at ~37 %
per attempt). A fix must still be checked on real hardware. If 028-class
failure-reason data is ever captured, tighten this model (e.g. a fixed-seed
per-command success roll would stay run-to-run reproducible while letting the
reset-retry fix boot in-emu).

## Regression coverage: DMA reads under a hot timer must NOT fail

This is the one case model (3) above got wrong, and the case that matters most
going forward: it's the shape of the DMA-based doom fix (count-0 phase-driven
KING DMA, per `waifu-pcfx-count0-dma-cd-read`) that doom must adopt while
*keeping* its ~1 ms level-9 timer running. Because `king->PIOReadSeen` is only
ever set by a CPU read of the SCSI data register — something the KING real-DMA
pump never does — a DMA transfer never trips `KING_PIOTransferShouldFail()`,
timer hot or not. Regression-tested with the erratum at its default (ON):

- `waifupcfx.cue` (homebrew, count-0 phase-driven DMA CD reads) — boots clean.
- `TE_English` (*Team Innocent*), `N-nyuu_pcfxga.chd`, `Makeruna! Makendou Z`
  (retail, KING real-DMA loads) — all boot clean.
- `DoomPCFX/doom_pcfx.cue` (the 2026-07-19 14:10 image from the hardware
  video) — reproduces the video's timeline: empty bar → sub-second quick fill
  → fatal_blink at ~1.6 s per phase, blink onset ~0.4 s after the bar appears.
- `doom-pcfx/doom_pcfx.cue` (current build) — still reaches fatal_blink
  (two md5-stable alternating frames).

The regression case this model was previously blind to — an
`eris_cd_read_kram`-converted doom booting successfully in-emu with the erratum
ON — can't be exercised until doom's SFX load is migrated off
`eris_cd_read_kram_pio`; add it to this battery once that migration lands.

## Reproduce it here

```
# default: the first multi-sector CPU-PIO read poisons the bus at its second
# sector; every read after that fast-fails -> empty bar, sub-second quick
# fill (the wedged reads still tick it), then fatal_blink oscillating
# full<->empty (~1.6 s per phase) forever.  waifu boots to its title.
pcfx-headless --bios-dir <bios> --pcfx --auto-run --frames 15000 \
    --screenshot on.png doom_pcfx.cue

# PCFX_KING_PIO_ERRATUM=0: the PIO read succeeds -> doom boots past the BIOS
# save-device menu into gameplay.
PCFX_KING_PIO_ERRATUM=0 pcfx-headless --bios-dir <bios> --pcfx --auto-run \
    --frames 8000 --screenshot off.png doom_pcfx.cue
```

`--auto-run` pulses RUN/START through the BIOS boot automatically. Compare two
frames a few thousand apart once the bar has stabilized (e.g. 10000 vs 15000) —
one full, one back to the empty outline — that alternation is the fatal-blink.

## Where the blink actually comes from

The old attribution ("the only boot-path I_Error is `adpcm_load_bank`") was
wrong for the video-era binary: that build **ignores** the SFX-bank read's
return value. The video's 3 bar ticks prove `W_Init` ran after the SFX load,
so the blink is a **data-validation I_Error on the garbage/zeroes the wedged
reads return** during `W_Init`'s first few reads. That is why the non-fatal
SFX handler in the current build doesn't change the outcome: with the bus
poisoned, the WAD reads fail too and a later validation I_Error still blinks
(verified in-emu for the current build). The reset-between-retries libpcfx
fix (`e41278e`) is the intended silicon-side cure; see "What is measured vs.
what is modelled" for why this deterministic model cannot green-light it
in-emu.
