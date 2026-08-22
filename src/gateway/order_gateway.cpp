#include "exchange/gateway/order_gateway.hpp"
#include "exchange/logging/logger.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>

namespace exchange::gateway {

using logging::log_error;
using logging::log_info;
using logging::log_warn;

OrderGateway::OrderGateway(GatewayConfig config,
                            SymbolTable& symbols,
                            concurrency::SpscQueue<core::Command>& out_queue)
    : config_(config), symbols_(symbols), out_queue_(out_queue) {}

OrderGateway::~OrderGateway() { stop(); }

void OrderGateway::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        log_error("gateway", std::string("socket() failed: ") + std::strerror(errno));
        return;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config_.port);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_error("gateway", std::string("bind() failed: ") + std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::listen(listen_fd_, config_.backlog) < 0) {
        log_error("gateway", std::string("listen() failed: ") + std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    running_ = true;
    accept_thread_ = std::thread([this] { accept_loop(); });

    std::ostringstream os;
    os << "listening on port " << config_.port;
    log_info("gateway", os.str());
}

void OrderGateway::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

void OrderGateway::accept_loop() {
    while (running_.load(std::memory_order_relaxed)) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (client_fd < 0) {
            if (!running_.load(std::memory_order_relaxed)) break;
            continue; // transient accept error; keep listening
        }
        ++connections_accepted_;
        // One thread per connection: simple and adequate for a
        // development/benchmark gateway with a small number of clients.
        std::thread(&OrderGateway::handle_client, this, client_fd).detach();
    }
}

void OrderGateway::handle_client(int client_fd) {
    int flag = 1;
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)); // reduce latency, disable Nagle

    std::string buffer;
    char chunk[4096];

    while (running_.load(std::memory_order_relaxed)) {
        ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break; // client disconnected or error

        buffer.append(chunk, static_cast<std::size_t>(n));

        std::size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            core::OrderId assigned_id = core::make_order_id(next_order_id_.fetch_add(1));

            protocol::ParseError error;
            auto resolver = [this](std::string_view sym) {
                return symbols_.resolve(std::string(sym));
            };
            auto command = protocol::parse_text_command(line, resolver, assigned_id, error);
            if (!command) {
                log_warn("gateway", "rejected malformed command: " + error.message);
                continue;
            }
            // Push to the matching engine thread. try_push can fail if
            // the SPSC queue is full (matching engine is falling
            // behind); in that case we drop and log rather than block
            // the network thread indefinitely, keeping this thread
            // responsive.
            if (!out_queue_.try_push(*command)) {
                log_warn("gateway", "command queue full, dropping command");
            }
        }
    }
    ::close(client_fd);
}

} // namespace exchange::gateway
