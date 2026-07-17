# KING SCSI real-DMA "trailing item" erratum (hypothesis)

**Status: unproven hypothesis, opt-in.** Enabled with `PCFX_KING_DMA_ERRATUM=1`
in the environment; off by default, in which case the emulator behaves exactly
as it did before this was added.

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

### Why the wedge is visible to the driver

libpcfx's `eris_scsi_check_dma()` (libpcfx/src/scsi.S) ultimately tests
**`DMATransferSize` (reg 0x0A) == 0** for completion — reg 0x0B returns the
one-shot IRQ-acknowledge latch, not a live status the poll can rely on. So the
model must express the erratum *in the count*: it holds `DMATransferSize` at 2
(one item short of zero) until the trailing handshake arrives. In the wedge case
that handshake never comes, the count stays at 2, `eris_scsi_check_dma()` never
reports done, `eris_cd_read_kram()` exhausts its 8 retries, and
`adpcm_load_bank()` calls `I_Error()` -> `pcfx_fatal_blink()`, which loops
forever alternating the load-bar fill/frame colour. That infinite blink is both
the observed **hang** and the observed **flashing**.

## Implementation

`mednafen/pcfx/king.c`, `DoRealDMA()` and the `DMARetirePending` field. When the
count would reach zero and the erratum is enabled, the engine sets
`DMARetirePending`, pins `DMATransferSize` at 2, and keeps the DMA-receive pump
ACKing. If another DATA-IN byte is available it retires normally on the next
handshake (leftover case); if the target has dropped to STATUS the pump stops and
the count stays pinned (wedge case). Reprogramming reg 0x0A clears the pending
flag so a retried transfer can't retire early on its first byte.

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
# wedges at the PCM-bank load, then flashes forever (matches hardware):
PCFX_KING_DMA_ERRATUM=1 pcfx-headless --bios-dir <bios> --pcfx \
    --commands run.cmd --frames 12000 --screenshot on.ppm doom_pcfx.cue

# default (unset): boots to the DOOM title as before.
pcfx-headless --bios-dir <bios> --pcfx \
    --commands run.cmd --frames 12000 --screenshot off.ppm doom_pcfx.cue
```

`--auto-run` does not reliably clear the PC-FX BIOS save-device menu for this
disc; drive START explicitly via `--commands` (pulse START a few times over the
first ~100 frames) to boot the game.
