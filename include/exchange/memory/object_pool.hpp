// Fixed-capacity object pool.
//
// Purpose: the matching engine's hot path (accepting/cancelling/matching
// orders) must not call malloc/free. This pool pre-allocates a flat
// std::vector<T> of a fixed capacity up front (at startup) and hands out
// slots by index. A free-list (also index-based, stored inline in the
// unused slots) tracks reclaimed slots so they can be reused without any
// allocation.
//
// Using indices instead of pointers:
//   - keeps Order (see core/order.hpp) storage contiguous, which is more
//     cache-friendly than scattered heap allocations,
//   - lets the free-list reuse the same storage as the (currently free)
//     objects instead of needing a separate structure,
//   - makes the pool trivially relocatable/serializable, which is useful
//     for the replay engine and for tests.
//
// This is intentionally simple: single-threaded, no synchronization. The
// matching engine is single-threaded on its critical path by design (see
// docs/concurrency.md), so the pool does not need to be thread-safe.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace exchange::memory {

template <typename T>
class ObjectPool {
public:
    static constexpr std::uint32_t kInvalidIndex = UINT32_MAX;

    explicit ObjectPool(std::size_t capacity) {
        storage_.resize(capacity);
        free_indices_.reserve(capacity);
        // Populate free list back-to-front so index 0 is handed out first.
        for (std::size_t i = capacity; i-- > 0;) {
            free_indices_.push_back(static_cast<std::uint32_t>(i));
        }
    }

    // Acquire a slot; returns its index. O(1), no allocation.
    // Throws std::length_error if the pool is exhausted -- callers on the
    // hot path should size the pool generously at startup and treat
    // exhaustion as a configuration error, not a runtime condition to
    // recover from order-by-order.
    [[nodiscard]] std::uint32_t acquire() {
        if (free_indices_.empty()) {
            throw std::length_error("ObjectPool exhausted: increase pool capacity");
        }
        std::uint32_t idx = free_indices_.back();
        free_indices_.pop_back();
        ++live_count_;
        return idx;
    }

    // Return a slot to the pool. O(1), no allocation (vector already has
    // capacity reserved).
    void release(std::uint32_t index) {
        free_indices_.push_back(index);
        --live_count_;
    }

    [[nodiscard]] T& operator[](std::uint32_t index) { return storage_[index]; }
    [[nodiscard]] const T& operator[](std::uint32_t index) const { return storage_[index]; }

    [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] std::size_t live_count() const noexcept { return live_count_; }
    [[nodiscard]] std::size_t free_count() const noexcept { return free_indices_.size(); }

private:
    std::vector<T> storage_;
    std::vector<std::uint32_t> free_indices_;
    std::size_t live_count_ = 0;
};

} // namespace exchange::memory
