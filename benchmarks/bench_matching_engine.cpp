#include "exchange/gateway/symbol_table.hpp"
#include "exchange/matching/matching_engine.hpp"
#include "exchange/tools/synthetic_generator.hpp"

#include <benchmark/benchmark.h>
#include <memory>

using namespace exchange::core;
using namespace exchange;

static void BM_MatchingEngine_LimitOrderInsertNoMatch(benchmark::State& state) {
    InstrumentId id = make_instrument_id(1);
    auto make_engine = [&]() {
        auto engine = std::make_unique<matching::MatchingEngine>([](const marketdata::MarketDataEvent&) {});
        engine->add_instrument(id, 1 << 20);
        return engine;
    };
    auto engine = make_engine();

    std::uint64_t order_id = 1;
    std::uint64_t inserted_since_reset = 0;
    constexpr std::uint64_t kResetThreshold = 1u << 19;
    for (auto _ : state) {
        if (inserted_since_reset >= kResetThreshold) {
            state.PauseTiming();
            engine = make_engine();
            inserted_since_reset = 0;
            state.ResumeTiming();
        }
        NewOrderCommand cmd;
        cmd.id = make_order_id(order_id);
        cmd.instrument = id;
        cmd.side = order_id % 2 == 0 ? Side::Buy : Side::Sell;
        cmd.type = OrderType::Limit;
        cmd.tif = TimeInForce::Day;
        // Alternate far-apart prices so orders never cross (measures
        // pure insert cost, isolated from matching cost).
        cmd.price = cmd.side == Side::Buy ? parse_price("100.00") : parse_price("200.00");
        cmd.quantity = make_qty(10);
        engine->process(Command{cmd});
        ++order_id;
        ++inserted_since_reset;
    }
}
BENCHMARK(BM_MatchingEngine_LimitOrderInsertNoMatch);

static void BM_MatchingEngine_FullyCrossingOrders(benchmark::State& state) {
    matching::MatchingEngine engine([](const marketdata::MarketDataEvent&) {});
    InstrumentId id = make_instrument_id(1);
    engine.add_instrument(id, 1 << 20);

    std::uint64_t order_id = 1;
    for (auto _ : state) {
        // Each iteration: rest a sell, then cross it with a buy. Measures
        // the full match-and-remove path.
        NewOrderCommand sell;
        sell.id = make_order_id(order_id++);
        sell.instrument = id;
        sell.side = Side::Sell;
        sell.type = OrderType::Limit;
        sell.tif = TimeInForce::Day;
        sell.price = parse_price("100.00");
        sell.quantity = make_qty(10);
        engine.process(Command{sell});

        NewOrderCommand buy;
        buy.id = make_order_id(order_id++);
        buy.instrument = id;
        buy.side = Side::Buy;
        buy.type = OrderType::Limit;
        buy.tif = TimeInForce::Day;
        buy.price = parse_price("100.00");
        buy.quantity = make_qty(10);
        engine.process(Command{buy});
    }
}
BENCHMARK(BM_MatchingEngine_FullyCrossingOrders);

static void BM_MatchingEngine_MarketOrders(benchmark::State& state) {
    matching::MatchingEngine engine([](const marketdata::MarketDataEvent&) {});
    InstrumentId id = make_instrument_id(1);
    engine.add_instrument(id, 1 << 20);

    std::uint64_t order_id = 1;
    for (auto _ : state) {
        NewOrderCommand sell;
        sell.id = make_order_id(order_id++);
        sell.instrument = id;
        sell.side = Side::Sell;
        sell.type = OrderType::Limit;
        sell.tif = TimeInForce::Day;
        sell.price = parse_price("100.00");
        sell.quantity = make_qty(10);
        engine.process(Command{sell});

        NewOrderCommand buy;
        buy.id = make_order_id(order_id++);
        buy.instrument = id;
        buy.side = Side::Buy;
        buy.type = OrderType::Market;
        buy.tif = TimeInForce::IOC;
        buy.quantity = make_qty(10);
        engine.process(Command{buy});
    }
}
BENCHMARK(BM_MatchingEngine_MarketOrders);

// End-to-end: replay a pre-generated synthetic order flow through the
// engine, representative of realistic mixed traffic (news, cancels,
// modifies, crosses).
static void BM_MatchingEngine_EndToEndSyntheticFlow(benchmark::State& state) {
    gateway::SymbolTable symbols;
    InstrumentId id = symbols.intern("AAPL");

    for (auto _ : state) {
        state.PauseTiming();
        matching::MatchingEngine engine([](const marketdata::MarketDataEvent&) {});
        engine.add_instrument(id, 1 << 20);

        tools::GeneratorConfig config;
        config.seed = 42;
        config.order_count = static_cast<std::uint64_t>(state.range(0));
        config.instruments = {id};
        tools::SyntheticOrderGenerator generator(config);
        state.ResumeTiming();

        for (std::uint64_t i = 0; i < config.order_count; ++i) {
            engine.process(generator.next());
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_MatchingEngine_EndToEndSyntheticFlow)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMillisecond);
