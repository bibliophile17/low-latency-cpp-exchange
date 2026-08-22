#include "exchange/concurrency/spsc_queue.hpp"
#include <benchmark/benchmark.h>

#include <thread>

using exchange::concurrency::SpscQueue;

static void BM_SpscQueue_PushPopSingleThread(benchmark::State& state) {
    SpscQueue<std::uint64_t> q(1 << 16);
    std::uint64_t v = 0;
    for (auto _ : state) {
        q.try_push(v);
        auto popped = q.try_pop();
        benchmark::DoNotOptimize(popped);
        ++v;
    }
}
BENCHMARK(BM_SpscQueue_PushPopSingleThread);

// Two-thread producer/consumer throughput. Reports items/sec via
// SetItemsProcessed; this is the realistic usage pattern (see
// concurrency/spsc_queue.hpp for why SPSC is the right structure here).
static void BM_SpscQueue_ProducerConsumerThroughput(benchmark::State& state) {
    const std::uint64_t total = static_cast<std::uint64_t>(state.range(0));

    for (auto _ : state) {
        SpscQueue<std::uint64_t> q(1 << 16);
        std::thread producer([&] {
            for (std::uint64_t i = 0; i < total; ++i) {
                while (!q.try_push(i)) { /* spin */ }
            }
        });
        std::uint64_t received = 0;
        std::thread consumer([&] {
            while (received < total) {
                if (q.try_pop()) ++received;
            }
        });
        producer.join();
        consumer.join();
    }
    state.SetItemsProcessed(state.iterations() * total);
}
BENCHMARK(BM_SpscQueue_ProducerConsumerThroughput)->Arg(10000)->Arg(100000)->Unit(benchmark::kMillisecond);
