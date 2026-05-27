# PCFXemu by Gameblabla

This is an improved fork of Mednafen, aiming for accuracy and testing,
mostly aimed for helping me develop my own homebrew games for the PC-FX.

Features :
- World's first PC-FXGA emulator (for the 2 games + homebrew games that can leverage the hardware)
- Entirely C-based
- Webassembly version that does not leverage Emscripten (no annoying startup time)
- Some accuracy improvements for homebrew devs and commercial games (it no longer runs twice as fast as real hardware !)
- Has a headless mode and can be ported to other devices

# TODO

- Redo/remove some of the provided tests for headless mode, some are now broken
- Pipeline improvements. David Shadoff made early findings on his PC-FX worth implementing.
- Allowing cartridge boot of binary executables (as used for PCFXUploader for instance and related), namely the PCFXBoot images.
- MCP Server for AI tools, like GearSystem. There's a headless mode that can do some things but not as flexible
- A proper Qt-like UI, for those that neither like the Web or the basic SDL3 ui.