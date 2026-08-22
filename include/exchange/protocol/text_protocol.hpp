// Human-readable text order-entry protocol, for development/debugging
// and for the CLI client. See docs/network-protocol.md for the full
// grammar and the binary protocol used for performance benchmarking.
//
// Grammar (one command per line, whitespace-separated tokens):
//   NEW BUY|SELL <SYMBOL> <QTY> <PRICE> [IOC|FOK]
//   NEW MARKET BUY|SELL <SYMBOL> <QTY>
//   CANCEL <ORDER_ID> <SYMBOL>
//   MODIFY <ORDER_ID> <SYMBOL> <NEW_QTY>
//
// Example:
//   NEW BUY AAPL 100 185.20
//   NEW SELL AAPL 50 185.25 IOC
//   CANCEL 12345 AAPL
//   MODIFY 12346 AAPL 200
#pragma once

#include "exchange/core/commands.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace exchange::protocol {

// Resolves a ticker symbol string to an InstrumentId. Supplied by the
// caller (gateway) which owns the symbol table.
using SymbolResolver = std::function<core::InstrumentId(std::string_view)>;

struct ParseError {
    std::string message;
};

// Parses one line of text protocol into a Command. `order_id` is the
// id to assign to NEW orders (the gateway assigns ids/sequence numbers
// centrally; the wire protocol lets a dev client submit without knowing
// its own id in advance -- see docs/network-protocol.md). CANCEL/MODIFY
// carry an explicit id supplied by the client since they must reference
// a previously-accepted order.
//
// Returns std::nullopt and sets `error` on malformed input. Never
// throws: this function's whole job is to safely reject untrusted
// network input (see docs/network-protocol.md "Security" section).
std::optional<core::Command> parse_text_command(std::string_view line,
                                                  const SymbolResolver& resolve_symbol,
                                                  core::OrderId assign_id_for_new,
                                                  ParseError& error) noexcept;

// Formats a NewOrderCommand back to its text representation. Used by
// the replay/record subsystem's human-readable dump mode and by tests.
std::string format_text_command(const core::Command& command, std::string_view symbol);

} // namespace exchange::protocol
