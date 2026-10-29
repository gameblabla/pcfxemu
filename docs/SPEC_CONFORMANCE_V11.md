# v11 spec-conformance delta

| Behavior | Status | Authority | Notes |
|---|---|---|---|
| V810 same-width repeated ST/OUT minimum timing | Corrected | V810 Family Architecture UM, Table 5-11 note 3 | Width-specific history, no arbitrarily added waits; old save histories degrade safely. |
| KING per-bank microprogram sequence | Observed in profile | HuC6272 Part 2, §3.6.7 | 8 slots/bank relative to emulator active-window origin. |
| KING active/HBlank bus priorities | Existing approximation | HuC6272 Part 2 §§3.2.2–3.2.3 | No new unverified service timing in v11. |
| CPU posted-write retirement, phase/grant mapping | **Requires hardware testing** | HuC6272 Part 2 §3.1.2 | Described in docs/HARDWARE_TESTS_REQUIRED.md PERF-08. |
| 480i / 1024-clock video output | Preserved | HuC6261 timing | Regression capture pixel-identical. |
| Tank 18–19 FPS on real hardware | Still NOT reproduced | User-supplied reference | v11's RAM timer+frame counter reports 33.817 FPS. |

No BIOS, tank demo, or copyrighted console ROM is included in the release archive.
