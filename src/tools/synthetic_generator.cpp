#include "exchange/tools/synthetic_generator.hpp"

#include <algorithm>

namespace exchange::tools {

using namespace exchange::core;

SyntheticOrderGenerator::SyntheticOrderGenerator(GeneratorConfig config)
    : config_(std::move(config)), rng_(config_.seed) {
    if (config_.instruments.empty()) {
        config_.instruments.push_back(make_instrument_id(1));
    }
}

Command SyntheticOrderGenerator::make_new_order() {
    std::uniform_int_distribution<std::size_t> instrument_dist(0, config_.instruments.size() - 1);
    std::uniform_int_distribution<std::uint64_t> qty_dist(config_.min_qty, config_.max_qty);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> walk_dist(-1, 1);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    NewOrderCommand cmd;
    cmd.id = make_order_id(next_order_id_++);
    cmd.instrument = config_.instruments[instrument_dist(rng_)];
    cmd.side = side_dist(rng_) == 0 ? Side::Buy : Side::Sell;
    cmd.quantity = make_qty(qty_dist(rng_));

    bool is_market = unit(rng_) < config_.market_order_fraction;
    if (is_market) {
        cmd.type = OrderType::Market;
        cmd.tif = TimeInForce::IOC;
        cmd.price = make_price(0);
    } else {
        cmd.type = OrderType::Limit;
        // Random walk around mid price, clamped to configured band, so
        // the book has a realistic mix of price levels rather than
        // every order at a single price.
        walk_offset_ticks_ += walk_dist(rng_);
        int band = config_.price_spread_ticks;
        walk_offset_ticks_ = std::clamp<std::int64_t>(walk_offset_ticks_, -band, band);

        std::int64_t side_bias = (cmd.side == Side::Buy) ? -1 : 1; // buys slightly below mid, sells slightly above
        std::uniform_int_distribution<int> jitter_dist(0, band);
        std::int64_t offset_ticks = walk_offset_ticks_ + side_bias * jitter_dist(rng_);

        Price price = make_price(ticks_of(config_.mid_price) + offset_ticks * ticks_of(config_.tick_size));
        cmd.price = price;

        cmd.tif = unit(rng_) < config_.ioc_fraction ? TimeInForce::IOC : TimeInForce::Day;
    }

    if (cmd.tif == TimeInForce::Day) {
        live_order_ids_.push_back(value_of(cmd.id));
    }
    return Command{cmd};
}

Command SyntheticOrderGenerator::make_cancel() {
    if (live_order_ids_.empty()) return make_new_order();

    std::uniform_int_distribution<std::size_t> idx_dist(0, live_order_ids_.size() - 1);
    std::size_t idx = idx_dist(rng_);
    std::uint64_t id = live_order_ids_[idx];
    live_order_ids_[idx] = live_order_ids_.back();
    live_order_ids_.pop_back();

    std::uniform_int_distribution<std::size_t> instrument_dist(0, config_.instruments.size() - 1);
    CancelOrderCommand cmd;
    cmd.id = make_order_id(id);
    cmd.instrument = config_.instruments[instrument_dist(rng_)]; // best-effort; generator doesn't track per-id instrument
    return Command{cmd};
}

Command SyntheticOrderGenerator::make_modify() {
    if (live_order_ids_.empty()) return make_new_order();

    std::uniform_int_distribution<std::size_t> idx_dist(0, live_order_ids_.size() - 1);
    std::uint64_t id = live_order_ids_[idx_dist(rng_)];

    std::uniform_int_distribution<std::uint64_t> qty_dist(1, std::max<std::uint64_t>(1, config_.min_qty));
    std::uniform_int_distribution<std::size_t> instrument_dist(0, config_.instruments.size() - 1);

    ModifyOrderCommand cmd;
    cmd.id = make_order_id(id);
    cmd.instrument = config_.instruments[instrument_dist(rng_)];
    cmd.new_quantity = make_qty(qty_dist(rng_));
    return Command{cmd};
}

Command SyntheticOrderGenerator::next() {
    ++generated_;

    if (burst_remaining_ > 0) {
        --burst_remaining_;
        return make_new_order();
    }

    std::uniform_real_distribution<double> unit(0.0, 1.0);
    double roll = unit(rng_);

    if (unit(rng_) < config_.burst_probability) {
        burst_remaining_ = config_.burst_size;
    }

    if (roll < config_.cancel_fraction) return make_cancel();
    if (roll < config_.cancel_fraction + config_.modify_fraction) return make_modify();
    return make_new_order();
}

} // namespace exchange::tools
