# PCFXemu v17 — RAINBOW three-word read-ahead experiment

The previous v16 unified K-BUS scheduler remains the **only** CPU/KRAM timing model. No old legacy timing modes have been restored.

**v17 change:** a conservative model of the *documented* HuC6272 three-word read-ahead across HuC6271 16-line block boundaries. Requests compete for the KRAM bank and capture the word at the selected K-BUS grant. Prefetched words survive later KRAM writes. These new data and deadlines are save-stated. SDL3 and headless use the same core.

**Do not call this fully deferred RAINBOW decoding.** The entropy/RLE parser continues to access most data synchronously. See `docs/RAINBOW_PREFETCH_V17.md` for both the improvement and unimplemented stages.

Test with `PCFX_RAINBOW_STATS=1` and your own copy of `Makeruna! Makendou Z (Japan).chd`. The image and BIOS are not distributed. v16 and v17 2400-frame images match at 1024x480. The main 480i still and tank 300-frame tests are also byte-identical. Existing and new unit tests pass.

Existing SCSI pending-queue exhaustion on the Makendou game remains a separate timing/handshake defect, as does the HuC6271's synchronous input, and both are documented in `docs/HARDWARE_TESTS_REQUIRED.md`.
