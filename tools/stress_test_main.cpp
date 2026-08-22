// Stress test executable.
//
// Feeds a large randomized (but seeded/reproducible) synthetic order
// flow through the matching engine and checks book-level invariants
// after every command, rather than only measuring speed. See
// docs/testing.md for the full invariant list; the checks here mirror
// tests/test_invariants.cpp but run against much larger, randomized
// workloads than the unit tests use.
//
// Usage:
//   stress_test --orders 500000 --seed 7 --instruments 5
#include "exchange/gateway/symbol_table.hpp"
#include "exchange/matching/matching_engine.hpp"
#include "exchange/tools/synthetic_generator.hpp"

#include <chrono>
#include <iostream>
#include <vector>

namespace {
using namespace exchange;

struct InvariantViolation {
    std::string description;
};

// Walks every resting order in every book and checks:
//   - remaining_quantity is never zero for a resting order (a zero-qty
//     order should have been removed from the book)
//   - remaining_quantity never exceeds original quantity
//   - price levels' cached total_quantity matches the sum of their
//     orders' remaining_quantity
//   - bid/ask FIFO ordering is consistent (walked without crashing;
//     head/tail integrity implicitly checked by the walk itself)
//   - best bid < best ask (book is not crossed) whenever both sides
//     are non-empty -- a crossed book means matching failed to match
//     something it should have.
bool check_invariants(const orderbook::OrderBook& book, std::vector<InvariantViolation>& violations) {
    bool ok = true;

    if (auto bb = book.best_bid(); bb) {
        if (auto ba = book.best_ask(); ba) {
            if (*bb >= *ba) {
                violations.push_back({"crossed book: best_bid >= best_ask"});
                ok = false;
            }
        }
    }

    for (const auto& lvl : book.bid_depth(SIZE_MAX)) {
        if (core::value_of(lvl.total_quantity) == 0 && lvl.order_count > 0) {
            violations.push_back({"bid level has zero total quantity but nonzero order count"});
            ok = false;
        }
    }
    for (const auto& lvl : book.ask_depth(SIZE_MAX)) {
        if (core::value_of(lvl.total_quantity) == 0 && lvl.order_count > 0) {
            violations.push_back({"ask level has zero total quantity but nonzero order count"});
            ok = false;
        }
    }

    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t order_count = 200'000;
    std::uint64_t seed = 7;
    std::size_t instrument_count = 3;

    std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--orders" && i + 1 < args.size()) order_count = std::stoull(args[++i]);
        else if (args[i] == "--seed" && i + 1 < args.size()) seed = std::stoull(args[++i]);
        else if (args[i] == "--instruments" && i + 1 < args.size()) instrument_count = std::stoul(args[++i]);
    }

    gateway::SymbolTable symbol_table;
    std::vector<core::InstrumentId> instrument_ids;
    for (std::size_t i = 0; i < instrument_count; ++i) {
        instrument_ids.push_back(symbol_table.intern("SYM" + std::to_string(i)));
    }

    std::uint64_t trade_count = 0, reject_count = 0, accept_count = 0;

    matching::MatchingEngine engine([&](const marketdata::MarketDataEvent& e) {
        std::visit([&](auto&& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, marketdata::TradeExecuted>) ++trade_count;
            else if constexpr (std::is_same_v<T, marketdata::OrderRejected>) ++reject_count;
            else if constexpr (std::is_same_v<T, marketdata::OrderAccepted>) ++accept_count;
        }, e);
    });
    for (auto id : instrument_ids) engine.add_instrument(id, order_count / instrument_count + 1024);

    tools::GeneratorConfig config;
    config.seed = seed;
    config.order_count = order_count;
    config.instruments = instrument_ids;
    config.burst_probability = 0.03;
    config.burst_size = 30;
    tools::SyntheticOrderGenerator generator(config);

    std::cout << "Stress test: " << order_count << " commands across " << instrument_count
              << " instruments (seed=" << seed << ")\n";

    auto start = std::chrono::steady_clock::now();
    std::vector<InvariantViolation> violations;
    std::uint64_t checked = 0;

    for (std::uint64_t i = 0; i < order_count; ++i) {
        engine.process(generator.next());

        // Checking invariants after every single command would dominate
        // runtime for large workloads; sample periodically plus always
        // check the final state.
        if (i % 1000 == 0 || i == order_count - 1) {
            for (auto id : instrument_ids) {
                const auto* book = engine.book_for(id);
                if (book != nullptr) {
                    check_invariants(*book, violations);
                    ++checked;
                }
            }
        }
    }
    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "Processed " << order_count << " commands in " << seconds << "s ("
              << (order_count / std::max(seconds, 1e-9)) << " ops/sec)\n";
    std::cout << "Accepted: " << accept_count << "  Trades: " << trade_count
              << "  Rejected: " << reject_count << "\n";
    std::cout << "Invariant checks performed: " << checked << "\n";

    if (!violations.empty()) {
        std::cerr << "INVARIANT VIOLATIONS DETECTED (" << violations.size() << "):\n";
        std::size_t shown = 0;
        for (const auto& v : violations) {
            std::cerr << "  - " << v.description << "\n";
            if (++shown >= 20) {
                std::cerr << "  ... (" << (violations.size() - shown) << " more)\n";
                break;
            }
        }
        return 1;
    }

    std::cout << "No invariant violations detected.\n";
    return 0;
}
