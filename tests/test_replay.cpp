#include "exchange/gateway/symbol_table.hpp"
#include "exchange/matching/matching_engine.hpp"
#include "exchange/replay/replay_engine.hpp"
#include "exchange/tools/synthetic_generator.hpp"

#include <gtest/gtest.h>
#include <cstdio>

using namespace exchange::core;
using namespace exchange;

namespace {

std::string temp_file_path(const char* name) {
    return std::string("/tmp/exchange_test_") + name;
}

// Runs `count` synthetic commands (given seed) through a fresh matching
// engine and returns a summary vector capturing every trade (price,
// quantity, aggressor, resting) and every rejection, in order. Two runs
// with the same seed should produce identical summaries if the engine
// (and the generator) are deterministic.
std::vector<std::string> run_and_summarize(std::uint64_t seed, std::uint64_t count) {
    gateway::SymbolTable symbols;
    std::vector<InstrumentId> ids{symbols.intern("AAPL"), symbols.intern("MSFT")};

    std::vector<std::string> summary;
    matching::MatchingEngine engine([&](const marketdata::MarketDataEvent& e) {
        std::visit([&](auto&& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, marketdata::TradeExecuted>) {
                summary.push_back("TRADE " + std::to_string(value_of(ev.aggressor_order_id)) +
                                   " " + std::to_string(value_of(ev.resting_order_id)) +
                                   " " + format_price(ev.price) +
                                   " " + std::to_string(value_of(ev.quantity)));
            } else if constexpr (std::is_same_v<T, marketdata::OrderRejected>) {
                summary.push_back("REJECT " + std::to_string(value_of(ev.order_id)));
            }
        }, e);
    });
    for (auto id : ids) engine.add_instrument(id);

    tools::GeneratorConfig config;
    config.seed = seed;
    config.order_count = count;
    config.instruments = ids;
    tools::SyntheticOrderGenerator generator(config);

    for (std::uint64_t i = 0; i < count; ++i) {
        engine.process(generator.next());
    }
    return summary;
}

} // namespace

TEST(ReplayTest, RecordThenReplayProducesSameCommandSequence) {
    std::string path = temp_file_path("replay_basic.bin");

    NewOrderCommand new_order;
    new_order.id = make_order_id(1);
    new_order.instrument = make_instrument_id(1);
    new_order.side = Side::Buy;
    new_order.type = OrderType::Limit;
    new_order.tif = TimeInForce::Day;
    new_order.price = parse_price("100.00");
    new_order.quantity = make_qty(10);

    CancelOrderCommand cancel{make_order_id(1), make_instrument_id(1)};

    {
        replay::RecordWriter writer(path);
        writer.write(Command{new_order});
        writer.write(Command{cancel});
        EXPECT_EQ(writer.records_written(), 2u);
    }

    replay::RecordReader reader(path);
    ASSERT_TRUE(reader.is_open());

    auto first = reader.next();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(std::holds_alternative<NewOrderCommand>(*first));
    EXPECT_EQ(std::get<NewOrderCommand>(*first).id, make_order_id(1));

    auto second = reader.next();
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(std::holds_alternative<CancelOrderCommand>(*second));

    auto third = reader.next();
    EXPECT_FALSE(third.has_value()); // EOF

    std::remove(path.c_str());
}

TEST(ReplayTest, SyntheticGeneratorIsDeterministicForSameSeed) {
    auto summary1 = run_and_summarize(/*seed=*/12345, /*count=*/2000);
    auto summary2 = run_and_summarize(/*seed=*/12345, /*count=*/2000);
    ASSERT_EQ(summary1.size(), summary2.size());
    for (std::size_t i = 0; i < summary1.size(); ++i) {
        EXPECT_EQ(summary1[i], summary2[i]) << "mismatch at index " << i;
    }
    // Sanity: the run should have produced at least some trades given
    // the generator's default price-walk configuration.
    EXPECT_GT(summary1.size(), 0u);
}

TEST(ReplayTest, DifferentSeedsProduceDifferentSequences) {
    auto summary1 = run_and_summarize(/*seed=*/1, /*count=*/2000);
    auto summary2 = run_and_summarize(/*seed=*/2, /*count=*/2000);
    // Not a strict requirement that every element differs, but the
    // overall sequences should not be identical for different seeds.
    EXPECT_NE(summary1, summary2);
}

TEST(ReplayTest, RecordAndReplayThroughMatchingEngineIsDeterministic) {
    std::string path = temp_file_path("replay_determinism.bin");
    gateway::SymbolTable symbols;
    std::vector<InstrumentId> ids{symbols.intern("AAPL")};

    tools::GeneratorConfig config;
    config.seed = 999;
    config.order_count = 500;
    config.instruments = ids;
    tools::SyntheticOrderGenerator generator(config);

    {
        replay::RecordWriter writer(path);
        for (std::uint64_t i = 0; i < config.order_count; ++i) writer.write(generator.next());
    }

    auto replay_once = [&]() {
        std::vector<std::string> summary;
        matching::MatchingEngine engine([&](const marketdata::MarketDataEvent& e) {
            if (auto* trade = std::get_if<marketdata::TradeExecuted>(&e)) {
                summary.push_back(format_price(trade->price) + "/" + std::to_string(value_of(trade->quantity)));
            }
        });
        engine.add_instrument(ids[0]);

        replay::RecordReader reader(path);
        while (auto cmd = reader.next()) engine.process(*cmd);
        return summary;
    };

    auto run1 = replay_once();
    auto run2 = replay_once();
    EXPECT_EQ(run1, run2);

    std::remove(path.c_str());
}
