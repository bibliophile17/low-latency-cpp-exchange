// Matching engine.
//
// Deterministic, single-threaded on its critical path (processing one
// Command produces a deterministic sequence of MarketDataEvents, given
// the same book state). See docs/concurrency.md for why this is a
// deliberate choice: matching engines are the one component in this
// architecture where correctness (price-time priority, no
// double-fills, no lost updates) is far more valuable than parallel
// throughput, and the matching workload for a single instrument is
// inherently sequential anyway (each order can change what the next
// order matches against). Parallelism is applied *around* the engine
// (network I/O, market-data fan-out) rather than *inside* it.
//
// Events are emitted via a caller-supplied callback (EventSink) rather
// than an owned queue, so the engine has no dependency on sockets,
// threads, or any particular queue implementation -- it can be driven
// directly from unit tests, from the replay engine, or from a
// production pipeline that pushes events onto an SPSC queue
// (concurrency/spsc_queue.hpp) for a separate market-data thread.
#pragma once

#include "exchange/core/commands.hpp"
#include "exchange/core/order.hpp"
#include "exchange/core/types.hpp"
#include "exchange/marketdata/events.hpp"
#include "exchange/orderbook/order_book.hpp"

#include <functional>
#include <unordered_map>

namespace exchange::matching {

using EventSink = std::function<void(const marketdata::MarketDataEvent&)>;

class MatchingEngine {
public:
    explicit MatchingEngine(EventSink sink) : sink_(std::move(sink)) {}

    // Registers an instrument so orders can be matched against it. Must
    // be called before any command referencing that instrument.
    void add_instrument(core::InstrumentId id, std::size_t expected_orders = 1 << 16);

    // Processes one command to completion (fully matches/rests/cancels/
    // modifies before returning) and emits all resulting events via the
    // sink, in deterministic order:
    //   OrderAccepted or OrderRejected -> zero or more TradeExecuted
    //   -> BookUpdate (if the top of book changed).
    void process(const core::Command& command);

    [[nodiscard]] const orderbook::OrderBook* book_for(core::InstrumentId id) const;
    [[nodiscard]] orderbook::OrderBook* book_for(core::InstrumentId id);

    [[nodiscard]] std::uint64_t next_trade_id() const noexcept { return next_trade_id_; }

private:
    void handle_new_order(const core::NewOrderCommand& cmd);
    void handle_cancel(const core::CancelOrderCommand& cmd);
    void handle_modify(const core::ModifyOrderCommand& cmd);

    // Matches an incoming order against the opposite side of the book as
    // much as price allows, emitting TradeExecuted events. Reduces
    // `remaining` in place. Returns true if the order is IOC/FOK/Market
    // and should never be inserted as resting (even if quantity remains).
    void match_against_book(orderbook::OrderBook& book,
                             core::Order& incoming,
                             core::SequenceNumber seq);

    void emit_book_update(const orderbook::OrderBook& book, core::SequenceNumber seq);

    [[nodiscard]] static bool crosses(core::Side side, core::Price incoming_price, core::Price resting_price) noexcept;

    EventSink sink_;
    std::unordered_map<core::InstrumentId, orderbook::OrderBook> books_;
    std::uint64_t next_trade_id_ = 1;
};

} // namespace exchange::matching
