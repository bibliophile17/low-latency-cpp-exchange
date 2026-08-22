#include "exchange/protocol/text_protocol.hpp"
#include <gtest/gtest.h>

using namespace exchange::core;
using namespace exchange::protocol;

namespace {
InstrumentId resolve(std::string_view) { return make_instrument_id(1); }
}

TEST(TextProtocolTest, ParsesNewLimitBuy) {
    ParseError err;
    auto cmd = parse_text_command("NEW BUY AAPL 100 185.20", resolve, make_order_id(1), err);
    ASSERT_TRUE(cmd.has_value());
    auto& new_order = std::get<NewOrderCommand>(*cmd);
    EXPECT_EQ(new_order.side, Side::Buy);
    EXPECT_EQ(new_order.type, OrderType::Limit);
    EXPECT_EQ(new_order.quantity, make_qty(100));
    EXPECT_EQ(new_order.price, parse_price("185.20"));
}

TEST(TextProtocolTest, ParsesNewLimitSellWithIOC) {
    ParseError err;
    auto cmd = parse_text_command("NEW SELL AAPL 50 185.25 IOC", resolve, make_order_id(2), err);
    ASSERT_TRUE(cmd.has_value());
    auto& new_order = std::get<NewOrderCommand>(*cmd);
    EXPECT_EQ(new_order.tif, TimeInForce::IOC);
}

TEST(TextProtocolTest, ParsesMarketOrder) {
    ParseError err;
    auto cmd = parse_text_command("NEW MARKET BUY AAPL 20", resolve, make_order_id(3), err);
    ASSERT_TRUE(cmd.has_value());
    auto& new_order = std::get<NewOrderCommand>(*cmd);
    EXPECT_EQ(new_order.type, OrderType::Market);
    EXPECT_EQ(new_order.tif, TimeInForce::IOC);
}

TEST(TextProtocolTest, ParsesCancel) {
    ParseError err;
    auto cmd = parse_text_command("CANCEL 12345 AAPL", resolve, make_order_id(0), err);
    ASSERT_TRUE(cmd.has_value());
    auto& cancel = std::get<CancelOrderCommand>(*cmd);
    EXPECT_EQ(cancel.id, make_order_id(12345));
}

TEST(TextProtocolTest, ParsesModify) {
    ParseError err;
    auto cmd = parse_text_command("MODIFY 12346 AAPL 200", resolve, make_order_id(0), err);
    ASSERT_TRUE(cmd.has_value());
    auto& modify = std::get<ModifyOrderCommand>(*cmd);
    EXPECT_EQ(modify.id, make_order_id(12346));
    EXPECT_EQ(modify.new_quantity, make_qty(200));
}

TEST(TextProtocolTest, RejectsEmptyLine) {
    ParseError err;
    auto cmd = parse_text_command("", resolve, make_order_id(1), err);
    EXPECT_FALSE(cmd.has_value());
}

TEST(TextProtocolTest, RejectsUnknownVerb) {
    ParseError err;
    auto cmd = parse_text_command("FROB AAPL", resolve, make_order_id(1), err);
    EXPECT_FALSE(cmd.has_value());
}

TEST(TextProtocolTest, RejectsInvalidSide) {
    ParseError err;
    auto cmd = parse_text_command("NEW HOLD AAPL 10 100.00", resolve, make_order_id(1), err);
    EXPECT_FALSE(cmd.has_value());
}

TEST(TextProtocolTest, RejectsInvalidQuantity) {
    ParseError err;
    auto cmd = parse_text_command("NEW BUY AAPL abc 100.00", resolve, make_order_id(1), err);
    EXPECT_FALSE(cmd.has_value());
}

TEST(TextProtocolTest, RejectsZeroQuantity) {
    ParseError err;
    auto cmd = parse_text_command("NEW BUY AAPL 0 100.00", resolve, make_order_id(1), err);
    EXPECT_FALSE(cmd.has_value());
}

TEST(TextProtocolTest, RejectsInvalidPrice) {
    ParseError err;
    auto cmd = parse_text_command("NEW BUY AAPL 10 abc", resolve, make_order_id(1), err);
    EXPECT_FALSE(cmd.has_value());
}

TEST(TextProtocolTest, RejectsOversizedLine) {
    ParseError err;
    std::string huge(1000, 'x');
    auto cmd = parse_text_command(huge, resolve, make_order_id(1), err);
    EXPECT_FALSE(cmd.has_value());
}

TEST(TextProtocolTest, RoundTripFormatting) {
    NewOrderCommand cmd;
    cmd.id = make_order_id(1);
    cmd.instrument = make_instrument_id(1);
    cmd.side = Side::Buy;
    cmd.type = OrderType::Limit;
    cmd.tif = TimeInForce::Day;
    cmd.price = parse_price("185.20");
    cmd.quantity = make_qty(100);

    std::string text = format_text_command(Command{cmd}, "AAPL");
    EXPECT_EQ(text, "NEW BUY AAPL 100 185.2000");
}
