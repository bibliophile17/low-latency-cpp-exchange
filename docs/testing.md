# Testing

## Test suites

| Binary/target | Purpose | Location |
|---|---|---|
| `exchange_tests` | GoogleTest unit + invariant + fuzz/property tests | `tests/` |
| `stress_test` | Large randomized workload with runtime invariant checking | `tools/stress_test_main.cpp` |
| `exchange_benchmarks` | Google Benchmark micro/macro benchmarks | `benchmarks/` |

## Unit tests (`exchange_tests`)

75 tests across 9 test suites, all passing on this machine (Release
build, GCC 13.3.0) and again under `-fsanitize=address,undefined`
(see `docs/performance.md`):

```
PriceTest            (4)   - decimal parse/format round-trip, exact tick comparison, invalid input
QuantityTest         (1)   - arithmetic
SideTest              (1)   - opposite()
ToStringTest          (1)   - enum-to-string coverage
OrderBookTest        (13)  - empty book, single/multiple orders, FIFO, cancel,
                              modify, partial/full fill, depth ordering,
                              multi-instrument isolation
MatchingEngineTest   (19)  - matching against an empty book, full/partial/
                              multi-level fills, price-time priority,
                              better-price-wins, market orders, IOC/FOK,
                              cancel, modify, invalid orders, duplicate IDs,
                              unknown instrument/order, large quantities,
                              multi-instrument isolation
SpscQueueTest         (5)   - push/pop, empty pop, fill-to-capacity, FIFO
                              order, and a genuine two-thread concurrent
                              producer/consumer run (100K items)
TextProtocolTest     (13)  - grammar coverage for NEW/CANCEL/MODIFY,
                              malformed input rejection (bad side, bad
                              quantity, bad price, oversized line, unknown
                              verb, empty line), format round-trip
BinaryProtocolTest    (7)   - encode/decode round-trip for all 3 message
                              types, truncated-buffer rejection, empty-buffer
                              rejection, unknown message type rejection,
                              invalid enum byte rejection
ReplayTest             (4)   - record-then-read-back round trip, synthetic
                              generator determinism (same seed -> identical
                              output), different seeds -> different output,
                              full record->replay->matching-engine
                              determinism (same trade sequence twice)
InvariantTest          (4)   - book never crossed, price-level cached
                              quantity matches sum of resting orders,
                              double-cancel is rejected (not silently
                              accepted), no quantity corruption across a
                              randomized run
FuzzTest               (3)   - see "Fuzz/property testing" below
```

Run with:
```bash
./scripts/build.sh release
./scripts/test.sh
# or directly:
./build/exchange_tests
./build/exchange_tests --gtest_filter='MatchingEngineTest.*'   # filter to one suite
```

## Invariant testing philosophy

Rather than only asserting specific expected outputs, several tests
(`InvariantTest`, and more extensively `tools/stress_test_main.cpp`)
assert *properties that must hold regardless of the specific random
input*:

- **Orders cannot execute more quantity than available.** Implicitly
  enforced by `OrderBook::fill_best`, which only ever reduces
  `remaining_quantity` by `min(incoming.remaining, resting.remaining)`
  -- verified by `MatchingEngineTest.PartialFillLeavesRemainderResting`
  and friends never observing negative-going quantity.
- **Cancelled orders cannot execute.** `OrderBook::remove_order` fully
  unlinks and releases the order's pool slot; a second cancel of the
  same ID is rejected (`InvariantTest.CancelledOrderCannotBeCancelledAgain`)
  because `locations_.find` no longer finds it -- there is no code path
  by which a released pool slot's `OrderId` remains reachable via a
  cancel/modify command.
- **Order quantities cannot become negative.** `core::Quantity` wraps
  `uint64_t`; `InvariantTest.QuantityNeverNegativeAcrossRandomFlow`
  checks aggregated level quantities stay within a sane bound after a
  randomized run, which would catch an unsigned-underflow wraparound
  bug (the failure mode that would occur if a fill or modify ever
  subtracted more than was available).
- **Book state remains internally consistent.** `PriceLevel::total_quantity`
  is maintained incrementally on every insert/erase/fill; `InvariantTest.PriceLevelTotalQuantityMatchesSumOfOrders`
  and the analogous check in `stress_test`'s `check_invariants()`
  confirm every level with `order_count > 0` also has non-zero cached
  quantity (a level whose cache had drifted from the true sum of its
  orders would fail this).
- **Matching respects price-time priority.** `MatchingEngineTest.PriceTimePriorityFifoAtSamePrice`
  and `MatchingEngineTest.BetterPriceMatchesFirst` directly assert
  which resting order gets matched first under each rule.
- **Book is never crossed.** `InvariantTest.BookNeverCrossedAfterRandomFlow`
  and `stress_test`'s `check_invariants()` assert `best_bid < best_ask`
  after every command in a randomized run -- a crossed book would mean
  the matching engine failed to match something that should have
  traded.

## Stress testing (`stress_test`)

```bash
./build/stress_test --orders 500000 --seed 7 --instruments 5
```

Drives a large seeded-random `NEW`/`CANCEL`/`MODIFY` stream (via
`tools::SyntheticOrderGenerator`, see `docs/architecture.md`) through a
real `MatchingEngine`, sampling the same invariant checks used in
`InvariantTest` every 1000 commands (plus always on the final command)
across every active instrument's book. It reports throughput and exits
non-zero (printing every violation found, capped at 20) if any
invariant is violated -- **it is a correctness check that happens to
also report speed, not a speed benchmark that happens to check
correctness**. A run that is fast but finds a violation is a failing
run.

Verified on this machine: 200,000 commands across 3 instruments processed
with zero invariant violations (see `docs/performance.md` for the
measured throughput number and machine caveats).

Traffic characteristics exercised: bursty traffic (the generator
clusters commands via `burst_probability`/`burst_size`), high
cancellation rates (`cancel_fraction`), high matching rates (tight
`price_spread_ticks` band), large order books (accumulated resting
orders across a long run), and multiple instruments simultaneously.

## Fuzz/property testing (`FuzzTest.*`, in `tests/test_fuzz_parsers.cpp`)

Both untrusted-input parsers (`parse_text_command`, `decode_binary`)
are exercised with thousands of random/semi-random inputs:

- `TextProtocolNeverCrashesOnRandomInput`: 20,000 lines mixing
  plausible protocol tokens with raw random bytes, some exceeding the
  512-byte line limit -- asserts the call always returns normally
  (no crash, no uncaught exception).
- `BinaryProtocolNeverCrashesOnRandomBytes`: 20,000 buffers of random
  bytes and random lengths (0-80 bytes) -- asserts `decode_binary`
  never reports consuming more bytes than were supplied.
- `BinaryProtocolRoundTripNeverCorruptsValidMessages`: a property test
  (not pure fuzzing) over structurally valid `NewOrderCommand`s with
  random field values -- asserts `encode(decode(encode(x))) ==
  encode(x)` byte-for-byte, catching encode/decode asymmetry bugs a
  byte-fuzzer would rarely stumble into.

This is **not** a coverage-guided fuzzer (no libFuzzer/AFL corpus,
mutation, or coverage feedback) -- it's a seeded pseudo-random
byte-basher run as a normal GoogleTest case, which is judged adequate
given the small size and narrow, branchy nature of the two functions
under test. A libFuzzer harness (compiling `parse_text_command`/
`decode_binary` as a standalone `LLVMFuzzerTestOneInput` target under
`-fsanitize=fuzzer,address`) is a natural future improvement and would
give real coverage feedback and corpus minimization; it wasn't added
here to keep the project's build dependencies to what's specified
(GoogleTest, Google Benchmark, no additional fuzzing toolchain
requirement).

These tests only really "have teeth" against memory-safety bugs when
run under ASan/UBSan (`./scripts/build.sh asan && ./build-asan/exchange_tests --gtest_filter='FuzzTest.*'`)
-- confirmed clean on this machine, see `docs/performance.md`.

## What is intentionally not covered

- Network-layer fuzzing of the live TCP gateway (only the parsing
  functions themselves are fuzzed, not the socket-handling code path
  around them). The gateway's `recv()` loop and line-buffering logic
  are covered by manual/integration-style testing (see the worked
  example in the top-level README) rather than automated tests, since
  spinning up real sockets in unit tests adds flakiness for limited
  additional coverage beyond what the parser fuzz tests already give.
- Multi-instrument matching engine sharding/threading (not implemented
  -- see `docs/architecture.md` "Future improvements").
