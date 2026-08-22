#include "exchange/protocol/binary_protocol.hpp"

namespace exchange::protocol {

using namespace exchange::core;

std::size_t encode_binary(const Command& command, std::span<std::byte, 64> out) {
    return std::visit([&](auto&& cmd) -> std::size_t {
        using T = std::decay_t<decltype(cmd)>;
        if constexpr (std::is_same_v<T, NewOrderCommand>) {
            BinaryNewOrder msg;
            msg.order_id = value_of(cmd.id);
            msg.instrument = value_of(cmd.instrument);
            msg.side = static_cast<std::uint8_t>(cmd.side);
            msg.order_type = static_cast<std::uint8_t>(cmd.type);
            msg.tif = static_cast<std::uint8_t>(cmd.tif);
            msg.price_ticks = ticks_of(cmd.price);
            msg.quantity = value_of(cmd.quantity);
            std::memcpy(out.data(), &msg, sizeof(msg));
            return sizeof(msg);
        } else if constexpr (std::is_same_v<T, CancelOrderCommand>) {
            BinaryCancelOrder msg;
            msg.order_id = value_of(cmd.id);
            msg.instrument = value_of(cmd.instrument);
            std::memcpy(out.data(), &msg, sizeof(msg));
            return sizeof(msg);
        } else { // ModifyOrderCommand
            BinaryModifyOrder msg;
            msg.order_id = value_of(cmd.id);
            msg.instrument = value_of(cmd.instrument);
            msg.new_quantity = value_of(cmd.new_quantity);
            std::memcpy(out.data(), &msg, sizeof(msg));
            return sizeof(msg);
        }
    }, command);
}

std::optional<Command> decode_binary(std::span<const std::byte> bytes, std::size_t& consumed) noexcept {
    consumed = 0;
    if (bytes.empty()) return std::nullopt;

    auto type = static_cast<MessageType>(bytes[0]);
    switch (type) {
        case MessageType::NewOrder: {
            if (bytes.size() < sizeof(BinaryNewOrder)) return std::nullopt;
            BinaryNewOrder msg{};
            std::memcpy(&msg, bytes.data(), sizeof(msg));
            consumed = sizeof(msg);

            if (msg.side > 1 || msg.order_type > 1 || msg.tif > 2) return std::nullopt; // malformed enum
            NewOrderCommand cmd;
            cmd.id = make_order_id(msg.order_id);
            cmd.instrument = make_instrument_id(msg.instrument);
            cmd.side = static_cast<Side>(msg.side);
            cmd.type = static_cast<OrderType>(msg.order_type);
            cmd.tif = static_cast<TimeInForce>(msg.tif);
            cmd.price = make_price(msg.price_ticks);
            cmd.quantity = make_qty(msg.quantity);
            return Command{cmd};
        }
        case MessageType::CancelOrder: {
            if (bytes.size() < sizeof(BinaryCancelOrder)) return std::nullopt;
            BinaryCancelOrder msg{};
            std::memcpy(&msg, bytes.data(), sizeof(msg));
            consumed = sizeof(msg);
            CancelOrderCommand cmd;
            cmd.id = make_order_id(msg.order_id);
            cmd.instrument = make_instrument_id(msg.instrument);
            return Command{cmd};
        }
        case MessageType::ModifyOrder: {
            if (bytes.size() < sizeof(BinaryModifyOrder)) return std::nullopt;
            BinaryModifyOrder msg{};
            std::memcpy(&msg, bytes.data(), sizeof(msg));
            consumed = sizeof(msg);
            ModifyOrderCommand cmd;
            cmd.id = make_order_id(msg.order_id);
            cmd.instrument = make_instrument_id(msg.instrument);
            cmd.new_quantity = make_qty(msg.new_quantity);
            return Command{cmd};
        }
        default:
            return std::nullopt; // unknown/malformed message type
    }
}

} // namespace exchange::protocol
