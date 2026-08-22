#include "exchange/matching/matching_engine.hpp"
#include <gtest/gtest.h>

using namespace exchange::core;
using namespace exchange::matching;
namespace md = exchange::marketdata;

namespace {

class EventCollector {
public:
    std::vector<md::MarketDataEvent> events;

    void operator()(const md::MarketDataEvent& e) { events.push_back(e); }

    template <typename T>
    std::vector<T> of_type() const {
        std::vector<T> out;
        for (const auto& e : events) {
            if (std::holds_alternative<T>(e)) out.push_back(std::get<T>(e));
        }
        return out;
    }
};

NewOrderCommand new_limit(std::uint64_t id, Side side, const char* px, std::uint64_t qty,
                           TimeInForce tif = TimeInForce::Day) {
    NewOrderCommand cmd;
    cmd.id = make_order_id(id);
    cmd.instrument = make_instrument_id(1);
    cmd.side = side;
    cmd.type = OrderType::Limit;
    cmd.tif = tif;
    cmd.price = parse_price(px);
    cmd.quantity = make_qty(qty);
    return cmd;
}

NewOrderCommand new_market(std::uint64_t id, Side side, std::uint64_t qty) {
    NewOrderCommand cmd;
    cmd.id = make_order_id(id);
    cmd.instrument = make_instrument_id(1);
    cmd.side = side;
    cmd.type = OrderType::Market;
    cmd.tif = TimeInForce::IOC;
    cmd.price = make_price(0);
    cmd.quantity = make_qty(qty);
    return cmd;
}

} // namespace

TEST(MatchingEngineTest, EmptyBookRestsOrder) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Buy, "100.00", 10)});

    auto accepted = collector.of_type<md::OrderAccepted>();
    ASSERT_EQ(accepted.size(), 1u);
    EXPECT_EQ(accepted[0].order_id, make_order_id(1));

    auto trades = collector.of_type<md::TradeExecuted>();
    EXPECT_TRUE(trades.empty());

    const auto* book = engine.book_for(make_instrument_id(1));
    ASSERT_NE(book, nullptr);
    EXPECT_EQ(*book->best_bid(), parse_price("100.00"));
}

TEST(MatchingEngineTest, CrossingOrdersProduceFullFill) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 10)});
    engine.process(Command{new_limit(2, Side::Buy, "100.00", 10)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, make_qty(10));
    EXPECT_EQ(trades[0].price, parse_price("100.00"));
    EXPECT_EQ(trades[0].aggressor_order_id, make_order_id(2));
    EXPECT_EQ(trades[0].resting_order_id, make_order_id(1));

    const auto* book = engine.book_for(make_instrument_id(1));
    EXPECT_FALSE(book->best_bid().has_value());
    EXPECT_FALSE(book->best_ask().has_value());
}

TEST(MatchingEngineTest, PartialFillLeavesRemainderResting) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 10)});
    engine.process(Command{new_limit(2, Side::Buy, "100.00", 4)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, make_qty(4));

    const auto* book = engine.book_for(make_instrument_id(1));
    ASSERT_TRUE(book->best_ask().has_value());
    EXPECT_EQ(*book->best_ask(), parse_price("100.00"));
    auto depth = book->ask_depth(1);
    EXPECT_EQ(depth[0].total_quantity, make_qty(6));
}

TEST(MatchingEngineTest, MultipleFillsAcrossLevels) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 5)});
    engine.process(Command{new_limit(2, Side::Sell, "100.50", 5)});
    engine.process(Command{new_limit(3, Side::Buy, "101.00", 8)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, parse_price("100.00")); // best price first
    EXPECT_EQ(trades[0].quantity, make_qty(5));
    EXPECT_EQ(trades[1].price, parse_price("100.50"));
    EXPECT_EQ(trades[1].quantity, make_qty(3));

    const auto* book = engine.book_for(make_instrument_id(1));
    ASSERT_TRUE(book->best_ask().has_value());
    EXPECT_EQ(*book->best_ask(), parse_price("100.50"));
}

TEST(MatchingEngineTest, PriceTimePriorityFifoAtSamePrice) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 5)});
    engine.process(Command{new_limit(2, Side::Sell, "100.00", 5)});
    engine.process(Command{new_limit(3, Side::Buy, "100.00", 5)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_order_id, make_order_id(1)); // first-in resting order matched first
}

TEST(MatchingEngineTest, BetterPriceMatchesFirst) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "101.00", 5)});
    engine.process(Command{new_limit(2, Side::Sell, "100.00", 5)}); // better (lower) price, later in time
    engine.process(Command{new_limit(3, Side::Buy, "101.00", 5)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_order_id, make_order_id(2)); // price priority beats time priority
}

TEST(MatchingEngineTest, MarketOrderMatchesAtRestingPrice) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 10)});
    engine.process(Command{new_market(2, Side::Buy, 10)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].price, parse_price("100.00"));
}

TEST(MatchingEngineTest, MarketOrderDoesNotRestIfUnfilled) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_market(1, Side::Buy, 100)}); // no liquidity available

    const auto* book = engine.book_for(make_instrument_id(1));
    EXPECT_FALSE(book->best_bid().has_value());
    EXPECT_EQ(book->order_count(), 0u);
}

TEST(MatchingEngineTest, CancelRemovesRestingOrder) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Buy, "100.00", 10)});
    CancelOrderCommand cancel{make_order_id(1), make_instrument_id(1)};
    engine.process(Command{cancel});

    auto cancelled = collector.of_type<md::OrderCancelled>();
    EXPECT_EQ(cancelled.size(), 1u);

    const auto* book = engine.book_for(make_instrument_id(1));
    EXPECT_FALSE(book->best_bid().has_value());
}

TEST(MatchingEngineTest, ModifyReducesQuantity) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Buy, "100.00", 10)});
    ModifyOrderCommand modify{make_order_id(1), make_instrument_id(1), make_qty(3)};
    engine.process(Command{modify});

    auto modified = collector.of_type<md::OrderModified>();
    ASSERT_EQ(modified.size(), 1u);
    EXPECT_EQ(modified[0].new_quantity, make_qty(3));
}

TEST(MatchingEngineTest, InvalidOrderRejectedZeroQuantity) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Buy, "100.00", 0)});

    auto rejected = collector.of_type<md::OrderRejected>();
    EXPECT_EQ(rejected.size(), 1u);
}

TEST(MatchingEngineTest, DuplicateOrderIdRejected) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Buy, "100.00", 10)});
    engine.process(Command{new_limit(1, Side::Buy, "101.00", 5)});

    auto rejected = collector.of_type<md::OrderRejected>();
    EXPECT_EQ(rejected.size(), 1u);
}

TEST(MatchingEngineTest, UnknownInstrumentRejected) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    // instrument 1 never registered

    engine.process(Command{new_limit(1, Side::Buy, "100.00", 10)});
    auto rejected = collector.of_type<md::OrderRejected>();
    EXPECT_EQ(rejected.size(), 1u);
}

TEST(MatchingEngineTest, CancelUnknownOrderRejected) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    CancelOrderCommand cancel{make_order_id(999), make_instrument_id(1)};
    engine.process(Command{cancel});

    auto rejected = collector.of_type<md::OrderRejected>();
    EXPECT_EQ(rejected.size(), 1u);
}

TEST(MatchingEngineTest, MultipleInstrumentsIsolated) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));
    engine.add_instrument(make_instrument_id(2));

    NewOrderCommand order1 = new_limit(1, Side::Buy, "100.00", 10);
    order1.instrument = make_instrument_id(1);
    NewOrderCommand order2 = new_limit(2, Side::Sell, "50.00", 10);
    order2.instrument = make_instrument_id(2);

    engine.process(Command{order1});
    engine.process(Command{order2});

    EXPECT_TRUE(engine.book_for(make_instrument_id(1))->best_bid().has_value());
    EXPECT_FALSE(engine.book_for(make_instrument_id(1))->best_ask().has_value());
    EXPECT_TRUE(engine.book_for(make_instrument_id(2))->best_ask().has_value());
    EXPECT_FALSE(engine.book_for(make_instrument_id(2))->best_bid().has_value());
}

TEST(MatchingEngineTest, LargeQuantityHandledCorrectly) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 10'000'000)});
    engine.process(Command{new_limit(2, Side::Buy, "100.00", 10'000'000)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, make_qty(10'000'000));
}

TEST(MatchingEngineTest, IocCancelsUnfilledRemainder) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 5)});
    engine.process(Command{new_limit(2, Side::Buy, "100.00", 10, TimeInForce::IOC)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, make_qty(5));

    const auto* book = engine.book_for(make_instrument_id(1));
    EXPECT_FALSE(book->best_bid().has_value()); // remaining 5 not rested
}

TEST(MatchingEngineTest, FokRejectedWhenInsufficientLiquidity) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 5)});
    engine.process(Command{new_limit(2, Side::Buy, "100.00", 10, TimeInForce::FOK)});

    auto trades = collector.of_type<md::TradeExecuted>();
    EXPECT_TRUE(trades.empty());
    auto rejected = collector.of_type<md::OrderRejected>();
    EXPECT_EQ(rejected.size(), 1u);
}

TEST(MatchingEngineTest, FokFillsWhenLiquiditySufficient) {
    EventCollector collector;
    MatchingEngine engine(std::ref(collector));
    engine.add_instrument(make_instrument_id(1));

    engine.process(Command{new_limit(1, Side::Sell, "100.00", 10)});
    engine.process(Command{new_limit(2, Side::Buy, "100.00", 10, TimeInForce::FOK)});

    auto trades = collector.of_type<md::TradeExecuted>();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, make_qty(10));
}
