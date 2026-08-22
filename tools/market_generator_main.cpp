// Standalone synthetic order-flow generator CLI.
//
// Usage:
//   market_generator --orders 1000000 --seed 12345 [--out orders.bin] [--symbols AAPL,MSFT]
//
// With the same seed and configuration, this always produces the same
// sequence of commands (verified in tests/test_replay.cpp).
#include "exchange/gateway/symbol_table.hpp"
#include "exchange/protocol/text_protocol.hpp"
#include "exchange/replay/replay_engine.hpp"
#include "exchange/tools/synthetic_generator.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace exchange;

    std::uint64_t order_count = 10'000;
    std::uint64_t seed = 42;
    std::string out_file;
    std::vector<std::string> symbols;

    std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--orders" && i + 1 < args.size()) order_count = std::stoull(args[++i]);
        else if (args[i] == "--seed" && i + 1 < args.size()) seed = std::stoull(args[++i]);
        else if (args[i] == "--out" && i + 1 < args.size()) out_file = args[++i];
        else if (args[i] == "--symbols" && i + 1 < args.size()) {
            std::string csv = args[++i];
            std::size_t pos = 0;
            while (pos < csv.size()) {
                std::size_t comma = csv.find(',', pos);
                symbols.push_back(csv.substr(pos, comma - pos));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (args[i] == "--help") {
            std::cout << "Usage: market_generator --orders N --seed S [--out file.bin] [--symbols A,B,C]\n";
            return 0;
        }
    }

    gateway::SymbolTable symbol_table;
    std::vector<core::InstrumentId> instrument_ids;
    for (const auto& s : (symbols.empty() ? std::vector<std::string>{"AAPL", "MSFT", "GOOG"} : symbols)) {
        instrument_ids.push_back(symbol_table.intern(s));
    }

    tools::GeneratorConfig config;
    config.seed = seed;
    config.order_count = order_count;
    config.instruments = instrument_ids;
    tools::SyntheticOrderGenerator generator(config);

    std::cout << "Generating " << order_count << " synthetic commands (seed=" << seed << ")...\n";

    if (!out_file.empty()) {
        replay::RecordWriter writer(out_file);
        for (std::uint64_t i = 0; i < order_count; ++i) {
            writer.write(generator.next());
        }
        std::cout << "Wrote " << writer.records_written() << " commands to " << out_file << "\n";
    } else {
        // No --out: print to stdout in text-protocol form, capped to
        // avoid flooding the terminal for large counts.
        std::uint64_t print_limit = std::min<std::uint64_t>(order_count, 1000);
        for (std::uint64_t i = 0; i < order_count; ++i) {
            auto cmd = generator.next();
            if (i < print_limit) {
                std::cout << protocol::format_text_command(cmd, "SYM") << "\n";
            }
        }
        if (order_count > print_limit) {
            std::cout << "... (" << (order_count - print_limit) << " more commands not printed; use --out to write full stream)\n";
        }
    }
    return 0;
}
