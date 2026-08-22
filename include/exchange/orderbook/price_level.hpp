#pragma once

#include "exchange/core/order.hpp"
#include "exchange/core/types.hpp"
#include "exchange/memory/object_pool.hpp"

#include <cstdint>

namespace exchange::orderbook {

// A single price level: a FIFO queue of orders at exactly one price,
// implemented as an intrusive doubly-linked list over pool indices
// (Order::prev_in_level / next_in_level). No node allocation: the
// "list nodes" are the Order objects themselves, which already live in
// the ObjectPool. This is why Order carries prev/next indices instead of
// e.g. storing orders in a std::deque per level.
struct PriceLevel {
    core::Price price{};
    std::uint32_t head{exchange::memory::ObjectPool<core::Order>::kInvalidIndex}; // oldest order (front of FIFO)
    std::uint32_t tail{exchange::memory::ObjectPool<core::Order>::kInvalidIndex}; // newest order (back of FIFO)
    core::Quantity total_quantity{core::make_qty(0)}; // sum of remaining_quantity, cached for O(1) depth queries
    std::uint32_t order_count{0};

    [[nodiscard]] bool empty() const noexcept { return order_count == 0; }
};

// Appends an order to the back of the level's FIFO queue (time priority:
// new orders go to the back). O(1).
inline void level_push_back(PriceLevel& level,
                             memory::ObjectPool<core::Order>& pool,
                             std::uint32_t order_index) {
    core::Order& order = pool[order_index];
    order.prev_in_level = level.tail;
    order.next_in_level = memory::ObjectPool<core::Order>::kInvalidIndex;

    if (level.tail != memory::ObjectPool<core::Order>::kInvalidIndex) {
        pool[level.tail].next_in_level = order_index;
    } else {
        level.head = order_index;
    }
    level.tail = order_index;

    level.total_quantity = level.total_quantity + order.remaining_quantity;
    ++level.order_count;
}

// Removes an order from anywhere in the level's FIFO queue. O(1) because
// the list is doubly linked -- this is why cancel-anywhere-in-book is
// cheap and does not require scanning the level.
inline void level_erase(PriceLevel& level,
                         memory::ObjectPool<core::Order>& pool,
                         std::uint32_t order_index) {
    core::Order& order = pool[order_index];

    if (order.prev_in_level != memory::ObjectPool<core::Order>::kInvalidIndex) {
        pool[order.prev_in_level].next_in_level = order.next_in_level;
    } else {
        level.head = order.next_in_level;
    }

    if (order.next_in_level != memory::ObjectPool<core::Order>::kInvalidIndex) {
        pool[order.next_in_level].prev_in_level = order.prev_in_level;
    } else {
        level.tail = order.prev_in_level;
    }

    level.total_quantity = level.total_quantity - order.remaining_quantity;
    --level.order_count;

    order.prev_in_level = memory::ObjectPool<core::Order>::kInvalidIndex;
    order.next_in_level = memory::ObjectPool<core::Order>::kInvalidIndex;
}

// Reduces the resting quantity of an order in place (used for partial
// fills and quantity-reduce Modify). Keeps the level's cached
// total_quantity consistent. O(1).
inline void level_reduce_quantity(PriceLevel& level,
                                   core::Order& order,
                                   core::Quantity amount) {
    order.remaining_quantity = order.remaining_quantity - amount;
    level.total_quantity = level.total_quantity - amount;
}

} // namespace exchange::orderbook
