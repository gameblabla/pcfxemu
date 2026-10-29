# PCFXemu v13 specification-conformance delta

See the earlier SPEC_CONFORMANCE_V* files for the full historical audit.

| Item | Status | Basis |
|---|---|---|
| KING KRAMWA page versus word-offset bits | Corrected in slot, legacy and profiler | HuC6272 §1.3, R0D register description |
| KRAM mode force-page-zero | Preserved | HuC6272 §1.3 |
| CPU posted-write address/data captured at acceptance | Opt-in modeling improvement | Hardware-observer description; absolute timings uncertain |
| CPU posted-write data visible only at drain | Implemented for opt-in R0E writes | Model assumption; physical phase requires testing |
| Split graphics timeline at modeled retirement | Implemented | Improves causal order in emulator; other engines approximate |
| Pending write save/load and frame timestamp rebase | Tested | Internal correctness |
| Exact per-bank bus grants and refresh | Unresolved | HuC6272 priority documented, individual service timing not complete |
| Real hardware 18–19 FPS tank reproduction | Still unresolved | v13 slot model reports 20.489 FPS |
| 1024-position TETSU compositing / 480i | Preserved | No changes to video presentation code |

Default timing remains legacy; the improved posted-data visibility runs only with `PCFX_KRAM_TIMING=slot`.
