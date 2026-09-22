# RAINBOW physical block boundaries and trailing rescale controls

Both `rainbow_accurate.c` and `rainbow_fast.c` count the discarded stuffing byte
in an entropy `FF 00` pair. A terminal `FF` cannot fetch beyond the entropy
budget. The existing parser subtraction of two bytes is retained: MPCONV's
declared data length includes a two-byte inner dummy, following the aligned
entropy, and excludes the four-byte header. Three further zero guard words lie
outside that declared data length.

After decoding 16 macroblock columns, both backends consume complete DC-Y
`0x10..0x1F` rescale controls before chroma processing. The drain accepts already
buffered bits even when no physical bytes remain, but never reads outside the
budget. A separate count of real buffered bits prevents the ordinary decoder's
underflow zero-fill from completing an otherwise truncated control.

In-strip and trailing controls share the same qtable rescale helper. UV DC
continues to use `base >> 2` independently of scale. Null-run colour, IDCT,
palette/RLE decoding, registers, and savestate format are unchanged.

Evidence supplied in the parent workspace:

- `rainbow_findings/RAINBOW_trailing_rescale_bug.txt`, Paul Daniel's QoQ/hardware
  investigation: discarded trailing rescale controls leave stale quantizers.
- `rainbow_findings/MPCONV_RAINBOW_IMPLEMENTATION.md`, sections 2 and 3: stored
  byte counts, inner dummy, buffer-aware bounded tail parsing, and both backends.
- `rainbow_findings/OFFICIAL_SDK_RAINBOW_NOTES.md`: HuC6272 transport guards.

No build or tests were run for this change, as requested by the user. Their
pending checks are both backends, QoQ Disc B scenario FMV, null-heavy portraits
and stills, BIOS/menus, palette/RLE scenes, and streams with stuffed bytes or
truncated tail controls. Compare each backend against its own baseline; their
IDCT results need not be identical. Doom's matching encoder change is in the
separate sibling `doom-pcfx` repository.
