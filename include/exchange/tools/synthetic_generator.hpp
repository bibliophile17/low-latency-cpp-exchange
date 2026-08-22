// Synthetic order-flow generator.
//
// Generates a locally-produced, reproducible (seeded) stream of NEW /
// CANCEL / MODIFY commands for benchmarking, stress-testing, and demo
// purposes. This is the ONLY source of order flow used anywhere in this
// project -- there is no dependency on any live exchange, broker, or
// financial data feed, scraped or otherwise. See top-level README
// disclaimer.
#pragma once

#include "exchange/core/commands.hpp"
#include <cstdint>
#include <random>
#include <vector>

namespace exchange::tools {

struct GeneratorConfig {
    std::uint64_t seed = 42;
    std::uint64_t order_count = 10'000;
    std::vector<core::InstrumentId> instruments;

    core::Price mid_price = core::make_price(1'850'000); // 185.0000
    core::Price tick_size = core::make_price(100);        // 0.0100
    int price_spread_ticks = 50; // random walk band around mid price

    double market_order_fraction = 0.05;
    double cancel_fraction = 0.15;  // fraction of *previously placed* orders that get cancelled
    double modify_fraction = 0.05;
    double ioc_fraction = 0.10;

    std::uint64_t min_qty = 1;
    std::uint64_t max_qty = 500;

    // Bursty traffic: with probability burst_probability, emit a burst
    // of burst_size commands with no "thinking time" between them
    // (this generator does not model wall-clock timing directly --
    // bursts are expressed structurally, by clustering many commands
    // for the same instrument/price back-to-back).
    double burst_probability = 0.02;
    std::uint32_t burst_size = 20;
};

// Deterministic: the same seed + config always produces the same
// sequence of commands, verified by tests/replay determinism tests.
class SyntheticOrderGenerator {
public:
    explicit SyntheticOrderGenerator(GeneratorConfig config);

    // Generates the next command in the stream. IDs for NEW orders are
    // assigned sequentially starting at 1. Cancel/Modify reference a
    // previously-generated (and not-yet-cancelled) NEW order id when
    // possible.
    core::Command next();

    [[nodiscard]] std::uint64_t generated_count() const noexcept { return generated_; }

private:
    GeneratorConfig config_;
    std::mt19937_64 rng_;
    std::uint64_t next_order_id_ = 1;
    std::uint64_t generated_ = 0;

    std::vector<std::uint64_t> live_order_ids_; // ids we've generated that haven't been cancelled (best-effort tracking)
    std::int64_t walk_offset_ticks_ = 0;

    std::uint32_t burst_remaining_ = 0;

    core::Command make_new_order();
    core::Command make_cancel();
    core::Command make_modify();
};

} // namespace exchange::tools
