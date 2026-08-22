#include "exchange/replay/replay_engine.hpp"
#include "exchange/protocol/binary_protocol.hpp"

#include <array>
#include <stdexcept>

namespace exchange::replay {

// Each record on disk: 4-byte little/host-endian length prefix followed
// by that many bytes of binary-protocol-encoded command. A length
// prefix (rather than relying on fixed message sizes) keeps the file
// format stable even though NewOrder/Cancel/Modify encode to different
// sizes.

RecordWriter::RecordWriter(const std::string& path)
    : out_(path, std::ios::binary | std::ios::trunc) {
    if (!out_) throw std::runtime_error("RecordWriter: failed to open " + path);
}

RecordWriter::~RecordWriter() { out_.flush(); }

void RecordWriter::write(const core::Command& command) {
    std::array<std::byte, 64> buf{};
    std::size_t len = protocol::encode_binary(command, std::span<std::byte, 64>(buf));
    auto len32 = static_cast<std::uint32_t>(len);
    out_.write(reinterpret_cast<const char*>(&len32), sizeof(len32));
    out_.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(len));
    ++count_;
}

RecordReader::RecordReader(const std::string& path)
    : in_(path, std::ios::binary) {}

std::optional<core::Command> RecordReader::next() {
    if (!in_) return std::nullopt;

    std::uint32_t len32 = 0;
    in_.read(reinterpret_cast<char*>(&len32), sizeof(len32));
    if (!in_ || in_.gcount() != sizeof(len32)) return std::nullopt; // EOF

    if (len32 == 0 || len32 > 64) return std::nullopt; // corrupt record; stop replay defensively

    std::array<std::byte, 64> buf{};
    in_.read(reinterpret_cast<char*>(buf.data()), len32);
    if (!in_ || static_cast<std::uint32_t>(in_.gcount()) != len32) return std::nullopt;

    std::size_t consumed = 0;
    auto command = protocol::decode_binary(std::span<const std::byte>(buf.data(), len32), consumed);
    if (!command) return std::nullopt;

    ++count_;
    return command;
}

} // namespace exchange::replay
