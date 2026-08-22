#include "exchange/orderbook/order_book.hpp"

namespace exchange::orderbook {

using core::Order;
using core::OrderId;
using core::Price;
using core::Quantity;
using core::Side;

PriceLevel& OrderBook::level_for(Side side, Price price) {
    if (side == Side::Buy) {
        auto [it, inserted] = bids_.try_emplace(price);
        if (inserted) it->second.price = price;
        return it->second;
    } else {
        auto [it, inserted] = asks_.try_emplace(price);
        if (inserted) it->second.price = price;
        return it->second;
    }
}

void OrderBook::erase_level_if_empty(Side side, Price price) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end() && it->second.empty()) bids_.erase(it);
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end() && it->second.empty()) asks_.erase(it);
    }
}

std::uint32_t OrderBook::acquire_order_slot(const Order& order) {
    std::uint32_t idx = pool_.acquire();
    pool_[idx] = order;
    return idx;
}

void OrderBook::insert_resting(const Order& order_template, std::uint32_t pool_index) {
    Order& order = pool_[pool_index];
    order = order_template; // ensure slot reflects final state (qty may have been reduced by matching)
    PriceLevel& level = level_for(order.side, order.price);
    level_push_back(level, pool_, pool_index);
    locations_[order.id] = OrderLocation{pool_index, order.side};
}

std::uint32_t OrderBook::peek_best(Side side) const noexcept {
    if (side == Side::Buy) {
        if (bids_.empty()) return memory::ObjectPool<Order>::kInvalidIndex;
        return bids_.begin()->second.head;
    } else {
        if (asks_.empty()) return memory::ObjectPool<Order>::kInvalidIndex;
        return asks_.begin()->second.head;
    }
}

bool OrderBook::fill_best(Side side, Quantity qty) {
    std::uint32_t idx = peek_best(side);
    if (idx == memory::ObjectPool<Order>::kInvalidIndex) return false;

    Order& order = pool_[idx];
    Price price = order.price;
    PriceLevel& level = level_for(side, price);

    level_reduce_quantity(level, order, qty);

    if (order.remaining_quantity == core::make_qty(0)) {
        order.status = core::OrderStatus::Filled;
        level_erase(level, pool_, idx);
        locations_.erase(order.id);
        pool_.release(idx);
        erase_level_if_empty(side, price);
        return true;
    }
    order.status = core::OrderStatus::PartiallyFilled;
    return false;
}

bool OrderBook::remove_order(OrderId id) {
    auto it = locations_.find(id);
    if (it == locations_.end()) return false;

    OrderLocation loc = it->second;
    Order& order = pool_[loc.pool_index];
    Price price = order.price;
    Side side = loc.side;

    PriceLevel& level = level_for(side, price);
    level_erase(level, pool_, loc.pool_index);
    locations_.erase(it);
    pool_.release(loc.pool_index);
    erase_level_if_empty(side, price);
    return true;
}

bool OrderBook::reduce_order_quantity(OrderId id, Quantity new_remaining_qty) {
    auto it = locations_.find(id);
    if (it == locations_.end()) return false;

    OrderLocation loc = it->second;
    Order& order = pool_[loc.pool_index];
    if (new_remaining_qty >= order.remaining_quantity) return false;

    PriceLevel& level = level_for(loc.side, order.price);
    Quantity delta = order.remaining_quantity - new_remaining_qty;
    level_reduce_quantity(level, order, delta);
    return true;
}

std::optional<OrderLocation> OrderBook::find(OrderId id) const {
    auto it = locations_.find(id);
    if (it == locations_.end()) return std::nullopt;
    return it->second;
}

std::vector<BookDepthLevel> OrderBook::bid_depth(std::size_t depth) const {
    std::vector<BookDepthLevel> out;
    // Reserve at most the number of levels that actually exist (callers
    // may legitimately pass SIZE_MAX to mean "all levels").
    out.reserve(std::min(depth, bids_.size()));
    for (auto it = bids_.begin(); it != bids_.end() && out.size() < depth; ++it) {
        out.push_back(BookDepthLevel{it->first, it->second.total_quantity, it->second.order_count});
    }
    return out;
}

std::vector<BookDepthLevel> OrderBook::ask_depth(std::size_t depth) const {
    std::vector<BookDepthLevel> out;
    out.reserve(std::min(depth, asks_.size()));
    for (auto it = asks_.begin(); it != asks_.end() && out.size() < depth; ++it) {
        out.push_back(BookDepthLevel{it->first, it->second.total_quantity, it->second.order_count});
    }
    return out;
}

} // namespace exchange::orderbook
