#include "exchange/concurrency/spsc_queue.hpp"
#include <gtest/gtest.h>

#include <thread>
#include <vector>

using exchange::concurrency::SpscQueue;

TEST(SpscQueueTest, PushPopSingleItem) {
    SpscQueue<int> q(8);
    EXPECT_TRUE(q.try_push(42));
    auto v = q.try_pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST(SpscQueueTest, PopEmptyReturnsNullopt) {
    SpscQueue<int> q(8);
    EXPECT_FALSE(q.try_pop().has_value());
}

TEST(SpscQueueTest, FillsToCapacity) {
    SpscQueue<int> q(4); // rounds up to power of two internally; usable capacity is capacity-1
    std::size_t pushed = 0;
    while (q.try_push(static_cast<int>(pushed))) ++pushed;
    EXPECT_GT(pushed, 0u);
    EXPECT_FALSE(q.try_push(999)); // full
}

TEST(SpscQueueTest, FifoOrderPreserved) {
    SpscQueue<int> q(64);
    for (int i = 0; i < 10; ++i) EXPECT_TRUE(q.try_push(i));
    for (int i = 0; i < 10; ++i) {
        auto v = q.try_pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
}

TEST(SpscQueueTest, ConcurrentProducerConsumer) {
    constexpr int kCount = 100'000;
    SpscQueue<int> q(1024);

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            while (!q.try_push(i)) { /* spin */ }
        }
    });

    std::vector<int> received;
    received.reserve(kCount);
    std::thread consumer([&] {
        while (static_cast<int>(received.size()) < kCount) {
            if (auto v = q.try_pop()) received.push_back(*v);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(received[i], i);
    }
}
