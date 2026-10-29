# HuC6261 v3 runtime regression tests

Both builds can run `tests/run_dotwidth_test.sh` and
`tests/run_interlace_bob_test.sh` without a BIOS.

For the BIOS + sample homebrew test, keep an original PC-FX BIOS and the
`tetsu_hires_480i.cue/.bin` files available. Boot to the still image,
then create a save state with `--state-out`. From a save state displaying
that image, run these three examples (substitute absolute paths):

```sh
./pcfx-headless --bios-dir /BIOS/pcfx.rom --state-in /tmp/still.state \
 --frames 6 --horizontal clock --analog off --screenshot baseline.png still.cue
./pcfx-headless --bios-dir /BIOS/pcfx.rom --state-in /tmp/still.state \
 --frames 6 --horizontal clock --analog component --screenshot component.png still.cue
./pcfx-headless --bios-dir /BIOS/pcfx.rom --state-in /tmp/still.state \
 --frames 6 --horizontal clock --vdc-phase 0 --screenshot phase0.png still.cue
```

Mode 3 can be triggered by an input command file containing:

```
0 B
3 NONE
```

Then run with `--commands filename` and compare to baseline. The homebrew
uses pad II (= B) to select mode 3; a complete weave should change only
alternate rows. Hardware output shift magnitude/phase remains uncalibrated.

The test BIOS is user-supplied and is never bundled with the release.
