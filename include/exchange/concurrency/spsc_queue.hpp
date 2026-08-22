// Single-producer / single-consumer lock-free ring buffer.
//
// Why a ring buffer: it lets one thread (e.g. the network/gateway
// thread) hand work to another thread (e.g. the matching engine thread)
// without the consumer blocking the producer or vice versa, and without
// per-item heap allocation -- the backing store is a single fixed-size
// array allocated once.
//
// Why SPSC specifically is simpler than MPMC:
//   - With exactly one producer and one consumer, `head` (next write
//     slot) is only ever written by the producer and only ever read by
//     the consumer; `tail` (next read slot) is only ever written by the
//     consumer and only ever read by the producer. Neither index is
//     ever contended for *writing* by more than one thread, so no
//     compare-and-swap / retry loop is needed to advance an index --
//     a plain atomic store suffices.
//   - MPMC queues need CAS loops (or per-slot sequence numbers, as in
//     the classic Vyukov MPMC queue) to arbitrate which of several
//     producers "wins" a slot, and similarly for consumers. That adds
//     branches, retries, and generally worse tail latency under
//     contention. SPSC sidesteps all of that by construction.
//
// Why acquire/release ordering is sufficient (and correct) here:
//   - The producer publishes an item by (1) writing the item into the
//     slot, then (2) storing the new `head` with memory_order_release.
//   - The consumer checks `head` with memory_order_acquire before
//     reading the slot's contents.
//   - The release store on `head` cannot be reordered before the plain
//     write of the item (release semantics forbid earlier writes from
//     moving past it), and the acquire load of `head` cannot be
//     reordered after the subsequent read of the slot (acquire
//     semantics forbid later reads from moving before it). Together
//     this establishes a happens-before edge from "producer wrote the
//     item" to "consumer reads the item", which is exactly the
//     guarantee needed -- nothing stronger (e.g. seq_cst) is required
//     because there is no need for a *total* order across multiple
//     variables/threads, only a pairwise producer->consumer handoff per
//     slot.
//   - `tail` is published symmetrically by the consumer (release) and
//     read by the producer (acquire) so the producer knows when a slot
//     has been freed and can be reused.
//
// False sharing avoidance:
//   - `head_` and `tail_` are each placed on their own cache line via
//     `alignas(kCacheLineSize)`, with padding between them. Without
//     this, the producer's frequent writes to `head_` and the
//     consumer's frequent writes to `tail_` would invalidate each
//     other's cache line on every operation even though the two
//     variables are logically independent -- a classic false-sharing
//     pattern that can dominate latency in a queue like this.
//   - The underlying storage array is also kept separate from both
//     index variables' cache lines.
#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace exchange::concurrency {

inline constexpr std::size_t kCacheLineSize = 64;

template <typename T>
class SpscQueue {
public:
    // capacity is rounded up internally to a power of two so index
    // wrap-around can use a fast bitmask instead of a modulo division.
    explicit SpscQueue(std::size_t capacity) : mask_(next_pow2(capacity) - 1), buffer_(mask_ + 1) {}

    // Returns false if the queue is full (producer side only).
    bool try_push(T item) {
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = (head + 1) & mask_;
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[head] = std::move(item);
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    // Returns nullopt if the queue is empty (consumer side only).
    std::optional<T> try_pop() {
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt; // empty
        }
        T item = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return item;
    }

    // Approximate size; safe to call from either thread for
    // monitoring/metrics, but may be stale by the time it's read.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        std::size_t head = head_.load(std::memory_order_acquire);
        std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & mask_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return mask_ + 1; }

private:
    static std::size_t next_pow2(std::size_t v) {
        std::size_t p = 1;
        while (p < v) p <<= 1;
        return p == 0 ? 1 : p;
    }

    const std::size_t mask_;
    std::vector<T> buffer_;

    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0}; // producer-owned
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0}; // consumer-owned
    // Trailing padding so a subsequent member (or a heap-neighbor
    // allocation) doesn't share tail_'s cache line either.
    char padding_[kCacheLineSize - sizeof(std::atomic<std::size_t>)];
};

} // namespace exchange::concurrency
