# PC-FXGA / HuC6273 first-pass renderer

This tree includes a first-stage PC-FXGA HuC6273/Aurora implementation for FARL/GMAKER titles, plus the headless build/API work used for validation.

The implementation is not cycle-accurate and not a complete Aurora renderer. It implements enough of the command/register path used by Same Game FX to pass hardware probing, run the command stream, play audio, and display the title/menu/gameplay paths with approximate 3D output.

Implemented HuC6273 areas include FIFO/CMT/readback/status/config behavior, PE/TE register writes and reads, 32 texture banks, 9-bit I/C texture conversion through the live VCE palette, fixed-point matrix command/copy paths used by FARL, put-image/fill paths, textured triangle list/strip rasterization, and overlay into the PC-FX video output.

BIOS and game images are intentionally not included. Place `pcfx.rom` in the selected `--bios-dir` and run PC-FX/PC-FXGA CUE/CHD media normally.


## Shading pass

This revision adds FARL/HuC6273 lighting support for the primitive formats exercised by Same Game FX. The renderer now parses vertex-normal and facet-normal triangle list/strip variants, transforms normals through the TE normal matrix, evaluates the two TE light vectors with the ambient/diffuse material registers, and applies the result to the I component of I/C colors and textures before VCE palette lookup. This keeps the implementation hardware-facing rather than using a final-frame darkening filter.

The lighting model is still approximate. It is deliberately biased toward the source texture intensity after comparing against S-Video capture, because directly multiplying Same Game's mid-intensity I/C texture words by the raw Lambert term produced cubes that were much darker than real hardware.

## Bully Off and GMAKER path

The HuC6273 path now keeps FARL's packed/CMT memory separate from the unpacked 9-bit texture plane, including the `0x80530000` write window. Matrix operations use the documented fixed-point formats and source/destination order; projected vertices retain clip-space values for perspective division and logical-region clipping. HuC6273 FIFO, TE, PE, sprite, and swap completion events advance from V810 timestamps.

Bully Off needs a few narrowly selected GMAKER paths while the general FARL projection remains active: the `0x14xx` high-intensity title glyphs use their title projection; bank-0 `0x0801` floor strips use the floor path; and `0x0801` playfield geometry uses a separate compact projection and depth handling for the car meshes. I5C4 material lighting preserves the palette hue. These paths improve the match to PC-FXGA output but remain an approximation of Aurora hardware.
