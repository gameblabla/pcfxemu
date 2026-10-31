# PC-FX CD drive model (what the emulator refuses, and why)

The emulated CD block used to accept anything that worked "well enough" for a
disc image: any `MODE SELECT` block length, any sector regardless of its header or
EDC, a track layout derived by guessing, and a TOC that always said "disc ID 00".
A RAINBOW FMV disc built against that behaviour booted and played in
`pcfx-headless` and then **failed on real hardware** (black screen after the BIOS
boot, or "spins forever" on discs whose TOC was wrong).  The emulator is now a
strict model of the NEC drive.  `PCFXEMU_CD_LEGACY=1` restores the old permissive
behaviour for A/B comparisons only.

Sources, in order of trust: **[doc]** NEC `SCSICMD_E.txt` (PC-FX SCSI command
reference, `pcfx_toolchain/DOCUMENTATION/ENGLISH_TRANSLATION/SCSI/`), **[cdr77]** NEC
CDR-75/77 SCSI specification (the PDF next to it), **[bios]** the decompiled BIOS
v1.01 (`/home/anonymous/CD/BIOS_Source`), **[hw]** measured on real hardware (cd-info
of burned discs, tester reports), **[assume]** inferred, not verified.

## MODE SELECT

| Condition | Behaviour | Source |
|---|---|---|
| parameter list length 0 | restore defaults, no DATA OUT | [doc] |
| PF=0 | vendor-unique list, **exactly 10 bytes**, block descriptor length (byte 3) 0, byte 4 = `XA SH 0 0 0 EC EJ(2)`, byte 9 = retry (0..15) | [doc] ch.4 |
| PF=0, other length / BDL≠0 / reserved bits | CHECK CONDITION (ILLEGAL REQUEST, invalid parameter), mode unchanged | [doc] "Byte 4 must be 0Ah" [cdr77] |
| PF=1 | SCSI-2 list: 4-byte header, optional 8-byte block descriptor (density +0, block length +5..+7), pages | [doc] ch.3 |
| PF=1 density/length | density `00h`: 2048, 2336, 2340. density `81h`: 2048, 2056, 2324, 2332, 2336, 2340. Anything else (2352, 2324 under `00h`, unknown density) is refused | [doc] 3-3 |

Transfer window per mode (offset into the 2352-byte sector, length):

| XA | SH | EJ | transfer |
|---|---|---|---|
| 0 | x | 00/01 | 2048 @16 (Mode 1 and Mode 2 are *not* told apart) |
| 1 | 0 | 00/01 | 2056 @16 (subheader + Form 1 data) |
| 1 | 1 | 00/01 | 2048 @24 (Form 1 data) |
| x | x | 10 | 2336 @16 |
| x | x | 11 | 2340 @12 |

(SCSI-2: `00h`/2048 = @16, `81h`/2048 = @24, 2324 = @24, 2056/2332/2336 = @16, 2340 = @12.)

`MODE SENSE` reports the *current* density and block length, so the BIOS's
"save mode / `MODE SELECT` density 81h / restore" Photo CD probe round-trips.
`READ CAPACITY` reports the current block length.

## Defaults depend on the DISC ID

Defaults (power-on, SCSI bus reset, disc change) are `XA=0 SH=0 EC=0 EJ=00 retry=15`
**unless TOC point A0's PSEC byte is `20h` (CD-ROM XA)**, in which case they are
`XA=1 SH=1 EJ=01` [doc].  The BIOS reads the boot sectors with `READ(10)` in the
default mode and never issues a `MODE SELECT` first [bios], so a burner that marks a
disc as XA makes every Mode 1 boot sector come back **8 bytes shifted** (data from
byte 24) and the 2048-byte compare against the system sector fails.

* `GET DIRECTORY INFO` (`DEh`, mode 3, A0) now returns the disc ID in PSEC.
* For CUE/BIN images the ID defaults to `00h`.  Set it with `REM PCFX_DISC_ID 20` in
  the CUE or `PCFXEMU_CD_DISC_ID=20` in the environment.
* Physical drives pass through the A0 PSEC the host drive reports.

## Error judgement

Per [cdr77]: "error judgement is made with the information of the disk header.  In
Mode 1 an error check is made by ECC.  No error check is made in Mode 2."

* No sync pattern, or header MSF ≠ the address read → MEDIUM ERROR, header read error
  (this is what a read of pregap silence, or of the audio track, looks like).
* Mode 1: EDC must match, else MEDIUM ERROR (L-EC uncorrectable).  Only the EDC is
  checked, as drives stop L-EC once the EDC is fine.
* Mode 2 formless (what RAINBOW's 2336-byte stream is): never rejected for EDC.
* XA modes: a Form 1 sector (submode bit 5 clear) is EDC-checked, Form 2 is not.
  **[assume]** — the documents do not spell this out.
* A `READ` that runs from a data track into an audio track stops with MEDIUM ERROR,
  not-a-data-track.

## CUE / BIN layout

The old loader mixed up file-relative and disc-relative times, so a single BIN with
several tracks landed on the wrong LBAs ("known limitation" in the RAINBOW README).
Now:

* One running disc position; each `FILE` starts where the previous one ended.
* `INDEX 01` is the TOC entry of the track.  `INDEX 00 .. INDEX 01` is its pregap,
  stored in the file and readable (as on N-nyuu and retail discs).
* `PREGAP mm:ss:ff` inserts that many generated sectors (zero data) in front of the
  file; `REM PCFX_DISC_ID xx`; `TRACK MODE2/2336` (sync + header synthesised).
* A track ends at the next track's `INDEX 00` (same file) or at its file's end.

## Diagnostics

* `PCFXEMU_CD_TRACE=1` — every CDB (with command name), every `MODE SELECT` decision
  and the reason for every refused sector, to stderr.
* `tests/run_cd_drive_mode_test.sh` — SCSI-initiator test of all of the above.
* Regression check used for this change: Miraculum, N-nyuu, Makeruna, Team Innocent,
  doom_pcfx, wolf_pcfx, waifupcfx and the KING bench disc render **identical frames**
  before and after (1800 frames each).

## What is still not modelled

* Retry counts and timing of failed reads (an error is reported at once).
* L-EC *correction* (a sector with a bad EDC is never repaired).
* The Q-subchannel control bits of pregaps are taken from the track, not from the
  burner's actual output.
* The exact sense codes of the real drive for refused lists (we use the NEC "invalid
  parameter" code).
