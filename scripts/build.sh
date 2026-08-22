#!/usr/bin/env bash
# Configures and builds the project in Release mode by default.
#
# Usage:
#   scripts/build.sh                 # Release build, tests+benchmarks on
#   scripts/build.sh debug           # Debug build
#   scripts/build.sh asan            # Debug build with ASan+UBSan
#   scripts/build.sh tsan            # Debug build with ThreadSanitizer
set -euo pipefail
cd "$(dirname "$0")/.."

MODE="${1:-release}"
BUILD_DIR="build"
CMAKE_ARGS=()

case "$MODE" in
    release)
        CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE=Release)
        ;;
    debug)
        BUILD_DIR="build-debug"
        CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE=Debug)
        ;;
    asan)
        BUILD_DIR="build-asan"
        CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE=Debug -DEXCHANGE_ENABLE_ASAN=ON -DEXCHANGE_BUILD_BENCHMARKS=OFF)
        ;;
    tsan)
        BUILD_DIR="build-tsan"
        CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE=Debug -DEXCHANGE_ENABLE_TSAN=ON -DEXCHANGE_BUILD_BENCHMARKS=OFF)
        ;;
    *)
        echo "Unknown mode: $MODE (expected release|debug|asan|tsan)" >&2
        exit 1
        ;;
esac

mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "Build complete: $BUILD_DIR/"
echo "  ./$BUILD_DIR/exchange              - server / replay / record CLI"
echo "  ./$BUILD_DIR/exchange_client       - example TCP text-protocol client"
echo "  ./$BUILD_DIR/market_generator      - synthetic order-flow generator"
echo "  ./$BUILD_DIR/stress_test           - randomized stress test with invariant checks"
echo "  ./$BUILD_DIR/exchange_tests        - GoogleTest unit test suite"
echo "  ./$BUILD_DIR/exchange_benchmarks   - Google Benchmark suite"
