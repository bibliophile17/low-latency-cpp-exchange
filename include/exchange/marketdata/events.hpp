#pragma once

#include "exchange/core/types.hpp"
#include <cstdint>
#include <variant>

namespace exchange::marketdata {

struct OrderAccepted {
    core::OrderId order_id{};
    core::InstrumentId instrument{};
    core::Side side{};
    core::Price price{};
    core::Quantity quantity{};
    core::SequenceNumber sequence{};
};

struct OrderCancelled {
    core::OrderId order_id{};
    core::InstrumentId instrument{};
    core::SequenceNumber sequence{};
};

struct OrderModified {
    core::OrderId order_id{};
    core::InstrumentId instrument{};
    core::Quantity new_quantity{};
    core::SequenceNumber sequence{};
};

struct OrderRejected {
    core::OrderId order_id{};
    core::InstrumentId instrument{};
    core::SequenceNumber sequence{};
    std::string_view reason; // points to a static string literal; not owning
};

struct TradeExecuted {
    core::OrderId aggressor_order_id{};
    core::OrderId resting_order_id{};
    core::InstrumentId instrument{};
    core::Price price{};
    core::Quantity quantity{};
    core::SequenceNumber sequence{};
    std::uint64_t trade_id{};
};

struct BookUpdate {
    core::InstrumentId instrument{};
    core::Price best_bid{};
    core::Quantity best_bid_qty{};
    core::Price best_ask{};
    core::Quantity best_ask_qty{};
    bool has_bid{false};
    bool has_ask{false};
    core::SequenceNumber sequence{};
};

using MarketDataEvent = std::variant<OrderAccepted,
                                      OrderCancelled,
                                      OrderModified,
                                      OrderRejected,
                                      TradeExecuted,
                                      BookUpdate>;

} // namespace exchange::marketdata
