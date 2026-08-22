// Compact fixed-size binary message format, used for performance
// benchmarking and for the high-throughput path of the order gateway.
// Unlike the text protocol, this avoids string parsing entirely on the
// hot path.
//
// Wire format: every message starts with a 1-byte MessageType tag
// followed by a fixed-size, tightly packed payload (no padding --
// `#pragma pack` below). All multi-byte integers are host-endian; this
// is a local/loopback benchmarking protocol, not a cross-network
// standard, so endianness portability is out of scope (documented in
// docs/network-protocol.md).
#pragma once

#include "exchange/core/commands.hpp"
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace exchange::protocol {

enum class MessageType : std::uint8_t {
    NewOrder = 1,
    CancelOrder = 2,
    ModifyOrder = 3,
};

#pragma pack(push, 1)
struct BinaryNewOrder {
    MessageType type{MessageType::NewOrder};
    std::uint64_t order_id{};
    std::uint32_t instrument{};
    std::uint8_t side{};      // core::Side
    std::uint8_t order_type{}; // core::OrderType
    std::uint8_t tif{};        // core::TimeInForce
    std::int64_t price_ticks{};
    std::uint64_t quantity{};
};

struct BinaryCancelOrder {
    MessageType type{MessageType::CancelOrder};
    std::uint64_t order_id{};
    std::uint32_t instrument{};
};

struct BinaryModifyOrder {
    MessageType type{MessageType::ModifyOrder};
    std::uint64_t order_id{};
    std::uint32_t instrument{};
    std::uint64_t new_quantity{};
};
#pragma pack(pop)

static_assert(sizeof(BinaryNewOrder) == 1 + 8 + 4 + 1 + 1 + 1 + 8 + 8);
static_assert(sizeof(BinaryCancelOrder) == 1 + 8 + 4);
static_assert(sizeof(BinaryModifyOrder) == 1 + 8 + 4 + 8);

// Encodes a Command into `out` (resized as needed). Returns the number
// of bytes written.
std::size_t encode_binary(const core::Command& command, std::span<std::byte, 64> out);

// Decodes a Command from a byte buffer. `bytes` must contain at least
// one complete message; returns nullopt (and does NOT throw) on
// truncated or malformed input -- this is the boundary that receives
// untrusted network bytes, so it must never assume well-formed input.
std::optional<core::Command> decode_binary(std::span<const std::byte> bytes, std::size_t& consumed) noexcept;

} // namespace exchange::protocol
