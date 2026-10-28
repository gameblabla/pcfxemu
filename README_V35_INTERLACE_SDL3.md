# PCFXemu SDL3 interlace and sharp scaling

## Interlace presentation

The SDL3 presentation filters operate on a staging buffer. They do not change
TETSU field order, KING, RAINBOW, KRAM, or the emulated framebuffer. They apply
only while the core reports an interlaced 480-line mode.

- **Weave** displays both stored field parities and preserves full vertical
  detail, with possible combing on motion.
- **Bob** doubles the newest field to all output rows.
- **Raw** displays only the newest field's scanlines and paints the other
  parity opaque black.
- **Motion-adaptive** weaves static 8x4 field-line tiles and bobs moving tiles.
- **Pair-locked weave** holds the last completed odd/even image. It publishes a
  new image only after both fields arrive, and bobs the latest field until the
  first complete pair is available.

The filters update once per newly emulated field, even if SDL redraws without
advancing emulation. The video menu cycles through all five modes. The saved
setting is interlace_mode=0..4; the old interlace_bob setting is still read
for existing configurations. Command-line options are --weave, --bob,
--raw-interlace, --motion-adaptive, and --pair-locked.

F9 captures the original emulated framebuffer. The SDL3 window does not blend
the video texture with the window background, which keeps raw-mode black lines
opaque.

## Sharp integer scaling

The SDL3 frontend defaults to nearest-neighbor filtering. In **Integer scale
(sharp)** mode, the 256- and 344-pixel PC-FX modes use separate whole-number
horizontal and vertical scales, up to 3x per axis. This keeps 480i within
normal-height displays while widening each source pixel sharply. The usual
1280x960 window renders 256x480i at 768x960 and 344x480i at 1032x960; the
256x240 and 344x240 modes render at 768x720 and 1032x720. Smaller windows can
reduce those integer factors. Integer scale always uses nearest-neighbor even
if the saved filter preference is linear. The 4:3 and Stretch aspect choices
remain available; linear filtering can be selected for those modes.

Use --integer-scale (or --native-aspect) to select the sharp integer mode
from the command line. The video menu also exposes it as an Aspect choice.

## Build

Compile SDL3 first, then build the frontend:

    make -f Makefile.sdl3 -j8 CHD=NO SDL3_CFLAGS='-I/path/to/SDL3/include' SDL3_LIBS='-L/path/to/lib -lSDL3'
    ./pcfx-sdl3-ui --bios-dir /path/to/bios-directory --pair-locked game.cue

The existing bob helper can be run with sh tests/run_interlace_bob_test.sh.
No BIOS or game data is included.
