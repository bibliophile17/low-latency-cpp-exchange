// Linux TCP order-entry gateway.
//
// Responsibilities: accept client connections, read newline-delimited
// text-protocol commands (see protocol/text_protocol.hpp), assign each
// NEW order a strictly increasing OrderId/sequence number, and push
// resulting Commands onto an SPSC queue for the matching engine thread
// to consume. This class knows nothing about order books or matching;
// the matching engine (matching/matching_engine.hpp) knows nothing
// about sockets. That separation is intentional -- see docs/architecture.md.
//
// This is a simple blocking/poll()-based single-threaded-per-connection
// server, deliberately not epoll-based reactor machinery: the project's
// low-latency claims are about the matching engine and queue, not about
// scaling to tens of thousands of concurrent TCP connections (which a
// production gateway would need epoll/io_uring for). That scope
// decision is documented in docs/network-protocol.md.
#pragma once

#include "exchange/concurrency/spsc_queue.hpp"
#include "exchange/core/commands.hpp"
#include "exchange/gateway/symbol_table.hpp"
#include "exchange/protocol/text_protocol.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace exchange::gateway {

struct GatewayConfig {
    std::uint16_t port = 9999;
    int backlog = 16;
};

class OrderGateway {
public:
    OrderGateway(GatewayConfig config,
                 SymbolTable& symbols,
                 concurrency::SpscQueue<core::Command>& out_queue);
    ~OrderGateway();

    OrderGateway(const OrderGateway&) = delete;
    OrderGateway& operator=(const OrderGateway&) = delete;

    // Starts accepting connections on a background thread. Non-blocking.
    void start();
    // Signals shutdown and joins the accept thread.
    void stop();

    [[nodiscard]] std::uint64_t connections_accepted() const noexcept { return connections_accepted_; }

private:
    void accept_loop();
    void handle_client(int client_fd);

    GatewayConfig config_;
    SymbolTable& symbols_;
    concurrency::SpscQueue<core::Command>& out_queue_;

    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::atomic<std::uint64_t> next_order_id_{1};
    std::atomic<std::uint64_t> connections_accepted_{0};
};

} // namespace exchange::gateway
