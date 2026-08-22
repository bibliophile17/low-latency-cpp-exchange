# Performance

## Important: numbers below are real, but machine-specific

All numbers in this document were produced by actually building and
running `exchange_benchmarks` and `stress_test` on the development
machine used to write this project -- **not fabricated or estimated**.
That machine is a single-vCPU (1 logical CPU) cloud sandbox VM at
2.1 GHz with a 2 MB L2 / ~260 MB L3 cache, running Ubuntu 24.04, GCC
13.3.0, `-O3` Release build. This is **not** a representative
low-latency trading server (real systems use dedicated multi-core
hardware, pinned threads, isolated cores, tuned kernel settings, etc.),
and the single-CPU environment in particular makes any
multi-*thread* result (the SPSC producer/consumer benchmark) a test of
correctness more than of realistic parallel throughput, since both
threads are time-sliced onto one core.

**Re-run these yourself** on your own hardware before citing any
number in an interview:

```bash
./scripts/build.sh release
./build/exchange_benchmarks --benchmark_out=results.json --benchmark_out_format=json
python3 scripts/plot_benchmarks.py results.json --out benchmark_report.png
```

The raw JSON from the run that produced the numbers below is committed
at `benchmarks/results/sample_run.json` for reference/reproducibility
of the methodology (not as a performance claim about your machine).

## Measured results (single run, see caveats above)

Environment: 1 CPU @ 2100 MHz, GCC 13.3.0, `-O3`, Google Benchmark
v1.9.1, single run (not averaged across multiple runs -- Google
Benchmark's default is one iteration count auto-tuned for statistical
stability per benchmark, but run-to-run variance was not separately
characterized here; see "Future improvements" below).

| Benchmark | Result |
|---|---|
| `BM_OrderBookInsert` | 41.0 ns/op |
| `BM_OrderBookCancel` | 2.0 ns/op |
| `BM_OrderBookBestPriceLookup` | 1.1 ns/op |
| `BM_OrderBookModifyReduceQuantity` | 5.4 ns/op |
| `BM_MatchingEngine_LimitOrderInsertNoMatch` | 96.6 ns/op |
| `BM_MatchingEngine_FullyCrossingOrders` | 149.6 ns/op (pair: rest + cross) |
| `BM_MatchingEngine_MarketOrders` | 138.5 ns/op (pair: rest + market IOC) |
| `BM_MatchingEngine_EndToEndSyntheticFlow/1000` | 1.89 ms (approx 533K ops/sec) |
| `BM_MatchingEngine_EndToEndSyntheticFlow/10000` | 4.12 ms (approx 2.43M ops/sec) |
| `BM_MatchingEngine_EndToEndSyntheticFlow/100000` | 23.1 ms (approx 4.34M ops/sec) |
| `BM_SpscQueue_PushPopSingleThread` | 1.6 ns/op |
| `BM_SpscQueue_ProducerConsumerThroughput/10000` | 0.069 ms (approx 537M items/sec -- see single-CPU caveat) |
| `BM_SpscQueue_ProducerConsumerThroughput/100000` | 7.98 ms (approx 4.16B items/sec -- see single-CPU caveat, likely dominated by scheduling noise at this size) |

Interpretation notes:

- `BM_OrderBookCancel`'s 2.0 ns is suspiciously fast for a hash-map
  lookup + linked-list unlink; this is very likely an artifact of the
  benchmark's access pattern (repeatedly cancelling from a
  pre-populated, cache-resident 100K-order book with predictable
  access) rather than a number to trust as "cancel is free." Re-run
  with `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`
  for more statistically robust numbers, and treat single-run
  nanosecond-scale numbers on a shared/virtualized CPU with healthy
  skepticism -- see "Known limitations" below.
- The SPSC throughput numbers on a single-core VM mostly measure how
  fast the OS scheduler can interleave two spinning threads on one
  core, not true concurrent throughput; on real multi-core hardware
  expect very different (and more meaningful) numbers. Re-run on
  multi-core hardware for numbers worth quoting.
- End-to-end synthetic-flow throughput (4.3M ops/sec at 100K orders)
  is the most representative single number for "how fast can this
  engine process realistic mixed order flow" on this machine, since it
  exercises validation, matching, book maintenance, and event emission
  together, the way production traffic would.

## Distinguishing latency types

This project's `metrics::LatencyRecorder` (`include/exchange/metrics/latency_recorder.hpp`)
supports separately timing:

- **Processing latency**: time inside `MatchingEngine::process()` for a
  single command (what the `BM_MatchingEngine_*` micro-benchmarks
  measure).
- **Queue latency**: time a `Command` spends sitting in the
  `SpscQueue` between being pushed by the gateway and popped by the
  matching thread. Not currently instrumented end-to-end in the CLI
  server (the server loop pops immediately when available), but the
  `ScopedTimer`/`LatencyRecorder` utilities are ready to be wired in at
  the push/pop boundary for a live deployment.
- **Network latency**: time between a client sending bytes and the
  gateway thread's `recv()` returning them. Not measured in-repo (would
  require client-side clock synchronization or a loopback-only
  measurement); out of scope for this project's benchmarks, which
  focus on the matching core.
- **End-to-end latency**: sum of the above; approximated by
  `BM_MatchingEngine_EndToEndSyntheticFlow`, which excludes network
  latency (it drives the engine in-process) but includes realistic
  mixed-command processing cost.

No latency numbers anywhere in this repository were fabricated or
back-calculated from a "target" figure -- every number quoted was
produced by an actual instrumented run on real hardware, and the
methodology to reproduce each one is given above.

## perf: CPU profiling and call graphs

```bash
./scripts/build.sh debug          # need frame pointers / debug info for good symbols
./scripts/profile.sh perf-record --orders 200000 --seed 1 --instruments 3
```

This runs `stress_test` under `perf record -g` and prints a `perf
report --stdio` summary. `perf_event_paranoid` may need to be lowered
(`sudo sysctl kernel.perf_event_paranoid=1`) for `perf` to access
hardware counters without root, depending on your distribution's
default.

For hardware counters (cache misses, branch mispredicts, instructions
per cycle) instead of a call-graph profile:

```bash
./scripts/profile.sh perf-stat --orders 200000 --seed 1 --instruments 3
```

What to look for: high `cache-misses` relative to `cache-references`
during the matching-heavy portion of a stress run would point at the
`std::unordered_map<OrderId, OrderLocation>` lookup (the one hash map
on the hot path, see `docs/order-book.md`) as a candidate for further
investigation -- e.g. a custom open-addressing map tuned for this
project's key distribution, or reducing map lookups by having
`Command`s carry a resolved pool index when available.

## gdb: debugging

```bash
./scripts/build.sh debug
./scripts/profile.sh gdb --orders 1000 --seed 1
```

Useful commands inside gdb for this project: `break
MatchingEngine::process`, `break OrderBook::remove_order`, `print
*this` inside a `PriceLevel`-touching function, `bt` after any crash
inside a Debug build.

## Valgrind: memory analysis

```bash
./scripts/build.sh debug
./scripts/profile.sh valgrind-memcheck --orders 2000 --seed 1   # small workload -- memcheck is ~20-50x slower than native
./scripts/profile.sh valgrind-cachegrind --orders 20000 --seed 1
```

`memcheck` catches use-after-free, uninitialized reads, and leaks;
`cachegrind` simulates the cache hierarchy and reports per-function
miss rates (via `cg_annotate cachegrind.out.<pid>`), useful for
validating the "cache-conscious" claims made about the `ObjectPool`
and intrusive-list design in `docs/order-book.md`.

## AddressSanitizer / UndefinedBehaviorSanitizer

```bash
./scripts/build.sh asan
./scripts/test.sh asan
```

All 75 tests (72 unit tests + 3 fuzz/property tests, see
`docs/testing.md`) and a 100K-order `stress_test` run pass cleanly
under `-fsanitize=address,undefined` on this machine -- no reported
leaks, out-of-bounds accesses, use-after-free, or undefined behavior.
Re-run locally to confirm on your own toolchain/platform, since
sanitizer behavior can vary slightly by compiler version.

ThreadSanitizer (`./scripts/build.sh tsan`) is also wired up via
`EXCHANGE_ENABLE_TSAN` for validating the SPSC queue's concurrent
correctness claims (`docs/concurrency.md`) under a happens-before
checker rather than by inspection alone.

## Where allocations happen (and don't)

- **Do not allocate** during `MatchingEngine::process()`'s matching
  logic: `Order` objects come from a pre-sized `ObjectPool` (one
  `std::vector` allocated once at `OrderBook` construction), the FIFO
  list is intrusive (no list-node allocation), and `PriceLevel` structs
  live inside the `std::map` node (one allocation per *new distinct
  price level*, not per order -- see below).
- **Do allocate, but only per new price level, not per order**:
  `std::map::try_emplace` allocates one tree node when a price level is
  first created (`OrderBook::level_for`). Since L (distinct price
  levels) is much smaller than the number of orders in any realistic
  workload, this is amortized to a small fraction of total operations.
- **Do allocate**: `std::unordered_map<OrderId, OrderLocation>` may
  allocate on rehash; capacity is reserved up front
  (`locations_.reserve(expected_orders)`) to avoid this during normal
  operation, but exceeding the reserved capacity will trigger a rehash
  allocation.
- **Do allocate, off the hot path**: gateway per-connection `std::string`
  read buffer, logging's `fprintf`, and the replay writer's file I/O
  buffering -- none of these are in the matching engine's call path.

## Known limitations of this benchmark suite

- Single machine, single run per number (no statistical repetition
  across multiple independent runs in the committed sample). For a
  publication-quality result, re-run with
  `--benchmark_repetitions=N --benchmark_report_aggregates_only=true`
  and quote mean plus/minus stddev.
- Single-vCPU sandbox: multi-threaded numbers (SPSC throughput) are not
  representative of real multi-core hardware; re-run there.
- No CPU pinning / isolated cores / disabled frequency scaling was
  configured for this run (`cpu_scaling_enabled: false` in the captured
  JSON context reflects what Google Benchmark detected on this VM, not
  a deliberately tuned benchmark environment).
