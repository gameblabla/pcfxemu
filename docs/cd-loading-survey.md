# CD-loading survey: how real PC-FX titles drive the SCSI/KING CD engine

Background: this fork models two real-hardware CD-transfer errata (see
`docs/king-pio-read-erratum.md` and `docs/king-dma-erratum.md`) discovered while
getting `doom-pcfx` to boot and load levels reliably. Both are hypotheses inferred
from a handful of real-hardware probes (doom-pcfx `examples/027-030` in libpcfx),
not from documented silicon behaviour. Before committing to a CD-loading strategy
for `doom-pcfx`/`libpcfx`, this session ran several **real commercial PC-FX disc
images** through `pcfx-headless` with the new `CD_XFER_DEBUG` profiler
(`mednafen/pcfx/cd_xfer_debug.h`, `make -f Makefile.headless CD_XFER_DEBUG=1`) to
see how real, shipped software actually drives the CD engine — ground truth to
check the errata models and our own driver design against, not just theory.

**Method:** boot each disc's own BIOS-native boot sequence under
`pcfx-headless --auto-run`, stock settings (PIO-read erratum default ON, DMA
erratum default OFF), and capture the `[CDXFER]` trace: one line per SCSI READ
command dispatch (opcode/LBA/sector count) and one per KING DMA arm / CPU-PIO
data-register touch. No game code was modified or redistributed — these are
read-only observations of existing legally-owned dumps' own driver behaviour,
used the same way a logic analyzer would be on real hardware.

**Discs surveyed** (`/home/anonymous/Documents/DEV/pc/Games/`):
- Team Innocent - The Point of No Return (retail, English fan translation build)
- Makeruna! Makendou Z (retail)
- Same Game FX (PC-FXGA / official GMAKER SDK)
- N-nyuu - PC-FXGA Game ga Asoberu Tsukureru Hon (PC-FXGA / official GMAKER SDK)

## Finding 1: none of them ever use CPU-PIO for CD data

Zero CPU-PIO SCSI data-register reads across all four traces, including titles
whose bulk loads span tens of thousands of sectors. Every byte of every CD read
observed moves through the KING's own hardware SCSI-to-KRAM DMA engine. This
matches the PIO-read erratum's premise directly: real developers apparently never
issue a CPU-driven multi-sector CD transfer at all, so whether or not a fast timer
IRQ is live during one essentially never arises in shipped software. (`doom-pcfx`
used a CPU-PIO path, `eris_cd_read_kram_pio`/`eris_cd_read`, specifically to dodge
the *other*, DMA-side erratum — see Finding 2 — which the real titles' own
strategy shows was solving the wrong problem.)

## Finding 2: two different, but both DMA-based, strategies for avoiding the count-bounded erratum

The count-bounded "retire one handshake past the programmed count" erratum
(`docs/king-dma-erratum.md`) only bites a DMA arm whose programmed count exactly
matches what the drive delivers for that arm, with nothing left over. Real
software avoids the exact-match case two different ways, split cleanly along SDK
lines:

**PC-FXGA titles (Same Game FX, N-nyuu, official GMAKER SDK) arm one single
COUNT-0 (phase-driven) DMA for the whole transfer**, e.g.:
```
READ(10) lba=3272 sectors=300
DMA arm: dest=00000 page=0 size=0 words (phase-driven/count-0)
```
One command, one arm, done — there is no programmed count to overrun, so the
erratum cannot trigger by construction. This is exactly the fix already validated
for `waifu_card_game` (see memory `waifu-pcfx-count0-dma-cd-read`) and is now
`libpcfx`'s `eris_cd_read_kram()`.

**Retail titles (Team Innocent, Makeruna) instead continuously re-arm small,
fixed-size, COUNT-BOUNDED chunks** for the same kind of bulk load, refilling a
double buffer as each chunk retires:
```
READ(10) lba=97173 sectors=21847        # Makeruna, ~44.7 MB in one command
DMA arm: dest=00000 page=0 size=16814 words (count-bounded)
DMA arm: dest=00000 page=1 size=263 words (count-bounded)
DMA arm: dest=20000 page=1 size=263 words (count-bounded)
DMA arm: dest=00107 page=1 size=263 words (count-bounded)
... (thousands more, fixed 263-word/526-byte bursts)
```
```
READ(10) lba=33924 sectors=14628        # Team Innocent, ~28.5 MB in one command
DMA arm: dest=00000 page=1 size=128000 words (count-bounded)
DMA arm: dest=20000 page=1 size=129024 words (count-bounded)
DMA arm: dest=00000 page=1 size=126976 words (count-bounded)
... (ping-pong between two ~128000-word half-page buffers)
```
Every chunk boundary here is, in principle, a place the modeled erratum *could*
trigger if a chunk's count happened to exactly match what's left in the transfer
— and for Makeruna's fixed 263-word bursts against an arbitrary total, the final
chunk almost certainly does not divide evenly. That these ship on real retail
hardware regardless suggests either the count-bounded erratum's exact-match
condition is narrower on real silicon than our model assumes, or these titles'
low-level driver does something around the final chunk (buffer padding, an extra
dummy re-arm, a different completion signal) not visible from the DMA-arm trace
alone. `libpcfx`'s own `eris_cd_read_dma()` (below) takes the conservative
reading: full-size chunks are count-bounded (fast, matches the real pattern), but
the *final* partial chunk — the only one that can coincide with the transfer's
true end — is armed count-0 instead, closing the gap regardless of which reading
is correct.

## Applied to libpcfx / doom-pcfx

- **`eris_cd_read_kram()`** (`libpcfx/src/cd.c`) now arms a single count-0
  (phase-driven) DMA for the whole transfer, matching the PC-FXGA/GMAKER SDK
  pattern above. `doom-pcfx`'s sky/ADPCM-bank/framebuffer CD→KRAM loads
  (`platform/i_system_pcfx.c`) default to this (`PCFX_CD_KRAM_USE_PIO` opts back
  into the older CPU-PIO path). Real hardware DMA speed instead of a CPU byte
  pump, and immune to both modeled errata by construction.
- **`eris_cd_read_dma()`** (`libpcfx/src/cd.c`) implements the chunked,
  continuously-re-armed pattern for CD→RAM (there is no DMA engine straight to
  system RAM, so it bounces through a small caller-supplied KRAM scratch window):
  full scratch-sized chunks are count-bounded, the final chunk is count-0. It
  ~~measurably slower per read than expected against small, scattered reads for
  a reason not yet root-caused~~ **RESOLVED — see
  `docs/cd-ram-dma-bounce-investigation.md`**: the slowness was two
  per-chunk-boundary race conditions (a DRQ-latch wedge cured by the
  retail-style reg-2 DMA-mode toggle between re-arms, `eris_scsi_pause_dma`;
  and a completion-IRQ-acknowledge consumed by a redundant
  `eris_scsi_check_dma()` re-check), not a throughput property of the bounce
  design. Fixed, the path now beats `eris_cd_read()` CPU-PIO on every measured
  shape (controlled A/B bench: `libpcfx/examples/031_cd_ram_read_bench`).
  **Not yet wired into doom-pcfx's WAD/map-pack readers**
  (`platform/pcfx_wad.c`, `pcfx_mappack.c`) — real-hardware validation of the
  fixed chunked path (the bench ROM doubles as the hardware probe) should come
  first.
