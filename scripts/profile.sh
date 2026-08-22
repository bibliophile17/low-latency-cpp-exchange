#!/usr/bin/env bash
# Convenience wrappers around perf / valgrind for profiling the stress
# test or benchmark binaries. See docs/performance.md for how to
# interpret the output of each of these.
#
# Usage:
#   scripts/profile.sh perf-record [args...]      # perf record + report, CPU profiling
#   scripts/profile.sh perf-stat [args...]        # perf stat, cache/branch counters
#   scripts/profile.sh valgrind-memcheck [args...] # Valgrind memcheck (slow; use small workload)
#   scripts/profile.sh valgrind-cachegrind [args...] # Valgrind cachegrind, cache simulation
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="build-debug"
TARGET="$BUILD_DIR/stress_test"

if [ ! -x "$TARGET" ]; then
    echo "Debug build not found at $TARGET; run scripts/build.sh debug first." >&2
    exit 1
fi

CMD="${1:-}"
shift || true
ARGS=("$@")
if [ ${#ARGS[@]} -eq 0 ]; then
    ARGS=(--orders 100000 --seed 1 --instruments 3)
fi

case "$CMD" in
    perf-record)
        echo "Recording CPU profile with call graphs (requires perf; may need root/perf_event_paranoid tuning)..."
        perf record -g -o perf.data -- "$TARGET" "${ARGS[@]}"
        echo "Report:"
        perf report -i perf.data --stdio | head -60
        echo ""
        echo "For an interactive view: perf report -i perf.data"
        ;;
    perf-stat)
        echo "Collecting hardware counters (cache misses, branch mispredicts, IPC)..."
        perf stat -e cycles,instructions,cache-references,cache-misses,branch-instructions,branch-misses \
            -- "$TARGET" "${ARGS[@]}"
        ;;
    valgrind-memcheck)
        echo "Running under Valgrind memcheck (this will be MUCH slower than native; use a small workload)..."
        valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes \
            -- "$TARGET" "${ARGS[@]}"
        ;;
    valgrind-cachegrind)
        echo "Running under Valgrind cachegrind (cache simulation; also slow)..."
        valgrind --tool=cachegrind -- "$TARGET" "${ARGS[@]}"
        echo "Use 'cg_annotate cachegrind.out.<pid>' to see per-function cache miss rates."
        ;;
    gdb)
        echo "Launching under gdb..."
        gdb --args "$TARGET" "${ARGS[@]}"
        ;;
    *)
        echo "Usage: $0 {perf-record|perf-stat|valgrind-memcheck|valgrind-cachegrind|gdb} [args...]" >&2
        exit 1
        ;;
esac
