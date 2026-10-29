#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tests/run_v14_regression.sh
tests/run_king_v15_bus_priority_test.sh
