// Exchange CLI entry point.
//
// Usage:
//   exchange --server [--port N] [--symbols AAPL,MSFT,...]
//   exchange --replay orders.bin [--symbols AAPL,MSFT,...]
//   exchange --record orders.bin --generate N [--seed S]
//   exchange --benchmark        (prints where to find the dedicated benchmark binary)
//   exchange --help
#include "exchange/concurrency/spsc_queue.hpp"
#include "exchange/core/commands.hpp"
#include "exchange/gateway/order_gateway.hpp"
#include "exchange/gateway/symbol_table.hpp"
#include "exchange/logging/logger.hpp"
#include "exchange/marketdata/events.hpp"
#include "exchange/matching/matching_engine.hpp"
#include "exchange/metrics/latency_recorder.hpp"
#include "exchange/replay/replay_engine.hpp"
#include "exchange/tools/synthetic_generator.hpp"

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace exchange;

std::atomic<bool> g_shutdown{false};
void handle_sigint(int) { g_shutdown.store(true); }

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

void print_help() {
    std::cout <<
        "Low-Latency C++ Matching Engine & Market Data System\n\n"
        "Usage:\n"
        "  exchange --server [--port N] [--symbols AAPL,MSFT,...]\n"
        "  exchange --replay <file> [--symbols AAPL,MSFT,...]\n"
        "  exchange --record <file> --generate <N> [--seed S] [--symbols ...]\n"
        "  exchange --benchmark\n"
        "  exchange --help\n\n"
        "This project is a local educational exchange simulator. It does not\n"
        "connect to real exchanges, brokers, financial accounts, or live markets.\n";
}

void print_event(const marketdata::MarketDataEvent& event, const gateway::SymbolTable& symbols) {
    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, marketdata::OrderAccepted>) {
            std::cout << "ACCEPTED  id=" << core::value_of(e.order_id)
                       << " " << symbols.symbol_of(e.instrument)
                       << " " << core::to_string(e.side)
                       << " qty=" << core::value_of(e.quantity)
                       << " px=" << core::format_price(e.price) << "\n";
        } else if constexpr (std::is_same_v<T, marketdata::TradeExecuted>) {
            std::cout << "TRADE     trade_id=" << e.trade_id
                       << " aggressor=" << core::value_of(e.aggressor_order_id)
                       << " resting=" << core::value_of(e.resting_order_id)
                       << " " << symbols.symbol_of(e.instrument)
                       << " qty=" << core::value_of(e.quantity)
                       << " px=" << core::format_price(e.price) << "\n";
        } else if constexpr (std::is_same_v<T, marketdata::OrderCancelled>) {
            std::cout << "CANCELLED id=" << core::value_of(e.order_id) << "\n";
        } else if constexpr (std::is_same_v<T, marketdata::OrderModified>) {
            std::cout << "MODIFIED  id=" << core::value_of(e.order_id)
                       << " new_qty=" << core::value_of(e.new_quantity) << "\n";
        } else if constexpr (std::is_same_v<T, marketdata::OrderRejected>) {
            std::cout << "REJECTED  id=" << core::value_of(e.order_id)
                       << " reason=" << e.reason << "\n";
        } else if constexpr (std::is_same_v<T, marketdata::BookUpdate>) {
            // Book updates are frequent; omitted from default console
            // output to keep it readable. Use --verbose (future work)
            // to print these.
        }
    }, event);
}

int run_server(std::uint16_t port, const std::vector<std::string>& symbols) {
    gateway::SymbolTable symbol_table;
    concurrency::SpscQueue<core::Command> queue(1 << 16);

    matching::MatchingEngine engine([&symbol_table](const marketdata::MarketDataEvent& e) {
        print_event(e, symbol_table);
    });
    for (const auto& s : (symbols.empty() ? std::vector<std::string>{"AAPL", "MSFT", "GOOG"} : symbols)) {
        engine.add_instrument(symbol_table.intern(s));
    }

    gateway::OrderGateway gw(gateway::GatewayConfig{port}, symbol_table, queue);
    gw.start();

    std::signal(SIGINT, handle_sigint);
    std::cout << "Exchange server running on port " << port << ". Press Ctrl+C to stop.\n";

    while (!g_shutdown.load()) {
        auto cmd = queue.try_pop();
        if (!cmd) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        engine.process(*cmd);
    }

    gw.stop();
    std::cout << "Shutdown complete.\n";
    return 0;
}

int run_replay(const std::string& file, const std::vector<std::string>& symbols) {
    gateway::SymbolTable symbol_table;
    matching::MatchingEngine engine([&symbol_table](const marketdata::MarketDataEvent& e) {
        print_event(e, symbol_table);
    });
    for (const auto& s : (symbols.empty() ? std::vector<std::string>{"AAPL", "MSFT", "GOOG"} : symbols)) {
        engine.add_instrument(symbol_table.intern(s));
    }

    replay::RecordReader reader(file);
    if (!reader.is_open()) {
        std::cerr << "Failed to open replay file: " << file << "\n";
        return 1;
    }

    std::uint64_t count = 0;
    while (auto cmd = reader.next()) {
        engine.process(*cmd);
        ++count;
    }
    std::cout << "Replayed " << count << " commands from " << file << "\n";
    return 0;
}

int run_record(const std::string& file, std::uint64_t generate_count, std::uint64_t seed,
                const std::vector<std::string>& symbols) {
    gateway::SymbolTable symbol_table;
    std::vector<core::InstrumentId> ids;
    for (const auto& s : (symbols.empty() ? std::vector<std::string>{"AAPL", "MSFT", "GOOG"} : symbols)) {
        ids.push_back(symbol_table.intern(s));
    }

    tools::GeneratorConfig gen_config;
    gen_config.seed = seed;
    gen_config.order_count = generate_count;
    gen_config.instruments = ids;
    tools::SyntheticOrderGenerator generator(gen_config);

    replay::RecordWriter writer(file);
    for (std::uint64_t i = 0; i < generate_count; ++i) {
        writer.write(generator.next());
    }
    std::cout << "Recorded " << writer.records_written() << " synthetic commands to " << file
              << " (seed=" << seed << ")\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) {
        print_help();
        return 0;
    }

    std::uint16_t port = 9999;
    std::vector<std::string> symbols;
    std::string replay_file, record_file;
    std::uint64_t generate_count = 0;
    std::uint64_t seed = 42;

    bool do_server = false, do_replay = false, do_record = false, do_benchmark = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--help" || a == "-h") { print_help(); return 0; }
        else if (a == "--server") do_server = true;
        else if (a == "--benchmark") do_benchmark = true;
        else if (a == "--port" && i + 1 < args.size()) port = static_cast<std::uint16_t>(std::stoi(args[++i]));
        else if (a == "--symbols" && i + 1 < args.size()) symbols = split_csv(args[++i]);
        else if (a == "--replay" && i + 1 < args.size()) { do_replay = true; replay_file = args[++i]; }
        else if (a == "--record" && i + 1 < args.size()) { do_record = true; record_file = args[++i]; }
        else if (a == "--generate" && i + 1 < args.size()) generate_count = std::stoull(args[++i]);
        else if (a == "--seed" && i + 1 < args.size()) seed = std::stoull(args[++i]);
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            print_help();
            return 1;
        }
    }

    std::cout << "NOTE: This is a local educational exchange simulator. It does not connect "
                 "to real exchanges, brokers, financial accounts, or live markets.\n";

    if (do_server) return run_server(port, symbols);
    if (do_replay) return run_replay(replay_file, symbols);
    if (do_record) {
        if (generate_count == 0) {
            std::cerr << "--record requires --generate N\n";
            return 1;
        }
        return run_record(record_file, generate_count, seed, symbols);
    }
    if (do_benchmark) {
        std::cout << "Run the dedicated benchmark binary instead: ./exchange_benchmarks\n"
                     "(see docs/performance.md for usage and how to interpret results)\n";
        return 0;
    }

    print_help();
    return 0;
}
