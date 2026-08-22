cmake_minimum_required(VERSION 3.20)
project(low_latency_exchange CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

option(EXCHANGE_ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)
option(EXCHANGE_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(EXCHANGE_BUILD_TESTS "Build GoogleTest unit tests" ON)
option(EXCHANGE_BUILD_BENCHMARKS "Build Google Benchmark suite" ON)

if(EXCHANGE_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(EXCHANGE_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

# --------------------------------------------------------------------
# Core library: everything except main()/CLI entry points, so it can be
# linked into both the exchange binary and the test/benchmark binaries.
# --------------------------------------------------------------------
add_library(exchange_core
    src/core/types.cpp
    src/orderbook/order_book.cpp
    src/matching/matching_engine.cpp
    src/protocol/text_protocol.cpp
    src/protocol/binary_protocol.cpp
    src/gateway/order_gateway.cpp
    src/replay/replay_engine.cpp
    src/tools/synthetic_generator.cpp
)
target_include_directories(exchange_core PUBLIC include)
target_link_libraries(exchange_core PUBLIC Threads::Threads)
target_compile_features(exchange_core PUBLIC cxx_std_20)

# --------------------------------------------------------------------
# Main exchange server executable
# --------------------------------------------------------------------
add_executable(exchange src/main.cpp)
target_link_libraries(exchange PRIVATE exchange_core)

# --------------------------------------------------------------------
# Example CLI trading client
# --------------------------------------------------------------------
add_executable(exchange_client tools/client_main.cpp)
target_link_libraries(exchange_client PRIVATE exchange_core)

# --------------------------------------------------------------------
# Standalone synthetic market-data generator CLI
# --------------------------------------------------------------------
add_executable(market_generator tools/market_generator_main.cpp)
target_link_libraries(market_generator PRIVATE exchange_core)

# --------------------------------------------------------------------
# Stress test executable
# --------------------------------------------------------------------
add_executable(stress_test tools/stress_test_main.cpp)
target_link_libraries(stress_test PRIVATE exchange_core)

# --------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------
if(EXCHANGE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(exchange_tests
        tests/test_types.cpp
        tests/test_order_book.cpp
        tests/test_matching_engine.cpp
        tests/test_spsc_queue.cpp
        tests/test_text_protocol.cpp
        tests/test_binary_protocol.cpp
        tests/test_replay.cpp
        tests/test_invariants.cpp
        tests/test_fuzz_parsers.cpp
    )
    target_link_libraries(exchange_tests PRIVATE exchange_core GTest::gtest GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(exchange_tests)
endif()

# --------------------------------------------------------------------
# Benchmarks
# --------------------------------------------------------------------
if(EXCHANGE_BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    add_executable(exchange_benchmarks
        benchmarks/bench_order_book.cpp
        benchmarks/bench_matching_engine.cpp
        benchmarks/bench_spsc_queue.cpp
    )
    target_link_libraries(exchange_benchmarks PRIVATE exchange_core benchmark::benchmark benchmark::benchmark_main)
endif()
