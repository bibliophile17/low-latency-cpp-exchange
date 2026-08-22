// Latency measurement utilities.
//
// LatencyRecorder collects raw sample nanosecond durations into a
// std::vector<uint64_t> (pre-reserved to avoid reallocation on the hot
// path being measured) and computes percentiles by sorting once at
// report time. This is intentionally simple: for the sample counts this
// project benchmarks (up to low millions), sort-based percentiles are
// fast enough to compute post-hoc and are exact, unlike a streaming
// approximate-histogram approach (e.g. HDRHistogram-style bucketing),
// which would be the right choice at much higher sample counts or when
// memory for raw samples isn't available. That tradeoff is documented
// in docs/performance.md.
//
// IMPORTANT: recording a sample (record()) must be extremely cheap
// (a timestamp read + a vector push_back into pre-reserved capacity)
// since it is called from latency-sensitive code paths. Percentile
// computation (percentile()/report()) is NOT cheap and must only be
// called after measurement is complete, never interleaved with
// recording on the hot path.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace exchange::metrics {

inline std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

struct LatencyReport {
    std::uint64_t count{};
    double throughput_ops_per_sec{};
    std::uint64_t min_ns{};
    std::uint64_t max_ns{};
    std::uint64_t mean_ns{};
    std::uint64_t p50_ns{};
    std::uint64_t p90_ns{};
    std::uint64_t p95_ns{};
    std::uint64_t p99_ns{};
    std::uint64_t p999_ns{};
};

class LatencyRecorder {
public:
    explicit LatencyRecorder(std::size_t expected_samples = 1 << 20) {
        samples_.reserve(expected_samples);
    }

    void record(std::uint64_t duration_ns) { samples_.push_back(duration_ns); }

    [[nodiscard]] std::size_t sample_count() const noexcept { return samples_.size(); }

    void clear() { samples_.clear(); }

    // Sorts internal storage (destructive w.r.t. ordering, not values)
    // and computes a full report, including throughput derived from
    // wall-clock span if `wall_clock_seconds` > 0.
    [[nodiscard]] LatencyReport report(double wall_clock_seconds = 0.0) {
        LatencyReport r{};
        r.count = samples_.size();
        if (samples_.empty()) return r;

        std::sort(samples_.begin(), samples_.end());

        r.min_ns = samples_.front();
        r.max_ns = samples_.back();

        std::uint64_t sum = 0;
        for (auto v : samples_) sum += v;
        r.mean_ns = sum / samples_.size();

        r.p50_ns = percentile_of_sorted(0.50);
        r.p90_ns = percentile_of_sorted(0.90);
        r.p95_ns = percentile_of_sorted(0.95);
        r.p99_ns = percentile_of_sorted(0.99);
        r.p999_ns = percentile_of_sorted(0.999);

        if (wall_clock_seconds > 0.0) {
            r.throughput_ops_per_sec = static_cast<double>(samples_.size()) / wall_clock_seconds;
        }
        return r;
    }

private:
    // Requires samples_ already sorted.
    [[nodiscard]] std::uint64_t percentile_of_sorted(double p) const {
        if (samples_.empty()) return 0;
        double idx_f = p * static_cast<double>(samples_.size() - 1);
        std::size_t idx = static_cast<std::size_t>(idx_f);
        return samples_[idx];
    }

    std::vector<std::uint64_t> samples_;
};

// RAII scope timer: records elapsed nanoseconds into `recorder` on
// destruction. Convenience for benchmarks/tests; not used inside the
// matching engine's own hot path (which times explicitly at call sites
// to distinguish queue/processing/publish latency -- see docs/performance.md).
class ScopedTimer {
public:
    explicit ScopedTimer(LatencyRecorder& recorder) : recorder_(recorder), start_(now_ns()) {}
    ~ScopedTimer() { recorder_.record(now_ns() - start_); }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    LatencyRecorder& recorder_;
    std::uint64_t start_;
};

} // namespace exchange::metrics
