#pragma once

#include "exchange/core/types.hpp"
#include <variant>

namespace exchange::core {

struct NewOrderCommand {
    OrderId id{};
    InstrumentId instrument{};
    Side side{};
    OrderType type{};
    TimeInForce tif{TimeInForce::Day};
    Price price{}; // ignored for Market
    Quantity quantity{};
};

struct CancelOrderCommand {
    OrderId id{};
    InstrumentId instrument{};
};

struct ModifyOrderCommand {
    OrderId id{};
    InstrumentId instrument{};
    Quantity new_quantity{}; // this simplified engine only supports quantity-reduce modify
};

using Command = std::variant<NewOrderCommand, CancelOrderCommand, ModifyOrderCommand>;

} // namespace exchange::core
