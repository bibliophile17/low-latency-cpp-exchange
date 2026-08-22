#pragma once

#include "exchange/core/types.hpp"
#include <cstdint>

namespace exchange::core {

// Order is a plain-old-data struct deliberately kept small and
// trivially copyable so it can live in a pre-allocated pool (see
// memory/object_pool.hpp) with no per-order heap allocation.
struct Order {
    OrderId id{};
    InstrumentId instrument{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce tif{TimeInForce::Day};
    OrderStatus status{OrderStatus::New};

    Price price{};                 // ignored for Market orders
    Quantity quantity{};           // original quantity
    Quantity remaining_quantity{}; // quantity still open

    SequenceNumber sequence{};     // assigned at gateway; time priority tiebreaker
    std::uint64_t recv_time_ns{};  // monotonic receive timestamp, for latency metrics

    // Intrusive FIFO linked-list pointers used by the price-level order
    // book (see orderbook/price_level.hpp). Indices into the order pool
    // rather than raw pointers, so the pool can be a flat contiguous
    // array (cache-friendlier, no pointer chasing across heap pages).
    std::uint32_t prev_in_level{UINT32_MAX};
    std::uint32_t next_in_level{UINT32_MAX};

    [[nodiscard]] bool is_fully_filled() const noexcept {
        return remaining_quantity == make_qty(0);
    }
};

} // namespace exchange::core
