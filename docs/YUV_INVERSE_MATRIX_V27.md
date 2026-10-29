# HuC6261 analog YUV output conversion

KING video is composed in YUV before the final display conversion. The shared
lookup table now converts that output with the inverse of the conventional
three-decimal RGB-to-analog-YUV matrix:

```text
Y =  0.299 R + 0.587 G + 0.114 B
U = -0.147 R - 0.289 G + 0.436 B
V =  0.615 R - 0.515 G - 0.100 B
```

For unsigned 8-bit chroma values `ur` and `vr`, the lookup table centers them
at 128 and calculates these RGB offsets:

```c
u = (int)ur - 128;
v = (int)vr - 128;
r = (int)lrint(0 - 0.000039457070707 * u + 1.139827967171717 * v);
g = (int)lrint(0 - 0.394610164141414 * u - 0.580500315656566 * v);
b = (int)lrint(0 + 2.031999684343434 * u - 0.000481376262626 * v);
```

The final RGB components are the luma value plus these offsets, clamped to
8-bit range. `lrint()` follows the active floating-point rounding mode; with
`FE_TONEAREST`, halfway values round to even. The offsets are calculated while
building the 65,536-entry chroma table, not for each output pixel.

Palette entries store YUV844. Their U and V nibbles are expanded to 8-bit
values by shifting them left four bits, with the low bits set to zero. The
same final conversion is used for KING, RAINBOW, and palette-based planes.

This is a consistent display approximation based on the stated analog matrix.
The small U-to-R and V-to-B terms result from rounding the forward matrix
coefficients; they are not measured PC-FX encoder crosstalk. Accurate board
and CRT colorimetry still requires measurements from real hardware.
