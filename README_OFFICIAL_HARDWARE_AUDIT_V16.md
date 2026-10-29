# PCFXemu v16 — deferred K-BUS memory data transfers

Continuing from v15, the single posted-write, per-bank slot scheduler is retained. No legacy timing path is reintroduced. The HuC6272 SCSI real-DMA halfword now commits to KRAM at the modeled grant deadline, not at SCSI byte reception. The HuC6230 ADPCM input latch now captures the KRAM word after its modeled K-BUS grant rather than when the -HSYNC request was made. Same-timestamp priorities and reservation displacement can move those completion deadlines. In-flight state is serialized for save states.

**RAINBOW is not yet fully deferred.** Its compressed-byte source remains synchronous in both decoder backends and needs a resumable JPEG input pipeline. The exact four-clock grant quantum, CPU WAIT phase, refresh placement, and SCSI IRQ/readback sequencing are also uncalibrated. The document `docs/DEFERRED_KBUS_V16.md` specifies precisely what changed and why; `docs/HARDWARE_TESTS_REQUIRED.md` lists targeted physical measurements.

The headless and SDL3 frontends share this core. `tests/run_v16_regression.sh` runs focused v16 tests plus the preceding suite. The BIOS and proprietary user-uploaded demo are not redistributed. All results from the PC-FX console itself remain unverified; a physical test is essential before claiming a cycle-exact scheduler.

Cold-boot smoke test: with the supplied BIOS and original tank disc, 1550 emulated video periods reach the 3D gameplay scene. The image at field 1550 shows the game FPS counter at 21; this is an instantaneous HUD number, not the 300-period guest-counter measurement.
