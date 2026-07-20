# KING CPU-PIO SCSI read failure (doom-pcfx boot hang)

**Status: modelled by default (hardware-accuracy).** Reproduces the real PC-FX
behaviour that hangs the *current* `doom-pcfx` at boot. Set
`PCFX_KING_PIO_ERRATUM=0` to disable it (the CPU-PIO read succeeds, so doom boots
to the title — the pre-accuracy behaviour, and the mode to use once a
doom/libpcfx workaround exists).

This is a **sibling** of `king-dma-erratum.md`, for a different transfer path.
That erratum models the KING **real-DMA-to-KRAM** engine; the *current* doom-pcfx
no longer uses it. doom now loads its ADPCM SFX bank with a **CPU-driven PIO
read** (libpcfx `eris_cd_read_kram_pio`: the CPU manually reads the SCSI data
register and toggles ACK), and that PIO read is what fails on real hardware.

## The symptom being modelled

On real hardware the *current* (PIO) doom-pcfx reaches its loading bar, which
then oscillates unfilled↔full-white forever. That is `pcfx_fatal_blink()` —
`I_Error`'s halt indicator. The only boot-path `I_Error` that reaches it is
`adpcm_load_bank` (doom-pcfx `platform/i_sound_pcfx.c`): the program's **first CD
read**, the ~200 KB (100-sector) ADPCM SFX bank via `eris_cd_read_kram_pio`,
returns 0 after all its internal retries.

Verified with this model on (default): `doom_pcfx.cue` boots, the BIOS loads the
program (that boot uses the KING real-DMA path, unaffected), doom runs
`I_InitSound`, its first PIO read fails, and the boot bar sits at the bottom of a
black screen **oscillating** (fatal-blink) forever — matching a phone capture of
real hardware. With `PCFX_KING_PIO_ERRATUM=0` the same disc boots to E1M1
gameplay.

## What is measured vs. what is modelled

Measured on real hardware (homebrew probe 027, which ran cleanly):
- CPU-PIO CD reads succeed only **~37% per attempt**; the driver's retry does not
  recover without a **full SCSI reset**; failure is orthogonal to display state,
  KRAM page, and transfer size; reads fail *fast*.

**Not** measured: the exact SCSI-level failure reason (command reject /
never-reached-DATA-IN / short transfer / bad status) — the probe meant to capture
it (028) never ran (see `doom-pcfx/docs/realhw-boot-read-failure.md`). So this
models the **observed result**, deterministically, rather than a known mechanism:
> A CPU-PIO DATA-IN transfer completes, but its STATUS byte reads back non-GOOD,
> so the driver's read fails.

Deliberate divergence from hardware, for a reliable repro: hardware is *flaky*
(~37% succeed); this model fails **every** CPU-PIO read while enabled, so doom
**always** hangs. Consequently this model **cannot validate a doom/libpcfx fix**
(e.g. retry-with-reset) — turning the gate off just makes every read succeed. A
fix must still be checked on real hardware. If the 028-class failure-reason data
is ever captured, tighten this model to match (and possibly make it probabilistic
+ reset-recoverable).

## Implementation (`mednafen/pcfx/king.c`)

- `king_pio_erratum_enabled()` — cached `getenv("PCFX_KING_PIO_ERRATUM")`,
  default ON, mirroring `king_dma_erratum_enabled()`.
- `bool king->PIODataInSeen` — set TRUE when the CPU asserts ACK (`Reg01` bit
  0x10, the reg-01 write at the `SCSICD_SetACK(V & 0x10)` site) **while in DATA IN**
  (`IO=1, CD=0`). The BIOS's real-DMA boot loader auto-ACKs inside the KING pump
  (`SCSICD_SetACK` in `KING_Update`) and never writes this bit, so BIOS boot is
  unaffected — only CPU-PIO transfers set the flag.
- STATUS-byte corruption at the SCSI data register read (`KING_Read16`, AR==0 /
  `case 0x00`): when `PIODataInSeen` and enabled and the phase is STATUS
  (`IO=CD=1, MSG=0`), OR `0x0004` into the returned byte (non-GOOD, and not
  CHECK_CONDITION so the driver takes its bus-recover path, not REQUEST SENSE),
  then clear `PIODataInSeen` (one-shot per transfer). DATA-IN bytes read through
  the same register are untouched (phase guard).
- Cleared on `KING_Reset` and on a SCSI bus reset (RST, `Reg01` bit 0x80).
- Saved in the state block (`SFVARN(king->PIODataInSeen, "PIODataInSeen")`).

## Reproduce it here

```
# default: doom's first PIO read fails -> boot bar oscillates forever (hang).
pcfx-headless --bios-dir <bios> --pcfx --commands run.cmd --frames 6000 \
    --screenshot on.png doom_pcfx.cue

# PCFX_KING_PIO_ERRATUM=0: PIO read succeeds -> boots to E1M1 gameplay.
PCFX_KING_PIO_ERRATUM=0 pcfx-headless --bios-dir <bios> --pcfx --commands run.cmd \
    --frames 8000 --screenshot off.png doom_pcfx.cue
```

`run.cmd` should pulse START a few times over the first ~300 frames to clear the
BIOS save-device menu. Two frames ~0.3 s apart at the hang differ (the bar
alternates fill/frame) — that is the fatal-blink, confirming it reached
`pcfx_fatal_blink`, not merely a static bar.
