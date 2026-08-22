// Replay engine.
//
// Records every Command the exchange processes to a flat binary file
// using the same fixed-size binary message format as
// protocol/binary_protocol.hpp (so recording adds negligible overhead:
// it's the same encode already needed for the binary wire protocol).
// Replaying reads the file back and feeds the same Commands through a
// fresh MatchingEngine in the same order, which -- because the engine
// is deterministic (see matching/matching_engine.hpp) -- reproduces
// identical trades and book states. This underpins:
//   - debugging: capture a production/test session, replay locally
//   - regression testing: replay a fixed input, diff the resulting
//     trade/event stream against a golden output
//   - performance testing: replay a large realistic order flow while
//     measuring latency, instead of needing a live network source
#pragma once

#include "exchange/core/commands.hpp"
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>

namespace exchange::replay {

class RecordWriter {
public:
    explicit RecordWriter(const std::string& path);
    ~RecordWriter();

    void write(const core::Command& command);
    [[nodiscard]] std::uint64_t records_written() const noexcept { return count_; }

private:
    std::ofstream out_;
    std::uint64_t count_ = 0;
};

class RecordReader {
public:
    explicit RecordReader(const std::string& path);

    // Returns the next command, or nullopt at end of file.
    std::optional<core::Command> next();

    [[nodiscard]] std::uint64_t records_read() const noexcept { return count_; }
    [[nodiscard]] bool is_open() const noexcept { return in_.is_open(); }

private:
    std::ifstream in_;
    std::uint64_t count_ = 0;
};

} // namespace exchange::replay
