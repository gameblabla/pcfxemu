/* cd_xfer_debug.h -- optional CD-load transfer profiler.
 *
 * OFF by default (zero overhead: the macro compiles to nothing). Build with
 * `make -f Makefile.headless CD_XFER_DEBUG=1` (adds -DPCFX_CD_XFER_DEBUG=1)
 * to turn it on.
 *
 * Purpose: this fork models two real-hardware CD-transfer errata (see
 * docs/king-pio-read-erratum.md and docs/king-dma-erratum.md) that only bite
 * specific transfer *shapes* -- CPU-PIO reads under a hot interval timer, and
 * count-bounded (not phase-driven/count-0) KING real-DMA transfers. Working
 * out what shape of transfer a piece of homebrew (or a retail disc, run here
 * purely to observe its own driver's behaviour -- never redistributed, never
 * shipped) actually issues is otherwise a matter of guessing from source (if
 * you have it) or blind disassembly. This prints one line per SCSI READ
 * command dispatch and one per KING DMA arm/CPU-PIO register touch, so a
 * `pcfx-headless` run's stderr *is* the CD-driver trace: command opcode/LBA/
 * sector count, DMA vs CPU-PIO, count-bounded vs phase-driven (size==0) DMA,
 * and whether the interval timer was live at the time -- directly the inputs
 * both erratum models key off. See docs/cd-loading-survey.md for what this
 * showed run against several real PC-FX titles' own disc images.
 */
#ifndef _PCFX_CD_XFER_DEBUG_H_
#define _PCFX_CD_XFER_DEBUG_H_

#ifdef PCFX_CD_XFER_DEBUG
#include <stdio.h>
#define CDXFERDBG(...) fprintf(stderr, "[CDXFER] " __VA_ARGS__)
#else
#define CDXFERDBG(...) do { } while(0)
#endif

#endif
