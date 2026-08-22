// Example command-line trading client.
//
// Connects to the exchange gateway over TCP and sends text-protocol
// commands read from stdin (or from a script file), printing back
// whatever the server sends (execution reports / market data are
// currently printed server-side in this simplified demo; a fuller
// client would parse a response channel -- see docs/network-protocol.md
// "Future improvements").
//
// Usage:
//   exchange_client --host 127.0.0.1 --port 9999
//   then type commands, e.g.:
//     NEW BUY AAPL 100 185.20
//     NEW SELL AAPL 50 185.25
//     CANCEL 1 AAPL
//     MODIFY 2 AAPL 50
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int connect_to(const std::string& host, std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid host: " << host << "\n";
        ::close(fd);
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "connect() failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9999;

    std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--host" && i + 1 < args.size()) host = args[++i];
        else if (args[i] == "--port" && i + 1 < args.size()) port = static_cast<std::uint16_t>(std::stoi(args[++i]));
    }

    std::cout << "NOTE: local educational exchange simulator only; no real markets involved.\n";

    int fd = connect_to(host, port);
    if (fd < 0) return 1;

    std::cout << "Connected to " << host << ":" << port << ". Enter commands (Ctrl+D to quit):\n"
                 "  NEW BUY|SELL <SYMBOL> <QTY> <PRICE> [IOC|FOK]\n"
                 "  NEW MARKET BUY|SELL <SYMBOL> <QTY>\n"
                 "  CANCEL <ORDER_ID> <SYMBOL>\n"
                 "  MODIFY <ORDER_ID> <SYMBOL> <NEW_QTY>\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        line.push_back('\n');
        ssize_t sent = ::send(fd, line.data(), line.size(), 0);
        if (sent < 0) {
            std::cerr << "send() failed: " << std::strerror(errno) << "\n";
            break;
        }
    }

    ::close(fd);
    return 0;
}
