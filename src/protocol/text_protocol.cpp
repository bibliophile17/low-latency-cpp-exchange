#include "exchange/protocol/text_protocol.hpp"

#include <charconv>
#include <optional>
#include <sstream>
#include <vector>

namespace exchange::protocol {

using namespace exchange::core;

namespace {

std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i > start) tokens.push_back(line.substr(start, i - start));
    }
    return tokens;
}

bool parse_u64(std::string_view s, std::uint64_t& out) {
    auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}

std::optional<Side> parse_side(std::string_view s) {
    if (s == "BUY") return Side::Buy;
    if (s == "SELL") return Side::Sell;
    return std::nullopt;
}

} // namespace

std::optional<Command> parse_text_command(std::string_view line,
                                           const SymbolResolver& resolve_symbol,
                                           OrderId assign_id_for_new,
                                           ParseError& error) noexcept {
    try {
        // Bound input size defensively -- untrusted network input must
        // never be parsed without a size check (docs/network-protocol.md).
        if (line.size() > 512) {
            error.message = "line too long";
            return std::nullopt;
        }

        auto tokens = tokenize(line);
        if (tokens.empty()) {
            error.message = "empty command";
            return std::nullopt;
        }

        std::string_view verb = tokens[0];

        if (verb == "NEW") {
            if (tokens.size() < 2) { error.message = "NEW: too few tokens"; return std::nullopt; }

            bool is_market = (tokens[1] == "MARKET");
            std::size_t idx = is_market ? 2 : 1;
            if (tokens.size() <= idx) { error.message = "NEW: missing side"; return std::nullopt; }

            auto side = parse_side(tokens[idx]);
            if (!side) { error.message = "NEW: invalid side"; return std::nullopt; }
            ++idx;

            if (tokens.size() <= idx) { error.message = "NEW: missing symbol"; return std::nullopt; }
            std::string_view symbol = tokens[idx++];
            InstrumentId instrument = resolve_symbol(symbol);

            if (tokens.size() <= idx) { error.message = "NEW: missing quantity"; return std::nullopt; }
            std::uint64_t qty_raw = 0;
            if (!parse_u64(tokens[idx], qty_raw) || qty_raw == 0) {
                error.message = "NEW: invalid quantity"; return std::nullopt;
            }
            ++idx;

            NewOrderCommand cmd;
            cmd.id = assign_id_for_new;
            cmd.instrument = instrument;
            cmd.side = *side;
            cmd.quantity = make_qty(qty_raw);

            if (is_market) {
                cmd.type = OrderType::Market;
                cmd.tif = TimeInForce::IOC; // market orders are implicitly IOC
                cmd.price = make_price(0);
            } else {
                cmd.type = OrderType::Limit;
                if (tokens.size() <= idx) { error.message = "NEW: missing price"; return std::nullopt; }
                try {
                    cmd.price = parse_price(tokens[idx]);
                } catch (const std::exception&) {
                    error.message = "NEW: invalid price"; return std::nullopt;
                }
                ++idx;
                cmd.tif = TimeInForce::Day;
                if (tokens.size() > idx) {
                    if (tokens[idx] == "IOC") cmd.tif = TimeInForce::IOC;
                    else if (tokens[idx] == "FOK") cmd.tif = TimeInForce::FOK;
                    else { error.message = "NEW: invalid TIF"; return std::nullopt; }
                }
            }
            return Command{cmd};
        }

        if (verb == "CANCEL") {
            if (tokens.size() < 3) { error.message = "CANCEL: expected id and symbol"; return std::nullopt; }
            std::uint64_t id_raw = 0;
            if (!parse_u64(tokens[1], id_raw)) { error.message = "CANCEL: invalid id"; return std::nullopt; }
            CancelOrderCommand cmd;
            cmd.id = make_order_id(id_raw);
            cmd.instrument = resolve_symbol(tokens[2]);
            return Command{cmd};
        }

        if (verb == "MODIFY") {
            if (tokens.size() < 4) { error.message = "MODIFY: expected id, symbol, quantity"; return std::nullopt; }
            std::uint64_t id_raw = 0, qty_raw = 0;
            if (!parse_u64(tokens[1], id_raw)) { error.message = "MODIFY: invalid id"; return std::nullopt; }
            if (!parse_u64(tokens[3], qty_raw) || qty_raw == 0) {
                error.message = "MODIFY: invalid quantity"; return std::nullopt;
            }
            ModifyOrderCommand cmd;
            cmd.id = make_order_id(id_raw);
            cmd.instrument = resolve_symbol(tokens[2]);
            cmd.new_quantity = make_qty(qty_raw);
            return Command{cmd};
        }

        error.message = "unknown verb: " + std::string(verb);
        return std::nullopt;
    } catch (const std::exception& ex) {
        error.message = std::string("exception while parsing: ") + ex.what();
        return std::nullopt;
    }
}

std::string format_text_command(const Command& command, std::string_view symbol) {
    std::ostringstream os;
    std::visit([&](auto&& cmd) {
        using T = std::decay_t<decltype(cmd)>;
        if constexpr (std::is_same_v<T, NewOrderCommand>) {
            os << "NEW ";
            if (cmd.type == OrderType::Market) os << "MARKET ";
            os << to_string(cmd.side) << " " << symbol << " " << value_of(cmd.quantity);
            if (cmd.type == OrderType::Limit) {
                os << " " << format_price(cmd.price);
                if (cmd.tif == TimeInForce::IOC) os << " IOC";
                if (cmd.tif == TimeInForce::FOK) os << " FOK";
            }
        } else if constexpr (std::is_same_v<T, CancelOrderCommand>) {
            os << "CANCEL " << value_of(cmd.id) << " " << symbol;
        } else if constexpr (std::is_same_v<T, ModifyOrderCommand>) {
            os << "MODIFY " << value_of(cmd.id) << " " << symbol << " " << value_of(cmd.new_quantity);
        }
    }, command);
    return os.str();
}

} // namespace exchange::protocol
