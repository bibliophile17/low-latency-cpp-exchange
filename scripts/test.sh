#!/usr/bin/env bash
# Runs the unit test suite (Release build by default) plus a quick
# stress-test smoke pass. Use scripts/build.sh asan first and pass
# `asan` here to run the same tests under sanitizers.
#
# Usage:
#   scripts/test.sh            # run tests from build/
#   scripts/test.sh asan       # run tests from build-asan/
set -euo pipefail
cd "$(dirname "$0")/.."

MODE="${1:-release}"
case "$MODE" in
    release) BUILD_DIR="build" ;;
    debug)   BUILD_DIR="build-debug" ;;
    asan)    BUILD_DIR="build-asan" ;;
    tsan)    BUILD_DIR="build-tsan" ;;
    *) echo "Unknown mode: $MODE" >&2; exit 1 ;;
esac

if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory $BUILD_DIR not found; run scripts/build.sh $MODE first." >&2
    exit 1
fi

echo "=== Running GoogleTest suite ($BUILD_DIR) ==="
"$BUILD_DIR/exchange_tests"

echo ""
echo "=== Running quick stress-test smoke pass ==="
"$BUILD_DIR/stress_test" --orders 50000 --seed 1 --instruments 3
