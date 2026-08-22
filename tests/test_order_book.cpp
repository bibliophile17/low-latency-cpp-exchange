#include "exchange/orderbook/order_book.hpp"
#include <gtest/gtest.h>

using namespace exchange::core;
using namespace exchange::orderbook;

namespace {

Order make_order(std::uint64_t id, Side side, Price price, Quantity qty, std::uint64_t seq) {
    Order o{};
    o.id = make_order_id(id);
    o.instrument = make_instrument_id(1);
    o.side = side;
    o.type = OrderType::Limit;
    o.tif = TimeInForce::Day;
    o.status = OrderStatus::New;
    o.price = price;
    o.quantity = qty;
    o.remaining_quantity = qty;
    o.sequence = make_seq(seq);
    return o;
}

} // namespace

TEST(OrderBookTest, EmptyBookHasNoBestPrices) {
    OrderBook book(make_instrument_id(1));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookTest, InsertSingleBid) {
    OrderBook book(make_instrument_id(1));
    Order o = make_order(1, Side::Buy, parse_price("100.00"), make_qty(10), 1);
    auto idx = book.acquire_order_slot(o);
    book.insert_resting(o, idx);

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), parse_price("100.00"));
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBookTest, BestBidIsHighestPrice) {
    OrderBook book(make_instrument_id(1));
    for (auto [id, px] : {std::pair{1, "100.00"}, std::pair{2, "101.00"}, std::pair{3, "99.50"}}) {
        Order o = make_order(id, Side::Buy, parse_price(px), make_qty(10), id);
        auto idx = book.acquire_order_slot(o);
        book.insert_resting(o, idx);
    }
    EXPECT_EQ(*book.best_bid(), parse_price("101.00"));
}

TEST(OrderBookTest, BestAskIsLowestPrice) {
    OrderBook book(make_instrument_id(1));
    for (auto [id, px] : {std::pair{1, "100.00"}, std::pair{2, "101.00"}, std::pair{3, "99.50"}}) {
        Order o = make_order(id, Side::Sell, parse_price(px), make_qty(10), id);
        auto idx = book.acquire_order_slot(o);
        book.insert_resting(o, idx);
    }
    EXPECT_EQ(*book.best_ask(), parse_price("99.50"));
}

TEST(OrderBookTest, FifoWithinPriceLevel) {
    OrderBook book(make_instrument_id(1));
    Order first = make_order(1, Side::Buy, parse_price("100.00"), make_qty(10), 1);
    Order second = make_order(2, Side::Buy, parse_price("100.00"), make_qty(20), 2);

    auto idx1 = book.acquire_order_slot(first);
    book.insert_resting(first, idx1);
    auto idx2 = book.acquire_order_slot(second);
    book.insert_resting(second, idx2);

    std::uint32_t best = book.peek_best(Side::Buy);
    EXPECT_EQ(book.order_at(best).id, make_order_id(1)); // first order in FIFO order
}

TEST(OrderBookTest, CancelRemovesOrderAndEmptyLevel) {
    OrderBook book(make_instrument_id(1));
    Order o = make_order(1, Side::Buy, parse_price("100.00"), make_qty(10), 1);
    auto idx = book.acquire_order_slot(o);
    book.insert_resting(o, idx);

    EXPECT_TRUE(book.remove_order(make_order_id(1)));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBookTest, CancelUnknownOrderFails) {
    OrderBook book(make_instrument_id(1));
    EXPECT_FALSE(book.remove_order(make_order_id(999)));
}

TEST(OrderBookTest, ReduceQuantitySucceedsForSmallerValue) {
    OrderBook book(make_instrument_id(1));
    Order o = make_order(1, Side::Buy, parse_price("100.00"), make_qty(10), 1);
    auto idx = book.acquire_order_slot(o);
    book.insert_resting(o, idx);

    EXPECT_TRUE(book.reduce_order_quantity(make_order_id(1), make_qty(5)));
    std::uint32_t best = book.peek_best(Side::Buy);
    EXPECT_EQ(book.order_at(best).remaining_quantity, make_qty(5));
}

TEST(OrderBookTest, ReduceQuantityFailsForLargerValue) {
    OrderBook book(make_instrument_id(1));
    Order o = make_order(1, Side::Buy, parse_price("100.00"), make_qty(10), 1);
    auto idx = book.acquire_order_slot(o);
    book.insert_resting(o, idx);

    EXPECT_FALSE(book.reduce_order_quantity(make_order_id(1), make_qty(20)));
}

TEST(OrderBookTest, FillBestFullyConsumesOrder) {
    OrderBook book(make_instrument_id(1));
    Order o = make_order(1, Side::Sell, parse_price("100.00"), make_qty(10), 1);
    auto idx = book.acquire_order_slot(o);
    book.insert_resting(o, idx);

    bool fully_consumed = book.fill_best(Side::Sell, make_qty(10));
    EXPECT_TRUE(fully_consumed);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookTest, FillBestPartial) {
    OrderBook book(make_instrument_id(1));
    Order o = make_order(1, Side::Sell, parse_price("100.00"), make_qty(10), 1);
    auto idx = book.acquire_order_slot(o);
    book.insert_resting(o, idx);

    bool fully_consumed = book.fill_best(Side::Sell, make_qty(4));
    EXPECT_FALSE(fully_consumed);
    std::uint32_t best = book.peek_best(Side::Sell);
    EXPECT_EQ(book.order_at(best).remaining_quantity, make_qty(6));
}

TEST(OrderBookTest, DepthReportsLevelsInPriceOrder) {
    OrderBook book(make_instrument_id(1));
    for (auto [id, px] : {std::pair{1, "100.00"}, std::pair{2, "101.00"}, std::pair{3, "99.50"}}) {
        Order o = make_order(id, Side::Buy, parse_price(px), make_qty(10), id);
        auto idx = book.acquire_order_slot(o);
        book.insert_resting(o, idx);
    }
    auto depth = book.bid_depth(10);
    ASSERT_EQ(depth.size(), 3u);
    EXPECT_EQ(depth[0].price, parse_price("101.00"));
    EXPECT_EQ(depth[1].price, parse_price("100.00"));
    EXPECT_EQ(depth[2].price, parse_price("99.50"));
}

TEST(OrderBookTest, MultipleInstrumentsIndependent) {
    OrderBook book1(make_instrument_id(1));
    OrderBook book2(make_instrument_id(2));

    Order o1 = make_order(1, Side::Buy, parse_price("100.00"), make_qty(10), 1);
    auto idx1 = book1.acquire_order_slot(o1);
    book1.insert_resting(o1, idx1);

    EXPECT_TRUE(book1.best_bid().has_value());
    EXPECT_FALSE(book2.best_bid().has_value());
}
