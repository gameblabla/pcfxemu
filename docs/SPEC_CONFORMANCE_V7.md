# PCFXemu v7 conformance update

Compared with v6, emulated HuC6261, HuC6270, HuC6272, HuC6230 and V810 timing/behavior is unchanged. The existing implementation status is in `SPEC_CONFORMANCE_V6.md`.

New capability: headless *observation* of video changes once per emulated output frame, using an optional region checksum. This does not become a ground truth for completed software-rendered frames or NTSC DAC timing. `--frame-csv` is diagnostic and intentionally incurs a little host-side work only when enabled.

New unresolved conformance target: tank demo reproduces around 33–34 on-screen FPS when user expects 18–19 on real hardware. The comparison is **not hardware-verified**. The V810 renderer is heavily dominated by KING KRAM writes, making KING arbitration and CPU write buffering prime things to probe, not proven bugs.

Specific hardware test recommendations: `HARDWARE_TESTS_REQUIRED.md` PERF-01/02; reference measurements and methods: `TANK_DEMO_TIMING_INVESTIGATION.md`.
