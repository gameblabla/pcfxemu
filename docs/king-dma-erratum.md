# KING SCSI real-DMA "trailing item" erratum

**Status: modelled by default (hardware-accuracy).** This reproduces the real
PC-FX behaviour that hangs `doom-pcfx` at boot, so it is on by default. Set
`PCFX_KING_DMA_ERRATUM=0` in the environment to disable it (retire on the
programmed count -- the pre-accuracy behaviour) for debugging or for software
written around the retired-on-count model.

Verified against the broken `doom-pcfx` (no libpcfx CD-driver workaround): with
the model on (default) the boot progress bar freezes at its first step and never
advances -- identical frames across tens of thousands of emulated frames, exactly
as on real hardware -- while `PCFX_KING_DMA_ERRATUM=0` boots it to the DOOM title.

## The symptom being modelled

`doom-pcfx` on real PC-FX hardware wedges at its very first CD access: the boot
progress bar reaches its first step (pushing the ADPCM PCM bank into KRAM via a
KING SCSI DMA), then hangs there and the bar flashes. The same code runs to the
title screen fine under every emulator, including this one before this change.

Two secondhand accounts from developers who worked with the KING SCSI engine:

> guy1: I recall that was working for my previous games that used liberis / But
> it was also kind of glitchy as well / probably the delay you put is not too
> high or something / or some other bullshit

> guy2: the original author didn't seem to understand SCSI protocol. And I think
> there was something about waiting for a data item past the end of the list. /
> like if you want less than full sector, it still receives full sector but it
> disposes the leftovers

## The hypothesis

Those two remarks by guy2 describe one coherent quirk of the KING **real-DMA**
engine (the auto-ACK hardware DMA-to-KRAM path in `DoRealDMA()` — *not* the
CPU-polled pseudo-DMA path in `KING_Read16`/reg 0x0f):

> The engine does not complete when the programmed transfer count reaches zero.
> It completes one REQ/ACK handshake later, and that trailing item is discarded
> rather than written to KRAM.

Consequences, and why they split the world into "works (glitchy)" vs "wedges":

* **Request shorter than the sector the drive delivers.** The drive always sends
  a whole physical sector regardless of the requested length (guy2's second
  remark), so bytes are still sitting in DATA IN when the count hits zero. The
  trailing handshake happens immediately, the transfer completes, and the extra
  byte is dropped. This is the "kind of worked, but glitchy" case — data is
  intact, but the driver's leftover handling is where the glitches came from.

* **Request exactly equal to the delivered length.** Nothing is left over. The
  target drops to STATUS phase the instant the last requested byte is ACKed. The
  trailing handshake never arrives, so the count never falls to zero and the
  transfer never completes. A driver that polls for completion wedges here.

`doom-pcfx` rounds every DMA request up to a whole number of 2048-byte sectors
(`platform/i_system_pcfx.c`, `pcfx_king_dma_cd_to_kram`), so its PCM-bank load is
the exact-length case — hence the wedge, while liberis games that read partial
sectors into RAM only ever hit the glitchy-but-works case.

### Why the wedge freezes the CPU (not just the count)

libpcfx's `eris_scsi_check_dma()` (libpcfx/src/scsi.S) tests **`DMATransferSize`
(reg 0x0A) / `DMAStatus` (reg 0x0B)** for completion. Pinning the count alone is
*not* enough to reproduce the real-hardware symptom: every poll loop in the CD
driver (the DMA-done spin, `eris_scsi_eat_data_in`, `cd_wait_status`) is bounded,
so with only a stuck count the driver would spin those out, fail the read, run
its 8 retries, and reach `I_Error()`/`pcfx_fatal_blink()` — i.e. the boot bar
would *advance* to a full flashing bar. On real hardware the bar instead stays
frozen at its **first step**: the read never returns at all.

The missing piece is that while the DMA is wedged, the KING holds the CPU's I/O
bus — every access to a KING register stalls until the DMA can service it, which
it never can. The CPU therefore makes no forward progress on its very first poll;
it never even counts its spin loop out. That is what pins the boot bar at the
first step forever.

## Implementation

`mednafen/pcfx/king.c`:

* `DoRealDMA()` + the `DMARetirePending` field. When the count would reach zero
  and the erratum is enabled, the engine sets `DMARetirePending`, pins
  `DMATransferSize` at 2, and keeps the DMA-receive pump ACKing. If another
  DATA-IN byte is available it retires normally on the next handshake (leftover
  case); if the target has dropped to STATUS the pump stops and the count stays
  pinned (wedge case). Reprogramming reg 0x0A clears the pending flag so a
  retried transfer can't retire early on its first byte.

* `KING_DMAWedgeStallCycles()` — while `DMARetirePending` is set, KING register
  reads (`io-handler.inc`, ports 0x600–0x6FF) are charged a large per-access
  stall, modelling the bus hold above. The exact figure is unobservable; it only
  has to dwarf any poll loop's own iteration budget so the loop can't count
  itself out within any realistic run. Gated on `DMARetirePending`, which is only
  ever set with the erratum enabled, so ordinary (retiring) DMAs are unaffected.

Verified: with the model on (default) the boot bar is byte-for-byte identical
across tens of thousands of emulated frames (frozen at the first step); with
`PCFX_KING_DMA_ERRATUM=0` doom-pcfx boots to the title.

## How to confirm / falsify this

This is inferred from two forum quotes plus the observed doom-pcfx behaviour, not
from a datasheet or a logic-analyzer capture. To settle it on real hardware:

1. On real hardware, issue a KING real-DMA-to-KRAM of an **exact** multiple of
   2048 bytes and poll reg 0x0A. Hypothesis predicts it never reaches 0.
2. Repeat with a request a few bytes **short** of a sector. Hypothesis predicts
   it completes (count reaches 0), with the trailing byte absent from KRAM.
3. If (1) actually completes on hardware, this model is wrong — delete it (the
   `PCFX_KING_DMA_ERRATUM` gate keeps it isolated so nothing else depends on it).

## Reproduce it here

```
# default: wedges at the PCM-bank load; the boot bar freezes and never advances
# (matches hardware).  Run a large --frames to confirm it never recovers.
pcfx-headless --bios-dir <bios> --pcfx \
    --commands run.cmd --frames 24000 --screenshot on.png doom_pcfx.cue

# PCFX_KING_DMA_ERRATUM=0: retires on the programmed count, so it boots to the
# DOOM title (the pre-accuracy behaviour).
PCFX_KING_DMA_ERRATUM=0 pcfx-headless --bios-dir <bios> --pcfx \
    --commands run.cmd --frames 24000 --screenshot off.png doom_pcfx.cue
```

`--auto-run` does not reliably clear the PC-FX BIOS save-device menu for this
disc; drive START explicitly via `--commands` (pulse START a few times over the
first ~100 frames) to boot the game.
