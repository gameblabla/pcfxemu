# PCFXemu v8 — targeted tank CPU/KRAM diagnosis

New diagnostic: `make -f Makefile.headless PROFILE=1` prints an additional KING bus-accounting section at exit. The section distinguishes modeled posted-latch waits and modeled background-fetch arbitration, with bank A/B mapping and fetch-slot occupancy, and leaves guest timing unchanged.

CPU correctness fix: V810 pipeline flag-writer history is included in new save states (`FlagHistoryV8`), with 0 fallback on pre-v8 states. No arbitrary timing slowdown was added.

See `docs/TANK_PIPELINE_KBUS_V8.md` and `docs/HARDWARE_TESTS_REQUIRED.md`. The official HuC6272 specification is the authority for priority and request behavior; exact CPU/KRAM cycle timing remains a hardware-test target.
