# Order Book Design

## Price representation: integer ticks, not `double`

`core::Price` (`include/exchange/core/types.hpp`) is an `enum class`
wrapping `int64_t`, representing price as an integer count of "ticks"
(1 tick = 10^-4 of a price unit, i.e. 4 decimal places). It is **not** a
`double`.

Why this matters:

1. **Exactness.** `double` cannot represent most decimal fractions
   exactly (`0.1` has no exact binary representation). Repeatedly
   aggregating quantity/price at a level, or comparing two prices for
   equality, accumulates rounding error with floating point. Integer
   ticks make every comparison and every arithmetic operation exact.
2. **Fast, correct ordering as a map key.** `std::map<Price, PriceLevel>`
   needs a well-defined total order and exact equality for its
   red-black tree operations. Floating-point equality is famously
   unreliable; integers have none of these problems.
3. **Cheap hashing/indexing.** Even though the book itself doesn't hash
   prices, related systems (e.g. a hypothetical array-indexed variant,
   see below) benefit from prices being usable directly as array
   offsets, which requires an integer representation.

`parse_price("185.20")` / `format_price(price)` convert between the
human-readable decimal string used by the text protocol and the
internal tick representation exactly, using manual digit-by-digit
parsing rather than `atof`/`strtod`, specifically to avoid introducing
binary floating-point error at the parse boundary.

## Price levels: `std::map<Price, PriceLevel>`, not `std::map<double, ...>`

Each side of the book (`OrderBook::bids_`, `OrderBook::asks_`) is a
`std::map<Price, PriceLevel>`:

- Bids use `std::greater<Price>` so the highest bid is always
  `bids_.begin()`.
- Asks use `std::less<Price>` (the default) so the lowest ask is always
  `asks_.begin()`.

This gives O(log L) insertion/removal of a *price level* (L = number of
distinct price levels currently active, not number of orders) and O(1)
amortized best-price lookup via `begin()`. In practice L is small --
tens to a few hundred active levels even for a busy synthetic book in
this project's benchmarks/stress tests -- so `log(L)` is a handful of
pointer-chasing comparisons.

### Alternative considered: flat array indexed by tick offset

A common approach in real low-latency books, when an instrument's
price range is known and bounded ahead of time, is a flat
`std::vector<PriceLevel>` indexed by `(price - reference_price) /
tick_size`. This gives true O(1) insertion of a *new* price level (no
tree rebalancing at all) at the cost of:

- Pre-allocating (or dynamically resizing) a possibly wide, sparse
  array covering the instrument's entire tradable price range.
- Needing explicit handling for orders priced outside the pre-allocated
  range (reject, or resize -- both add complexity).

This project intentionally did **not** default to that approach,
because:

- It targets multiple synthetic instruments with essentially arbitrary
  price ranges (see `tools/synthetic_generator.cpp`), where a fixed
  array either wastes memory for the unbounded case or needs resizing
  logic whose complexity isn't justified at the order counts this
  project benchmarks (hundreds of thousands to low millions).
- `std::map`'s O(log L) is not the dominant cost in this system's
  hot path in practice -- see `docs/performance.md` for measured
  breakdowns; per-order matching cost is dominated by the O(1)
  FIFO-list and pool operations, not by level lookup.

This tradeoff is documented here rather than silently made; a flat
array is a reasonable "future improvement" (see `docs/architecture.md`)
for an instrument whose price range is known in advance.

## Orders within a level: intrusive FIFO linked list

`orderbook/price_level.hpp` implements each `PriceLevel`'s FIFO queue
as an **intrusive** doubly linked list: the "next"/"prev" pointers
(`Order::prev_in_level` / `Order::next_in_level`) live directly inside
the `Order` struct, as indices into the shared `ObjectPool<Order>`
rather than as a separate `std::list<Order>` or `std::deque<Order>` per
level.

Why:

- **No per-node allocation.** A `std::list` node is a separate heap
  allocation; with potentially many price levels this would mean many
  small allocations on the order-insert hot path. The intrusive list
  reuses the `Order`'s own pool slot as the "node," so list
  insert/erase touches zero heap memory.
- **O(1) insert-at-back** (`level_push_back`) for new orders (time
  priority: new orders always go to the back of the queue at their
  price).
- **O(1) erase-from-anywhere** (`level_erase`) for `Cancel`, which can
  reference any order in any position in the queue -- this is exactly
  why a doubly (not singly) linked list is used: singly linked lists
  need O(n) predecessor lookup to unlink a middle node.

Each `PriceLevel` also caches `total_quantity` (sum of resting
quantity at that price) and `order_count`, updated incrementally on
every insert/erase/fill so that book-depth queries
(`OrderBook::bid_depth`/`ask_depth`) are O(1) per level rather than
requiring a walk of the FIFO list.

## Order lookup by `OrderId`: `std::unordered_map`

`Cancel`/`Modify` commands arrive with only an `OrderId` -- the gateway
protocol does not require clients to remember which price level their
order rests at. `OrderBook::locations_` is a
`std::unordered_map<OrderId, OrderLocation>` (pool index + side) giving
O(1) average lookup.

This is the one heap-backed hash map on the hot path, and it is
unavoidable given the wire protocol's design (referencing orders by ID
rather than by book position). Its capacity is reserved up front in the
constructor (`locations_.reserve(expected_orders)`) to avoid rehashing
during a benchmark or stress run.

## Memory management

See `include/exchange/memory/object_pool.hpp` for the allocation-free
pool used to store `Order` objects; `docs/performance.md` covers where
allocations do and don't happen in more detail.
