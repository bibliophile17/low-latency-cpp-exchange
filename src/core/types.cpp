#include "exchange/core/types.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <array>

namespace exchange::core {

Price parse_price(std::string_view text) {
    // Parses "185.2", "185.2000", "185", "-1.5" into integer ticks
    // (kPriceScale = 10000 ticks per unit). Manual parse (not
    // std::from_chars for doubles, which isn't universally available,
    // and to keep exact decimal semantics instead of binary float error).
    if (text.empty()) throw std::invalid_argument("empty price string");

    bool negative = false;
    size_t i = 0;
    if (text[0] == '-') { negative = true; i = 1; }
    else if (text[0] == '+') { i = 1; }

    std::int64_t integer_part = 0;
    bool any_digits = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        integer_part = integer_part * 10 + (text[i] - '0');
        ++i;
        any_digits = true;
    }

    std::int64_t frac_ticks = 0;
    if (i < text.size() && text[i] == '.') {
        ++i;
        std::int64_t scale = kPriceScale;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            scale /= 10;
            if (scale > 0) {
                frac_ticks += (text[i] - '0') * scale;
            }
            ++i;
            any_digits = true;
        }
    }

    if (!any_digits || i != text.size()) {
        throw std::invalid_argument("invalid price string: " + std::string(text));
    }

    std::int64_t total = integer_part * kPriceScale + frac_ticks;
    if (negative) total = -total;
    return make_price(total);
}

std::string format_price(Price p) {
    std::int64_t t = ticks_of(p);
    bool negative = t < 0;
    if (negative) t = -t;
    std::int64_t integer_part = t / kPriceScale;
    std::int64_t frac_part = t % kPriceScale;

    std::string out;
    if (negative) out.push_back('-');
    out += std::to_string(integer_part);
    out.push_back('.');
    // pad fractional part to 4 digits
    std::string frac_str = std::to_string(frac_part);
    out += std::string(4 - frac_str.size(), '0') + frac_str;
    return out;
}

std::ostream& operator<<(std::ostream& os, Price p) {
    return os << format_price(p);
}

std::ostream& operator<<(std::ostream& os, Quantity q) {
    return os << value_of(q);
}

std::string_view to_string(Side s) noexcept {
    switch (s) {
        case Side::Buy: return "BUY";
        case Side::Sell: return "SELL";
    }
    return "UNKNOWN";
}

std::string_view to_string(OrderType t) noexcept {
    switch (t) {
        case OrderType::Limit: return "LIMIT";
        case OrderType::Market: return "MARKET";
    }
    return "UNKNOWN";
}

std::string_view to_string(TimeInForce t) noexcept {
    switch (t) {
        case TimeInForce::Day: return "DAY";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK: return "FOK";
    }
    return "UNKNOWN";
}

std::string_view to_string(OrderStatus s) noexcept {
    switch (s) {
        case OrderStatus::New: return "NEW";
        case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::Rejected: return "REJECTED";
        case OrderStatus::Replaced: return "REPLACED";
    }
    return "UNKNOWN";
}

} // namespace exchange::core
