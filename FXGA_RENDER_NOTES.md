# PC-FXGA / HuC6273 first-pass renderer

This tree includes a first-stage PC-FXGA HuC6273/Aurora implementation for FARL/GMAKER titles, plus the headless build/API work used for validation.

The implementation is not cycle-accurate and not a complete Aurora renderer. It implements enough of the command/register path used by Same Game FX to pass hardware probing, run the command stream, play audio, and display the title/menu/gameplay paths with approximate 3D output.

Implemented HuC6273 areas include FIFO/CMT/readback/status/config behavior, PE/TE register writes and reads, 32 texture banks, 9-bit I/C texture conversion through the live VCE palette, fixed-point matrix command/copy paths used by FARL, put-image/fill paths, textured triangle list/strip rasterization, and overlay into the PC-FX video output.

BIOS and game images are intentionally not included. Place `pcfx.rom` in the selected `--bios-dir` and run PC-FX/PC-FXGA CUE/CHD media normally.


## Shading pass

This revision adds FARL/HuC6273 lighting support for the primitive formats exercised by Same Game FX. The renderer now parses vertex-normal and facet-normal triangle list/strip variants, transforms normals through the TE normal matrix, evaluates the two TE light vectors with the ambient/diffuse material registers, and applies the result to the I component of I/C colors and textures before VCE palette lookup. This keeps the implementation hardware-facing rather than using a final-frame darkening filter.

The lighting model is still approximate. It is deliberately biased toward the source texture intensity after comparing against S-Video capture, because directly multiplying Same Game's mid-intensity I/C texture words by the raw Lambert term produced cubes that were much darker than real hardware.
