# PC-FX / Digimon World 2003 v55 — experimental Mode 2 Form 2 FMV extension

This is a patch-and-test deliverable for the **user-supplied PCFXEMU v43
SDL3 pair-locked source** and the **user-supplied Digimon World PC-FX v55
TRELLIS project**. It is NOT a replacement for either full original project.

## What works

* PC-FX SCSI MODE SELECT(6) eight-byte block descriptor requests logical
  READ(10) sector views 2048, 2324, 2336, 2340 or 2352. MODE SENSE(6) and
  READ CAPACITY reflect the selected block size. Reset restores 2048, and
  emulator save states retain the choice. Existing physical-sector timing
  remains based on the 75/150-sector-per-second drive model, not a proportionally
  faster drive.
* Physical Mode 2 Form 2 sectors use 12-byte sync, MSF address, mode 02,
  duplicated XA subheader with DATA|FORM2, 2324 application bytes and a
  four-byte CD-ROM EDC. A separate Mode 1/2352 boot track has Mode 1 EDC/ECC.
* The PCFV writer/reader supports 2324-byte logical sectors. Video records
  can span multiple sectors and are indexed by sector and exact byte length.
* The native player issues nonblocking MODE SELECT(6) before initial READ(10),
  batches video reads into an existing multi-buffer KRAM ring, and uses
  **one 29-sector READ(10) per 65536-byte ADPCM half**. To keep the half-ring
  uncorrupted, each full audio chunk has 1860 *leading* padding bytes; the
  player's guarded KRAM DMA preserves/restores any overlapping audio tail.
  A reserved 930-word gap protects lower video KRAM.
* The source-based TRELLIS option accepts --form2 and uses the previous
  per-field 2048-byte-sector allocation converted to a 2324-byte payload
  budget, without increasing frame rate, raster size or physical transfer
  rate. Four fields 1400–1403 have independently source-measured better SSE.
  Other fields are unchanged.

## Measured results (PCFXEMU patched headless + user's pcfx.rom)

Unchanged v55 baseline and the enhanced Form 2 mixed-mode disc were both
emulated for 8500 display fields, using ROM symbol addresses reconstructed
from the exact original v55 source and independently from the new player.

                                    Unchanged v55   Enhanced Form 2
  Boot stage / title handoff              4                4
  Video pictures presented              1777             1777
  Stale video records dropped              0                0
  ADPCM underruns                          0                0
  SCSI DMA errors                          0                0
  Cadence intervals exactly 4 fields    1775             1766
  Longer-than-4-field intervals            0                9
  Maximum cadence gap (display fields)     4               12
  Audio-relative late-frame indications  91             1577

Therefore the experimental build **does not match baseline frame cadence or
audio-relative sync**, even though all expected movie pictures are shown and
the title handoff succeeds. `g_video_frames_audio_late` is a player-side count
of displays behind the derived ADPCM clock target, NOT a measured millisecond
lip-sync offset or an ADPCM PCM checksum. Actual physical CD compatibility,
instantaneous throughput and real hardware audio/video synchronization remain
unverified.

The authored 119.47-second clip contains 11705 original 2048-byte physical
sectors versus 11546 Form 2 physical sectors: average required physical
transfer rates 97.98 versus 96.65 sectors/sec, below the model's nominal
150 sectors/sec at 2x. These are derived *required* rates from total stream
length and duration, not an instrumented real-time throughput trace.

The source-based SSE for 8 tested original fields (picture numbers 700–703)
decreased by 0.128 dB overall; four
fields improved and four were unchanged. This is **not** a whole-movie PSNR or
an unqualified claim that all video looks better. The first four replacements
remain within their old 2048-based physical sector counts when authored to 2324.

## Included paths

- `emulator/mednafen/cdrom/scsicd.c`: patched emulator backend.
- `emulator/pcfx-headless`: patched Linux headless binary (ELF); GUI binary
  not built or validated.
- `project/src/`: native V810 Form 2 player and ADPCM buffer changes.
- `project/tools/video_codec/`: Form 2 packer, ADPCM reader/writer,
  source-measured TRELLIS RDO and the four validated field bitstreams.
- `project/tools/build_form2_disc.py`: mixed-mode mastering script.
- `project/tests/`: host raw-sector and AV integrity test scripts.
- `project/verification/`: host/decoder/SSE/emulator reports.
- Full BIN/CUE in a separate release ZIP; optional unpacked file links.

No PC-FX BIOS, PS1 STR source, PC game resources, external V810 toolchain
or vendor library is included in this patch ZIP. Those remain in the
user-supplied inputs. The BIN/CUE is an experimental bootable disc containing
assets derived from the user-supplied project.

## Reproduce from original project archives

Overlay the contents of `project/` onto the original `PCFX_v55_project/`
directory, and overlay the `emulator/mednafen/cdrom/scsicd.c` file onto the
same relative path within the supplied PCFXEMU source. On Linux with a
compatible compiler:

```
make -f Makefile.headless CHD=NO -j4
```

From the patched `PCFX_v55_project/` directory with `PCFX_TOOLKIT_ROOT`
pointing at a toolkit containing `toolchain/v810-gcc`, `vendor/libpcfx`, and
`toolchain/bin/pcfx-cdlink-large`:

```
python3 tools/video_codec/build_form2_trellis.py
python3 tools/build_form2_disc.py --toolkit "$PCFX_TOOLKIT_ROOT" \\
  --stream assets/intro_480i_v55_form2_trellis_enhanced.pcfv \\
  --output-prefix release/PCFX_v55_Form2_TRELLIS_Experimental
python3 tests/form2_trellis_integrity.py \\
  assets/intro_480i_v55_trellis.pcfv \\
  assets/intro_480i_v55_form2_trellis_enhanced.pcfv \\
  release/PCFX_v55_Form2_TRELLIS_Experimental.bin
```

Transport-only v55 repack, without TRELLIS replacements:

```
python3 tools/video_codec/form2_pack.py \\
  --input assets/intro_480i_v55_trellis.pcfv \\
  --output assets/intro_480i_v55_form2.pcfv \\
  --raw-track assets/intro_480i_v55_form2_raw2352.bin
```

To regenerate more TRELLIS candidates using the original MDEC source, decode
the source into **256x480 RGB24, 15fps**, then use
`python3 tools/video_codec/v55_rainbow_trellis.py --source SOURCE.rgb24
--baseline assets/intro_480i_v55_trellis.pcfv --output CANDIDATE.pcfv
--pictures 700 --form2 --lambdas 1 2 3 --report REPORT.json`.
The source image must be original-derived, not reconstructed from v55 lossy
video. The encoder optimizes pixel SSE against that original source and keeps
the old physical sector count per field. Its candidate output is **not**
automatically validated for cadence or console compatibility.

## Caveats

Mode 2 compatibility follows the emulator's new software interface;
CD-G/Photo CD support does not prove the actual PC-FX drive accepts the same
MODE SELECT(6) 2324-byte data path. A real PC-FX hardware test is required.
The native patch changes KRAM/audio DMA scheduling; the emulator reports
nine long intervals versus zero in original v55 after 8500 fields. Do not
use this experiment as a drop-in guaranteed-smooth v55 release.
