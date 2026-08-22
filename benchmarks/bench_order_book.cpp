#include "exchange/orderbook/order_book.hpp"
#include <benchmark/benchmark.h>

using namespace exchange::core;
using namespace exchange::orderbook;

namespace {

Order make_order(std::uint64_t id, Side side, Price price, Quantity qty) {
    Order o{};
    o.id = make_order_id(id);
    o.instrument = make_instrument_id(1);
    o.side = side;
    o.type = OrderType::Limit;
    o.tif = TimeInForce::Day;
    o.status = OrderStatus::New;
    o.price = price;
    o.quantity = qty;
    o.remaining_quantity = qty;
    o.sequence = make_seq(id);
    return o;
}

} // namespace

static void BM_OrderBookInsert(benchmark::State& state) {
    OrderBook book(make_instrument_id(1), 1 << 20);
    std::uint64_t id = 1;
    std::uint64_t inserted_since_reset = 0;
    constexpr std::uint64_t kResetThreshold = 1u << 19; // stay well under pool capacity
    for (auto _ : state) {
        if (inserted_since_reset >= kResetThreshold) {
            state.PauseTiming();
            book = OrderBook(make_instrument_id(1), 1 << 20);
            inserted_since_reset = 0;
            state.ResumeTiming();
        }
        Price price = make_price(1'000'000 + static_cast<std::int64_t>(id % 500) * 100);
        Order o = make_order(id, id % 2 == 0 ? Side::Buy : Side::Sell, price, make_qty(10));
        auto idx = book.acquire_order_slot(o);
        book.insert_resting(o, idx);
        ++id;
        ++inserted_since_reset;
    }
}
BENCHMARK(BM_OrderBookInsert);

static void BM_OrderBookCancel(benchmark::State& state) {
    OrderBook book(make_instrument_id(1), 1 << 20);
    std::vector<std::uint64_t> ids;
    for (std::uint64_t id = 1; id <= 100000; ++id) {
        Price price = make_price(1'000'000 + static_cast<std::int64_t>(id % 500) * 100);
        Order o = make_order(id, id % 2 == 0 ? Side::Buy : Side::Sell, price, make_qty(10));
        auto idx = book.acquire_order_slot(o);
        book.insert_resting(o, idx);
        ids.push_back(id);
    }
    std::size_t i = 0;
    for (auto _ : state) {
        if (i >= ids.size()) {
            state.PauseTiming();
            i = 0;
            state.ResumeTiming();
        }
        book.remove_order(make_order_id(ids[i]));
        ++i;
    }
}
BENCHMARK(BM_OrderBookCancel);

static void BM_OrderBookBestPriceLookup(benchmark::State& state) {
    OrderBook book(make_instrument_id(1), 1 << 16);
    for (std::uint64_t id = 1; id <= 1000; ++id) {
        Price price = make_price(1'000'000 + static_cast<std::int64_t>(id % 200) * 100);
        Order o = make_order(id, Side::Buy, price, make_qty(10));
        auto idx = book.acquire_order_slot(o);
        book.insert_resting(o, idx);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(book.best_bid());
    }
}
BENCHMARK(BM_OrderBookBestPriceLookup);

static void BM_OrderBookModifyReduceQuantity(benchmark::State& state) {
    OrderBook book(make_instrument_id(1), 1 << 20);
    Order o = make_order(1, Side::Buy, parse_price("100.00"), make_qty(1'000'000'000));
    auto idx = book.acquire_order_slot(o);
    book.insert_resting(o, idx);

    std::uint64_t remaining = 1'000'000'000;
    for (auto _ : state) {
        if (remaining <= 2) {
            state.PauseTiming();
            book.remove_order(make_order_id(1));
            Order fresh = make_order(1, Side::Buy, parse_price("100.00"), make_qty(1'000'000'000));
            auto fresh_idx = book.acquire_order_slot(fresh);
            book.insert_resting(fresh, fresh_idx);
            remaining = 1'000'000'000;
            state.ResumeTiming();
        }
        remaining -= 1;
        book.reduce_order_quantity(make_order_id(1), make_qty(remaining));
    }
}
BENCHMARK(BM_OrderBookModifyReduceQuantity);
