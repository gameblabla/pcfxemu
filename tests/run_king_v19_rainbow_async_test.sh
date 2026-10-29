#!/bin/sh
# v20 supersedes the v19 block-atomic RLE test; the old test intentionally
# expected no RLE emission until all dummy and guard words were fetched.
# Keep the historical entry point available for older regression scripts.
set -eu
exec "$(dirname "$0")/run_king_v20_rainbow_incremental_test.sh" "$@"
