# PCFXemu For OpenDingux/GKD350H

This is a fork of the libretro fork of Mednafen to run NEC PC-FX games on handhelds.
On the GKD350H, the games are very playable. On the RG-350, it's recommended to overclock to 1.2 Ghz (but even then, it's not enough to be playable).
There's also a version for Funkey-S and it's more playable than RG-350 version but still slower than GKD350H/GKD Mini.

Changes done
- Switched from OwlResampler to Blip_buffer
- Made changes to improve performance on sinlge core platforms.
- Stripped down code that's not needed
- Added SDL 1.2 backend with basic menu system
