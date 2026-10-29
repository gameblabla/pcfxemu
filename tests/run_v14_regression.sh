#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tests/run_v13_regression.sh
