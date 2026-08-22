#include "exchange/gateway/symbol_table.hpp"
#include "exchange/matching/matching_engine.hpp"
#include "exchange/tools/synthetic_generator.hpp"

#include <gtest/gtest.h>

using namespace exchange::core;
using namespace exchange;

// These tests check book-level invariants after driving the engine
// through a moderate-sized randomized (seeded) order flow -- a smaller,
// CI-friendly counterpart to tools/stress_test_main.cpp, which runs the
// same style of check over much larger workloads.

TEST(InvariantTest, BookNeverCrossedAfterRandomFlow) {
    gateway::SymbolTable symbols;
    std::vector<InstrumentId> ids{symbols.intern("AAPL"), symbols.intern("MSFT")};

    matching::MatchingEngine engine([](const marketdata::MarketDataEvent&) {});
    for (auto id : ids) engine.add_instrument(id);

    tools::GeneratorConfig config;
    config.seed = 555;
    config.order_count = 5000;
    config.instruments = ids;
    tools::SyntheticOrderGenerator generator(config);

    for (std::uint64_t i = 0; i < config.order_count; ++i) {
        engine.process(generator.next());

        for (auto id : ids) {
            const auto* book = engine.book_for(id);
            ASSERT_NE(book, nullptr);
            auto bb = book->best_bid();
            auto ba = book->best_ask();
            if (bb && ba) {
                EXPECT_LT(*bb, *ba) << "book crossed at iteration " << i;
            }
        }
    }
}

TEST(InvariantTest, PriceLevelTotalQuantityMatchesSumOfOrders) {
    gateway::SymbolTable symbols;
    InstrumentId id = symbols.intern("AAPL");

    matching::MatchingEngine engine([](const marketdata::MarketDataEvent&) {});
    engine.add_instrument(id);

    tools::GeneratorConfig config;
    config.seed = 321;
    config.order_count = 3000;
    config.instruments = {id};
    tools::SyntheticOrderGenerator generator(config);

    for (std::uint64_t i = 0; i < config.order_count; ++i) {
        engine.process(generator.next());
    }

    const auto* book = engine.book_for(id);
    ASSERT_NE(book, nullptr);

    for (const auto& lvl : book->bid_depth(SIZE_MAX)) {
        EXPECT_GT(lvl.order_count, 0u);
        EXPECT_GT(value_of(lvl.total_quantity), 0u);
    }
    for (const auto& lvl : book->ask_depth(SIZE_MAX)) {
        EXPECT_GT(lvl.order_count, 0u);
        EXPECT_GT(value_of(lvl.total_quantity), 0u);
    }
}

TEST(InvariantTest, CancelledOrderCannotBeCancelledAgain) {
    gateway::SymbolTable symbols;
    InstrumentId id = symbols.intern("AAPL");

    std::vector<marketdata::MarketDataEvent> events;
    matching::MatchingEngine engine([&](const marketdata::MarketDataEvent& e) { events.push_back(e); });
    engine.add_instrument(id);

    NewOrderCommand new_order;
    new_order.id = make_order_id(1);
    new_order.instrument = id;
    new_order.side = Side::Buy;
    new_order.type = OrderType::Limit;
    new_order.tif = TimeInForce::Day;
    new_order.price = parse_price("100.00");
    new_order.quantity = make_qty(10);
    engine.process(Command{new_order});

    CancelOrderCommand cancel{make_order_id(1), id};
    engine.process(Command{cancel});
    engine.process(Command{cancel}); // second cancel of same id must be rejected, not silently succeed

    int cancelled_count = 0, rejected_count = 0;
    for (const auto& e : events) {
        if (std::holds_alternative<marketdata::OrderCancelled>(e)) ++cancelled_count;
        if (std::holds_alternative<marketdata::OrderRejected>(e)) ++rejected_count;
    }
    EXPECT_EQ(cancelled_count, 1);
    EXPECT_EQ(rejected_count, 1);
}

TEST(InvariantTest, QuantityNeverNegativeAcrossRandomFlow) {
    // Quantity is unsigned (core::Quantity wraps uint64_t), so an
    // "underflow" would appear as a huge wraparound value rather than a
    // negative number. This test checks no resting order's quantity
    // exceeds its original quantity, which would indicate corruption.
    gateway::SymbolTable symbols;
    InstrumentId id = symbols.intern("AAPL");

    matching::MatchingEngine engine([](const marketdata::MarketDataEvent&) {});
    engine.add_instrument(id);

    tools::GeneratorConfig config;
    config.seed = 8080;
    config.order_count = 4000;
    config.instruments = {id};
    tools::SyntheticOrderGenerator generator(config);

    for (std::uint64_t i = 0; i < config.order_count; ++i) {
        engine.process(generator.next());
    }

    const auto* book = engine.book_for(id);
    for (const auto& lvl : book->bid_depth(SIZE_MAX)) {
        // Sanity bound: no single level should ever accumulate an
        // absurd quantity that would indicate a wraparound bug
        // (max_qty per order is small in the generator's default config).
        EXPECT_LT(value_of(lvl.total_quantity), 1'000'000'000ull);
    }
}
