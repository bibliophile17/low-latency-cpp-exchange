// Core strong types used throughout the exchange.
//
// Design notes
// ------------
// Prices are represented as integer "ticks" rather than double/float.
// Rationale (see docs/order-book.md for the full discussion):
//   1. Floating point cannot represent decimal fractions exactly (e.g. 0.1),
//      so repeated arithmetic on prices (aggregating quantity at a level,
//      comparing for equality) accumulates rounding error.
//   2. Exchanges quote prices at a fixed tick size (e.g. $0.01 for many
//      US equities). Representing price as an integer number of ticks
//      makes price comparison, hashing, and array/vector indexing exact
//      and fast (integer compare instead of epsilon-based float compare).
//   3. Integers are cheaper to hash and to use as map/array keys, which
//      matters on the hot path.
//
// A "tick" here is an application-defined minimum price increment. This
// project uses 1 tick = 0.0001 (four decimal places) by default, encoded
// as Price = int64_t. This gives headroom for both equities-style and
// FX-style instruments without overflowing a 64-bit integer for any
// realistic price.

#pragma once

#include <cstdint>
#include <compare>
#include <functional>
#include <string>
#include <string_view>
#include <ostream>

namespace exchange::core {

// ---------------------------------------------------------------------
// Price: fixed-point integer, in ticks. 1 tick = 10^-4 of a price unit.
// ---------------------------------------------------------------------
enum class Price : std::int64_t {};

inline constexpr std::int64_t kPriceScale = 10'000; // 4 decimal places

constexpr Price make_price(std::int64_t ticks) noexcept { return Price{ticks}; }
constexpr std::int64_t ticks_of(Price p) noexcept { return static_cast<std::int64_t>(p); }

constexpr Price operator+(Price a, Price b) noexcept {
    return Price{ticks_of(a) + ticks_of(b)};
}
constexpr Price operator-(Price a, Price b) noexcept {
    return Price{ticks_of(a) - ticks_of(b)};
}
constexpr auto operator<=>(Price a, Price b) noexcept {
    return ticks_of(a) <=> ticks_of(b);
}
constexpr bool operator==(Price a, Price b) noexcept { return ticks_of(a) == ticks_of(b); }

// Parse a human-readable decimal string like "185.20" into ticks.
Price parse_price(std::string_view text);
// Format ticks back into a decimal string, e.g. "185.2000".
std::string format_price(Price p);

std::ostream& operator<<(std::ostream& os, Price p);

// ---------------------------------------------------------------------
// Quantity: integer shares/contracts/units. Never fractional in this
// simplified model, and never negative once validated.
// ---------------------------------------------------------------------
enum class Quantity : std::uint64_t {};

constexpr Quantity make_qty(std::uint64_t q) noexcept { return Quantity{q}; }
constexpr std::uint64_t value_of(Quantity q) noexcept { return static_cast<std::uint64_t>(q); }

constexpr Quantity operator+(Quantity a, Quantity b) noexcept {
    return Quantity{value_of(a) + value_of(b)};
}
constexpr Quantity operator-(Quantity a, Quantity b) noexcept {
    return Quantity{value_of(a) - value_of(b)};
}
constexpr auto operator<=>(Quantity a, Quantity b) noexcept {
    return value_of(a) <=> value_of(b);
}
constexpr bool operator==(Quantity a, Quantity b) noexcept { return value_of(a) == value_of(b); }

std::ostream& operator<<(std::ostream& os, Quantity q);

// ---------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------
enum class OrderId : std::uint64_t {};
constexpr OrderId make_order_id(std::uint64_t v) noexcept { return OrderId{v}; }
constexpr std::uint64_t value_of(OrderId id) noexcept { return static_cast<std::uint64_t>(id); }
constexpr bool operator==(OrderId a, OrderId b) noexcept { return value_of(a) == value_of(b); }

enum class InstrumentId : std::uint32_t {};
constexpr InstrumentId make_instrument_id(std::uint32_t v) noexcept { return InstrumentId{v}; }
constexpr std::uint32_t value_of(InstrumentId id) noexcept { return static_cast<std::uint32_t>(id); }
constexpr bool operator==(InstrumentId a, InstrumentId b) noexcept { return value_of(a) == value_of(b); }

// Monotonic sequence number assigned by the gateway on receipt. Used for
// price-time priority (time component) and for deterministic replay.
enum class SequenceNumber : std::uint64_t {};
constexpr SequenceNumber make_seq(std::uint64_t v) noexcept { return SequenceNumber{v}; }
constexpr std::uint64_t value_of(SequenceNumber s) noexcept { return static_cast<std::uint64_t>(s); }

// ---------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------
enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

enum class OrderType : std::uint8_t { Limit = 0, Market = 1 };

enum class TimeInForce : std::uint8_t {
    Day = 0,        // default: rests on the book until filled/cancelled
    IOC = 1,        // immediate-or-cancel: fill what's possible, cancel remainder
    FOK = 2,         // fill-or-kill: fill completely immediately or cancel entirely
};

enum class OrderStatus : std::uint8_t {
    New = 0,
    PartiallyFilled = 1,
    Filled = 2,
    Cancelled = 3,
    Rejected = 4,
    Replaced = 5,
};

std::string_view to_string(Side s) noexcept;
std::string_view to_string(OrderType t) noexcept;
std::string_view to_string(TimeInForce t) noexcept;
std::string_view to_string(OrderStatus s) noexcept;

} // namespace exchange::core

// std::hash specializations for use as unordered_map keys.
namespace std {
template <> struct hash<exchange::core::OrderId> {
    size_t operator()(exchange::core::OrderId id) const noexcept {
        return std::hash<std::uint64_t>{}(exchange::core::value_of(id));
    }
};
template <> struct hash<exchange::core::InstrumentId> {
    size_t operator()(exchange::core::InstrumentId id) const noexcept {
        return std::hash<std::uint32_t>{}(exchange::core::value_of(id));
    }
};
} // namespace std
