#include "exchange/core/types.hpp"
#include <gtest/gtest.h>

using namespace exchange::core;

TEST(PriceTest, ParseAndFormatRoundTrip) {
    EXPECT_EQ(format_price(parse_price("185.20")), "185.2000");
    EXPECT_EQ(format_price(parse_price("0.01")), "0.0100");
    EXPECT_EQ(format_price(parse_price("100")), "100.0000");
    EXPECT_EQ(format_price(parse_price("-5.5")), "-5.5000");
}

TEST(PriceTest, TicksExact) {
    Price p = parse_price("185.20");
    EXPECT_EQ(ticks_of(p), 1852000);
}

TEST(PriceTest, Comparison) {
    EXPECT_LT(parse_price("100.00"), parse_price("100.01"));
    EXPECT_GT(parse_price("100.01"), parse_price("100.00"));
    EXPECT_EQ(parse_price("100.00"), parse_price("100.00"));
}

TEST(PriceTest, InvalidThrows) {
    EXPECT_THROW(parse_price(""), std::invalid_argument);
    EXPECT_THROW(parse_price("abc"), std::invalid_argument);
    EXPECT_THROW(parse_price("1.2.3"), std::invalid_argument);
}

TEST(QuantityTest, Arithmetic) {
    Quantity a = make_qty(100);
    Quantity b = make_qty(30);
    EXPECT_EQ(value_of(a - b), 70u);
    EXPECT_EQ(value_of(a + b), 130u);
}

TEST(SideTest, Opposite) {
    EXPECT_EQ(opposite(Side::Buy), Side::Sell);
    EXPECT_EQ(opposite(Side::Sell), Side::Buy);
}

TEST(ToStringTest, AllEnums) {
    EXPECT_EQ(to_string(Side::Buy), "BUY");
    EXPECT_EQ(to_string(OrderType::Market), "MARKET");
    EXPECT_EQ(to_string(TimeInForce::IOC), "IOC");
    EXPECT_EQ(to_string(OrderStatus::Filled), "FILLED");
}
