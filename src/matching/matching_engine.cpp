#include "exchange/matching/matching_engine.hpp"

#include <cassert>

namespace exchange::matching {

using namespace exchange::core;
using orderbook::OrderBook;
namespace md = exchange::marketdata;

void MatchingEngine::add_instrument(InstrumentId id, std::size_t expected_orders) {
    books_.try_emplace(id, id, expected_orders);
}

const OrderBook* MatchingEngine::book_for(InstrumentId id) const {
    auto it = books_.find(id);
    return it == books_.end() ? nullptr : &it->second;
}

OrderBook* MatchingEngine::book_for(InstrumentId id) {
    auto it = books_.find(id);
    return it == books_.end() ? nullptr : &it->second;
}

bool MatchingEngine::crosses(Side side, Price incoming_price, Price resting_price) noexcept {
    // A buy crosses (can trade against) a resting ask priced at or below
    // the buy's limit. A sell crosses a resting bid priced at or above
    // the sell's limit.
    return side == Side::Buy ? incoming_price >= resting_price
                              : incoming_price <= resting_price;
}

void MatchingEngine::process(const Command& command) {
    std::visit([this](auto&& cmd) {
        using T = std::decay_t<decltype(cmd)>;
        if constexpr (std::is_same_v<T, NewOrderCommand>) {
            handle_new_order(cmd);
        } else if constexpr (std::is_same_v<T, CancelOrderCommand>) {
            handle_cancel(cmd);
        } else if constexpr (std::is_same_v<T, ModifyOrderCommand>) {
            handle_modify(cmd);
        }
    }, command);
}

void MatchingEngine::emit_book_update(const OrderBook& book, SequenceNumber seq) {
    md::BookUpdate upd;
    upd.instrument = book.instrument();
    upd.sequence = seq;
    if (auto bb = book.best_bid()) {
        upd.has_bid = true;
        upd.best_bid = *bb;
        auto depth = book.bid_depth(1);
        upd.best_bid_qty = depth.empty() ? make_qty(0) : depth[0].total_quantity;
    }
    if (auto ba = book.best_ask()) {
        upd.has_ask = true;
        upd.best_ask = *ba;
        auto depth = book.ask_depth(1);
        upd.best_ask_qty = depth.empty() ? make_qty(0) : depth[0].total_quantity;
    }
    sink_(upd);
}

void MatchingEngine::handle_new_order(const NewOrderCommand& cmd) {
    OrderBook* book = book_for(cmd.instrument);
    if (book == nullptr) {
        sink_(md::OrderRejected{cmd.id, cmd.instrument, SequenceNumber{}, "unknown instrument"});
        return;
    }
    if (value_of(cmd.quantity) == 0) {
        sink_(md::OrderRejected{cmd.id, cmd.instrument, SequenceNumber{}, "zero quantity"});
        return;
    }
    if (cmd.type == OrderType::Limit && ticks_of(cmd.price) <= 0) {
        sink_(md::OrderRejected{cmd.id, cmd.instrument, SequenceNumber{}, "non-positive limit price"});
        return;
    }
    if (book->find(cmd.id).has_value()) {
        sink_(md::OrderRejected{cmd.id, cmd.instrument, SequenceNumber{}, "duplicate order id"});
        return;
    }

    SequenceNumber seq = make_seq(value_of(cmd.id)); // sequence == order id ordering assumption documented in README/protocol
    // (Gateway assigns strictly increasing OrderId/sequence together; see docs/network-protocol.md)

    Order incoming{};
    incoming.id = cmd.id;
    incoming.instrument = cmd.instrument;
    incoming.side = cmd.side;
    incoming.type = cmd.type;
    incoming.tif = cmd.tif;
    incoming.status = OrderStatus::New;
    incoming.price = cmd.price;
    incoming.quantity = cmd.quantity;
    incoming.remaining_quantity = cmd.quantity;
    incoming.sequence = seq;

    // Fill-or-Kill: dry-run check that full quantity is achievable before
    // committing any state change (fills are irreversible / emit events
    // immediately, so we must know in advance).
    if (cmd.tif == TimeInForce::FOK) {
        Side opp_side = opposite(cmd.side);
        Quantity available = make_qty(0);
        auto check_depth = [&](const std::vector<orderbook::BookDepthLevel>& levels) {
            for (const auto& lvl : levels) {
                bool would_cross = cmd.type == OrderType::Market ||
                                    crosses(cmd.side, cmd.price, lvl.price);
                if (!would_cross) break;
                available = available + lvl.total_quantity;
                if (available >= cmd.quantity) return;
            }
        };
        // Walk enough levels to know if we can fill; depth is bounded by
        // how much quantity we need, so an unbounded depth request here
        // is safe in practice (order books rarely have thousands of
        // distinct price levels).
        std::size_t max_levels = opp_side == Side::Buy ? 4096 : 4096;
        check_depth(opp_side == Side::Buy ? book->bid_depth(max_levels) : book->ask_depth(max_levels));
        if (available < cmd.quantity) {
            sink_(md::OrderRejected{cmd.id, cmd.instrument, seq, "FOK could not be fully filled"});
            return;
        }
    }

    sink_(md::OrderAccepted{cmd.id, cmd.instrument, cmd.side, cmd.price, cmd.quantity, seq});

    match_against_book(*book, incoming, seq);

    bool should_rest = incoming.type == OrderType::Limit &&
                        cmd.tif == TimeInForce::Day &&
                        value_of(incoming.remaining_quantity) > 0;

    if (should_rest) {
        std::uint32_t idx = book->acquire_order_slot(incoming);
        book->insert_resting(incoming, idx);
    }
    // IOC/FOK/Market orders: any unfilled remainder is simply not
    // inserted (cancelled implicitly); no separate event is required
    // beyond the trades already emitted, matching common exchange
    // semantics where the OrderAccepted + fills fully describe the
    // outcome and lack of a resting order implies the remainder died.

    emit_book_update(*book, seq);
}

void MatchingEngine::match_against_book(OrderBook& book, Order& incoming, SequenceNumber seq) {
    Side opp_side = opposite(incoming.side);

    while (value_of(incoming.remaining_quantity) > 0) {
        std::uint32_t best_idx = book.peek_best(opp_side);
        if (best_idx == memory::ObjectPool<Order>::kInvalidIndex) break;

        const Order& resting = book.order_at(best_idx);

        bool would_cross = incoming.type == OrderType::Market ||
                            crosses(incoming.side, incoming.price, resting.price);
        if (!would_cross) break;

        Quantity trade_qty = std::min(incoming.remaining_quantity, resting.remaining_quantity);
        Price trade_price = resting.price; // resting order's price is the execution price

        OrderId resting_id = resting.id; // copy before fill_best potentially destroys the slot

        book.fill_best(opp_side, trade_qty);
        incoming.remaining_quantity = incoming.remaining_quantity - trade_qty;
        incoming.status = value_of(incoming.remaining_quantity) == 0
                               ? OrderStatus::Filled
                               : OrderStatus::PartiallyFilled;

        md::TradeExecuted trade;
        trade.aggressor_order_id = incoming.id;
        trade.resting_order_id = resting_id;
        trade.instrument = incoming.instrument;
        trade.price = trade_price;
        trade.quantity = trade_qty;
        trade.sequence = seq;
        trade.trade_id = next_trade_id_++;
        sink_(trade);
    }
}

void MatchingEngine::handle_cancel(const CancelOrderCommand& cmd) {
    OrderBook* book = book_for(cmd.instrument);
    if (book == nullptr) {
        sink_(md::OrderRejected{cmd.id, cmd.instrument, SequenceNumber{}, "unknown instrument"});
        return;
    }
    if (!book->remove_order(cmd.id)) {
        sink_(md::OrderRejected{cmd.id, cmd.instrument, SequenceNumber{}, "unknown order id"});
        return;
    }
    SequenceNumber seq = make_seq(value_of(cmd.id));
    sink_(md::OrderCancelled{cmd.id, cmd.instrument, seq});
    emit_book_update(*book, seq);
}

void MatchingEngine::handle_modify(const ModifyOrderCommand& cmd) {
    OrderBook* book = book_for(cmd.instrument);
    if (book == nullptr) {
        sink_(md::OrderRejected{cmd.id, cmd.instrument, SequenceNumber{}, "unknown instrument"});
        return;
    }
    // This simplified engine supports quantity-reduce modify only, which
    // preserves time priority (a common exchange rule: shrinking your
    // order keeps your place in the queue; increasing quantity or
    // changing price loses time priority and must be cancel+replace).
    if (!book->reduce_order_quantity(cmd.id, cmd.new_quantity)) {
        sink_(md::OrderRejected{cmd.id, cmd.instrument, SequenceNumber{}, "unknown order or invalid new quantity"});
        return;
    }
    SequenceNumber seq = make_seq(value_of(cmd.id));
    sink_(md::OrderModified{cmd.id, cmd.instrument, cmd.new_quantity, seq});
    emit_book_update(*book, seq);
}

} // namespace exchange::matching
