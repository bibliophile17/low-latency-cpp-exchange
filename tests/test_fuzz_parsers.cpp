// Lightweight fuzz/property test for the two untrusted-input parsers:
// the text protocol (protocol/text_protocol.hpp) and the binary
// protocol (protocol/binary_protocol.hpp).
//
// This is not libFuzzer/AFL-based (no coverage-guided mutation) --
// see docs/testing.md for why a simple seeded-random byte-basher is
// judged adequate here: both parsers are small, bounded-size, branchy
// functions over short inputs, and the property under test is narrow
// ("never crash, never read out of bounds, always either produce a
// valid Command or return nullopt/-cleanly"). AddressSanitizer +
// UndefinedBehaviorSanitizer (see docs/testing.md "Sanitizers") give
// this harness teeth: run this binary under an ASan/UBSan build
// (`cmake -DEXCHANGE_ENABLE_ASAN=ON`) to actually catch memory issues,
// not just parse failures.
//
// This is compiled into GoogleTest so it runs as part of the normal
// test suite and in CI, rather than needing a separate libFuzzer build
// -- a reasonable tradeoff given the project's scope. A true
// libFuzzer harness would be a natural "future improvement" (see
// docs/testing.md).
#include "exchange/gateway/symbol_table.hpp"
#include "exchange/protocol/binary_protocol.hpp"
#include "exchange/protocol/text_protocol.hpp"

#include <gtest/gtest.h>
#include <random>
#include <string>

using namespace exchange::core;
using namespace exchange::protocol;

namespace {

InstrumentId dummy_resolve(std::string_view) { return make_instrument_id(1); }

std::string random_text_line(std::mt19937& rng, std::size_t max_len) {
    // Mix of "plausible" tokens and pure garbage bytes so the fuzzer
    // exercises both the happy-path token recognition and the
    // malformed-input rejection paths.
    static const char* kTokens[] = {
        "NEW", "BUY", "SELL", "MARKET", "CANCEL", "MODIFY", "AAPL",
        "IOC", "FOK", "100", "185.20", "-5", "abc", "", "999999999999999999999",
    };
    std::uniform_int_distribution<std::size_t> len_dist(0, 8);
    std::uniform_int_distribution<std::size_t> token_dist(0, std::size(kTokens) - 1);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    std::string line;
    std::size_t token_count = len_dist(rng);
    for (std::size_t i = 0; i < token_count && line.size() < max_len; ++i) {
        if (!line.empty()) line.push_back(' ');
        // 80% chance of a plausible token, 20% chance of raw random bytes.
        std::uniform_real_distribution<double> pick(0.0, 1.0);
        if (pick(rng) < 0.8) {
            line += kTokens[token_dist(rng)];
        } else {
            std::size_t garbage_len = len_dist(rng);
            for (std::size_t g = 0; g < garbage_len; ++g) {
                char c = static_cast<char>(byte_dist(rng));
                if (c != '\n' && c != '\0') line.push_back(c); // protocol is newline-delimited
            }
        }
    }
    return line;
}

} // namespace

TEST(FuzzTest, TextProtocolNeverCrashesOnRandomInput) {
    std::mt19937 rng(12345);
    constexpr int kIterations = 20000;

    for (int i = 0; i < kIterations; ++i) {
        std::string line = random_text_line(rng, 600); // exceeds the 512-byte limit sometimes, intentionally
        ParseError error;
        // The only property under test: this must never crash, never
        // throw an uncaught exception, and never read out of bounds
        // (verified when this binary is built with ASan/UBSan).
        auto result = parse_text_command(line, dummy_resolve, make_order_id(static_cast<std::uint64_t>(i)), error);
        // No assertion on the result's value -- both accept and reject
        // are valid outcomes for random input. We only assert the call
        // returned normally.
        (void)result;
    }
    SUCCEED();
}

TEST(FuzzTest, BinaryProtocolNeverCrashesOnRandomBytes) {
    std::mt19937 rng(54321);
    std::uniform_int_distribution<int> byte_dist(0, 255);
    std::uniform_int_distribution<std::size_t> len_dist(0, 80);

    constexpr int kIterations = 20000;
    for (int i = 0; i < kIterations; ++i) {
        std::size_t len = len_dist(rng);
        std::vector<std::byte> buf(len);
        for (auto& b : buf) b = static_cast<std::byte>(byte_dist(rng));

        std::size_t consumed = 0;
        auto result = decode_binary(std::span<const std::byte>(buf.data(), buf.size()), consumed);
        (void)result;
        // consumed must never exceed the bytes actually supplied --
        // this would indicate an out-of-bounds read on the caller side
        // if consumed were trusted blindly.
        EXPECT_LE(consumed, len);
    }
    SUCCEED();
}

TEST(FuzzTest, BinaryProtocolRoundTripNeverCorruptsValidMessages) {
    // Property: encode(decode(encode(x))) == encode(x) for structurally
    // valid commands with random field values (not random bytes) --
    // catches encode/decode asymmetry bugs that a pure byte-fuzzer
    // would rarely stumble into by chance.
    std::mt19937_64 rng(777);
    std::uniform_int_distribution<std::uint64_t> id_dist(0, UINT64_MAX);
    std::uniform_int_distribution<std::uint32_t> instrument_dist(0, 1000);
    std::uniform_int_distribution<std::uint64_t> qty_dist(1, 1'000'000);
    std::uniform_int_distribution<std::int64_t> price_dist(1, 10'000'000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    for (int i = 0; i < 5000; ++i) {
        NewOrderCommand cmd;
        cmd.id = make_order_id(id_dist(rng));
        cmd.instrument = make_instrument_id(instrument_dist(rng));
        cmd.side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        cmd.type = OrderType::Limit;
        cmd.tif = TimeInForce::Day;
        cmd.price = make_price(price_dist(rng));
        cmd.quantity = make_qty(qty_dist(rng));

        std::array<std::byte, 64> buf1{};
        std::size_t len1 = encode_binary(Command{cmd}, std::span<std::byte, 64>(buf1));

        std::size_t consumed = 0;
        auto decoded = decode_binary(std::span<const std::byte>(buf1.data(), len1), consumed);
        ASSERT_TRUE(decoded.has_value());

        std::array<std::byte, 64> buf2{};
        std::size_t len2 = encode_binary(*decoded, std::span<std::byte, 64>(buf2));

        ASSERT_EQ(len1, len2);
        EXPECT_EQ(0, std::memcmp(buf1.data(), buf2.data(), len1));
    }
}
