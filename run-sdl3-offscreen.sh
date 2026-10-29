#!/bin/sh
# Run the SDL3 frontend with the offscreen video and dummy audio drivers.
# Build pcfx-sdl3-ui first. A packaged runtime SDL3 library is optional.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-offscreen}"
SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
if [ -d "$HERE/runtime_libs" ]; then
    LD_LIBRARY_PATH="$HERE/runtime_libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    SDL_VIDEODRIVER="$SDL_VIDEODRIVER" SDL_AUDIODRIVER="$SDL_AUDIODRIVER" \
    exec "$HERE/pcfx-sdl3-ui" "$@"
fi
SDL_VIDEODRIVER="$SDL_VIDEODRIVER" SDL_AUDIODRIVER="$SDL_AUDIODRIVER" \
exec "$HERE/pcfx-sdl3-ui" "$@"
