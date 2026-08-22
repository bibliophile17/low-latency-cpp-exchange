#include "exchange/protocol/binary_protocol.hpp"
#include <gtest/gtest.h>

using namespace exchange::core;
using namespace exchange::protocol;

TEST(BinaryProtocolTest, EncodeDecodeNewOrderRoundTrip) {
    NewOrderCommand cmd;
    cmd.id = make_order_id(42);
    cmd.instrument = make_instrument_id(7);
    cmd.side = Side::Buy;
    cmd.type = OrderType::Limit;
    cmd.tif = TimeInForce::Day;
    cmd.price = parse_price("185.20");
    cmd.quantity = make_qty(100);

    std::array<std::byte, 64> buf{};
    std::size_t len = encode_binary(Command{cmd}, std::span<std::byte, 64>(buf));

    std::size_t consumed = 0;
    auto decoded = decode_binary(std::span<const std::byte>(buf.data(), len), consumed);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(consumed, len);

    auto& new_order = std::get<NewOrderCommand>(*decoded);
    EXPECT_EQ(new_order.id, cmd.id);
    EXPECT_EQ(new_order.instrument, cmd.instrument);
    EXPECT_EQ(new_order.side, cmd.side);
    EXPECT_EQ(new_order.price, cmd.price);
    EXPECT_EQ(new_order.quantity, cmd.quantity);
}

TEST(BinaryProtocolTest, EncodeDecodeCancelRoundTrip) {
    CancelOrderCommand cmd;
    cmd.id = make_order_id(1);
    cmd.instrument = make_instrument_id(2);

    std::array<std::byte, 64> buf{};
    std::size_t len = encode_binary(Command{cmd}, std::span<std::byte, 64>(buf));

    std::size_t consumed = 0;
    auto decoded = decode_binary(std::span<const std::byte>(buf.data(), len), consumed);
    ASSERT_TRUE(decoded.has_value());
    auto& cancel = std::get<CancelOrderCommand>(*decoded);
    EXPECT_EQ(cancel.id, cmd.id);
    EXPECT_EQ(cancel.instrument, cmd.instrument);
}

TEST(BinaryProtocolTest, EncodeDecodeModifyRoundTrip) {
    ModifyOrderCommand cmd;
    cmd.id = make_order_id(5);
    cmd.instrument = make_instrument_id(3);
    cmd.new_quantity = make_qty(77);

    std::array<std::byte, 64> buf{};
    std::size_t len = encode_binary(Command{cmd}, std::span<std::byte, 64>(buf));

    std::size_t consumed = 0;
    auto decoded = decode_binary(std::span<const std::byte>(buf.data(), len), consumed);
    ASSERT_TRUE(decoded.has_value());
    auto& modify = std::get<ModifyOrderCommand>(*decoded);
    EXPECT_EQ(modify.new_quantity, make_qty(77));
}

TEST(BinaryProtocolTest, DecodeRejectsTruncatedBuffer) {
    NewOrderCommand cmd;
    cmd.id = make_order_id(1);
    std::array<std::byte, 64> buf{};
    std::size_t len = encode_binary(Command{cmd}, std::span<std::byte, 64>(buf));

    std::size_t consumed = 0;
    auto decoded = decode_binary(std::span<const std::byte>(buf.data(), len - 1), consumed);
    EXPECT_FALSE(decoded.has_value());
}

TEST(BinaryProtocolTest, DecodeRejectsEmptyBuffer) {
    std::size_t consumed = 0;
    auto decoded = decode_binary(std::span<const std::byte>(), consumed);
    EXPECT_FALSE(decoded.has_value());
}

TEST(BinaryProtocolTest, DecodeRejectsUnknownMessageType) {
    std::array<std::byte, 16> buf{};
    buf[0] = std::byte{0xFF};
    std::size_t consumed = 0;
    auto decoded = decode_binary(std::span<const std::byte>(buf), consumed);
    EXPECT_FALSE(decoded.has_value());
}

TEST(BinaryProtocolTest, DecodeRejectsInvalidEnumValues) {
    BinaryNewOrder msg;
    msg.side = 99; // invalid Side
    std::array<std::byte, 64> buf{};
    std::memcpy(buf.data(), &msg, sizeof(msg));
    std::size_t consumed = 0;
    auto decoded = decode_binary(std::span<const std::byte>(buf.data(), sizeof(msg)), consumed);
    EXPECT_FALSE(decoded.has_value());
}
