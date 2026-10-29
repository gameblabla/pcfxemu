#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Ishell/video/headless tests/dotwidth_test.c -o /tmp/pcfx-dotwidth-test
/tmp/pcfx-dotwidth-test
