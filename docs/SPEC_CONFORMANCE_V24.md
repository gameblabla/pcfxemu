# v24 specification conformance — color reconstruction

| Feature | Status | Source |
|---|---|---|
| 8-bit linear Y channel with 0..255 input | Implemented | HuC6261 §2.8 |
| U/V neutral at 0x80 and signed chroma difference | Implemented | HuC6261 §2.8 |
| YUV844 palette conversion appends low zero nibbles | Implemented and unit-tested | HuC6261 §2.2, §2.8 |
| One final color conversion for VDC/KING/RAINBOW | Implemented | HuC6261 §2.1, §2.8 |
| Conventional analog-YUV inverse transform | Implemented as approximation | Standard YUV, **not** a motherboard-measured matrix |
| Accurate RAINBOW chroma IDCT/output parity | Pending | v23 comparator validates only Y |
| Exact DAC/encoder S-Video/component transfer function | Pending measurement | HuC6261 documents DAC signal but not downstream board response |
| 1024-clock video, 480i, K-BUS, SCSI, ADPCM | Inherited from v23 without timing changes | Earlier `docs/SPEC_CONFORMANCE_V23.md` |

See `TETSU_COLOR_CONVERSION_V24.md` and `HARDWARE_TESTS_REQUIRED.md`.
