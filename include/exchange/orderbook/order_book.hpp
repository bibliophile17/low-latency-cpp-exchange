// Limit order book for a single instrument.
//
// Data-structure choice (see docs/order-book.md for full discussion):
//
//   Price levels: std::map<Price, PriceLevel> where Price is an integer
//   number of ticks (see core/types.hpp), NOT std::map<double, ...>.
//     - Bids are keyed with std::greater<Price> so the best bid is
//       always bids_.begin() (highest price first).
//     - Asks use the default std::less<Price> so the best ask is always
//       asks_.begin() (lowest price first).
//     - std::map gives O(log L) insert/erase of a price level (L = number
//       of distinct price levels, not number of orders) and O(1)
//       amortized best-price lookup via begin(). L is typically small
//       (tens to low hundreds of active levels) even for a busy book, so
//       log(L) is a handful of comparisons in practice.
//     - An alternative considered: a flat std::vector/array indexed by
//       (price - reference_price) / tick_size, giving true O(1) insert
//       of a *new* level at the cost of pre-allocating (or resizing) a
//       possibly wide, sparse array and handling out-of-range prices.
//       This is the approach many real low-latency books use when the
//       instrument's price range is known and bounded ahead of time.
//       It is documented as a "future improvement" in docs/order-book.md
//       and is a reasonable extension; it was not the default here
//       because this project targets multiple synthetic instruments
//       with arbitrary/unbounded price ranges, where a fixed array
//       either wastes memory or needs dynamic resizing logic that adds
//       complexity without changing the asymptotic story much at the
//       order counts this project benchmarks.
//
//   Orders within a level: intrusive FIFO linked list over pool indices,
//   see orderbook/price_level.hpp. O(1) insert-at-back, O(1) erase from
//   anywhere (needed for cancel), no per-order heap allocation.
//
//   Order lookup by OrderId (for Cancel/Modify): std::unordered_map from
//   OrderId to a small OrderLocation record (pool index, side, price).
//   O(1) average lookup. This is the one heap-backed hash map on the hot
//   path; it is unavoidable because Cancel/Modify arrive with only an
//   OrderId and must find the order in O(1)-ish time rather than
//   scanning the book. Rehashing is amortized and can be avoided
//   entirely by reserving capacity up front (done in the constructor).
#pragma once

#include "exchange/core/order.hpp"
#include "exchange/core/types.hpp"
#include "exchange/memory/object_pool.hpp"
#include "exchange/orderbook/price_level.hpp"

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace exchange::orderbook {

struct OrderLocation {
    std::uint32_t pool_index{};
    core::Side side{};
};

// Result of attempting to add a resting order or apply a fill; used by
// the matching engine to decide what events to emit.
struct Fill {
    core::OrderId aggressor_id{};
    core::OrderId resting_id{};
    core::Price price{};      // trade executes at the resting order's price
    core::Quantity quantity{};
};

struct BookDepthLevel {
    core::Price price{};
    core::Quantity total_quantity{};
    std::uint32_t order_count{};
};

class OrderBook {
public:
    explicit OrderBook(core::InstrumentId instrument, std::size_t expected_orders = 1 << 16)
        : instrument_(instrument), pool_(expected_orders) {
        locations_.reserve(expected_orders);
    }

    [[nodiscard]] core::InstrumentId instrument() const noexcept { return instrument_; }

    // --- Book-side accessors -------------------------------------------------
    [[nodiscard]] bool has_bids() const noexcept { return !bids_.empty(); }
    [[nodiscard]] bool has_asks() const noexcept { return !asks_.empty(); }

    [[nodiscard]] std::optional<core::Price> best_bid() const noexcept {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }
    [[nodiscard]] std::optional<core::Price> best_ask() const noexcept {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }
    [[nodiscard]] std::optional<core::Price> spread() const noexcept {
        auto bb = best_bid();
        auto ba = best_ask();
        if (!bb || !ba) return std::nullopt;
        return *ba - *bb;
    }

    // Returns up to `depth` levels on each side, best price first.
    [[nodiscard]] std::vector<BookDepthLevel> bid_depth(std::size_t depth) const;
    [[nodiscard]] std::vector<BookDepthLevel> ask_depth(std::size_t depth) const;

    [[nodiscard]] std::size_t order_count() const noexcept { return locations_.size(); }

    // --- Core operations used by the matching engine -------------------------
    // These are intentionally low-level; MatchingEngine (matching/matching_engine.hpp)
    // orchestrates them to implement price-time-priority matching. Keeping
    // the book itself free of matching *policy* (e.g. it does not decide
    // when to match, only how to insert/remove/query) keeps the two
    // concerns separable and independently testable.

    // Insert a new resting order at its limit price. Caller (matching
    // engine) is responsible for having already matched what it can
    // against the opposite side; this only adds what remains.
    void insert_resting(const core::Order& order_template, std::uint32_t pool_index);

    // Peek at the best resting order on `side` without removing it.
    // Returns kInvalidIndex if that side is empty.
    [[nodiscard]] std::uint32_t peek_best(core::Side side) const noexcept;

    // Access an order by pool index (for the matching engine to read/mutate
    // remaining_quantity during a fill).
    [[nodiscard]] core::Order& order_at(std::uint32_t pool_index) { return pool_[pool_index]; }
    [[nodiscard]] const core::Order& order_at(std::uint32_t pool_index) const { return pool_[pool_index]; }

    // Reduce the best resting order on `side` by `qty` (a fill). If this
    // fully consumes the order, it is removed from the book and its pool
    // slot released; returns true if the order was fully consumed.
    bool fill_best(core::Side side, core::Quantity qty);

    // Remove an order entirely (used both for full fills and Cancel).
    // Returns false if the order id is unknown.
    bool remove_order(core::OrderId id);

    // Reduce an order's quantity in place (Modify with smaller quantity).
    // Returns false if unknown or if new_qty >= current remaining qty
    // (use remove+re-insert for price changes or quantity increases,
    // since those must lose time priority per standard exchange rules).
    bool reduce_order_quantity(core::OrderId id, core::Quantity new_remaining_qty);

    [[nodiscard]] std::optional<OrderLocation> find(core::OrderId id) const;

    // Acquire a pool slot and populate it from `order`, but do NOT insert
    // into the book yet. Used by the matching engine so it can match
    // first, then insert the remainder.
    std::uint32_t acquire_order_slot(const core::Order& order);
    void release_order_slot(std::uint32_t pool_index) { pool_.release(pool_index); }

private:
    core::InstrumentId instrument_;
    memory::ObjectPool<core::Order> pool_;

    // Bids: highest price first. Asks: lowest price first.
    std::map<core::Price, PriceLevel, std::greater<core::Price>> bids_;
    std::map<core::Price, PriceLevel, std::less<core::Price>> asks_;

    std::unordered_map<core::OrderId, OrderLocation> locations_;

    PriceLevel& level_for(core::Side side, core::Price price);
    void erase_level_if_empty(core::Side side, core::Price price);
};

} // namespace exchange::orderbook
