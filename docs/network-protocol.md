# Network Protocol

## Scope decision: simplicity over connection scale

The gateway (`include/exchange/gateway/order_gateway.hpp`,
`src/gateway/order_gateway.cpp`) is a straightforward blocking-sockets,
thread-per-connection TCP server. It is **not** epoll/io_uring-based.

This project's low-latency claims are about the matching engine and the
SPSC queue between the network layer and the engine -- not about
scaling to tens of thousands of concurrent TCP connections, which a
production gateway would need an epoll/io_uring reactor for. A
thread-per-connection model is adequate for the benchmark/demo client
counts this project targets (a handful of concurrent connections) and
keeps the networking code simple to read and reason about. Moving to
epoll/io_uring is listed as a future improvement in
`docs/architecture.md`.

## Transport

TCP, one connection per client. `TCP_NODELAY` is set on accepted client
sockets to disable Nagle's algorithm, since batching small writes for
30-500ms is actively harmful for an order-entry protocol where each
message should be forwarded as soon as possible.

## Text protocol (development / debugging)

Newline-delimited ASCII, one command per line, whitespace-separated
tokens. Implemented in `include/exchange/protocol/text_protocol.hpp` /
`src/protocol/text_protocol.cpp`.

### Grammar

```
NEW BUY|SELL <SYMBOL> <QTY> <PRICE> [IOC|FOK]
NEW MARKET BUY|SELL <SYMBOL> <QTY>
CANCEL <ORDER_ID> <SYMBOL>
MODIFY <ORDER_ID> <SYMBOL> <NEW_QTY>
```

- `<PRICE>` is a decimal string, e.g. `185.20`, parsed exactly into
  integer ticks (see `docs/order-book.md`).
- `<QTY>` / `<NEW_QTY>` are positive integers.
- `<ORDER_ID>` is assigned by the gateway on `NEW` (the client does not
  choose it) and echoed back implicitly via `OrderAccepted` events; the
  client must remember it to `CANCEL`/`MODIFY` later.
- Omitting `[IOC|FOK]` on a limit order defaults to `Day` (rests on the
  book until filled or cancelled).
- `NEW MARKET ...` orders are always treated as `IOC` (fill what's
  immediately available, cancel the remainder) since a resting market
  order has no defined price to rest at.

### Examples

```
NEW BUY AAPL 100 18520
NEW BUY AAPL 100 185.20
NEW SELL AAPL 50 185.25 IOC
NEW MARKET BUY AAPL 20
CANCEL 12345 AAPL
MODIFY 12346 AAPL 200
```

(Both `18520` [interpreted as ticks-scale decimal `18520.0000` -- note:
always include the decimal point for a human price like `185.20`,
otherwise it parses as an integer price] and `185.20` are valid decimal
strings to `parse_price`; always write prices with an explicit decimal
point to avoid ambiguity, e.g. `185.20` not `18520`.)

## Binary protocol (performance benchmarking)

Implemented in `include/exchange/protocol/binary_protocol.hpp` /
`src/protocol/binary_protocol.cpp`. Used for the record/replay file
format and available for benchmark-oriented clients that want to avoid
text parsing overhead entirely.

Every message is a 1-byte `MessageType` tag followed by a
tightly-packed (`#pragma pack(push, 1)`), fixed-size payload:

| Message | Size (bytes) | Fields |
|---|---|---|
| `NewOrder` | 31 | type(1), order_id(8), instrument(4), side(1), order_type(1), tif(1), price_ticks(8), quantity(8) |
| `CancelOrder` | 13 | type(1), order_id(8), instrument(4) |
| `ModifyOrder` | 21 | type(1), order_id(8), instrument(4), new_quantity(8) |

Multi-byte integers are **host-endian**. This is a local/loopback
benchmarking and file-format protocol, not a cross-network wire
standard -- endianness portability was explicitly scoped out (a real
cross-network binary protocol would need to fix an endianness, e.g.
network byte order, and this is called out here rather than silently
assumed).

## Record/replay file format

`replay::RecordWriter`/`RecordReader` (`include/exchange/replay/replay_engine.hpp`)
wrap the binary protocol with a 4-byte little/host-endian length prefix
per record:

```
[uint32 length][binary-protocol-encoded Command bytes...] [uint32 length][...] ...
```

A length prefix (rather than relying on the fixed sizes above) keeps
the file format uniform even though `NewOrder`/`CancelOrder`/
`ModifyOrder` encode to different sizes.

## Security: untrusted input handling

All network input is treated as untrusted:

- The text protocol parser (`parse_text_command`) bounds line length
  (rejects lines over 512 bytes) before tokenizing, and never throws --
  it wraps its body in `try/catch` and converts any exception into a
  `ParseError` return.
- The binary protocol decoder (`decode_binary`) checks buffer length
  against the expected fixed size for each message type **before**
  `memcpy`-ing into a local struct, and validates enum-like byte fields
  (side/order_type/tif) are in-range before casting them to their
  strongly-typed enum -- an out-of-range byte is rejected rather than
  producing an invalid enum value that could later trip undefined
  behavior in a `switch`.
- Neither parser uses unsafe C string functions (`strcpy`, `sscanf`,
  etc.); tokenizing and integer parsing use `std::from_chars` and
  manual bounds-checked loops.
- `MatchingEngine::process` independently validates every `NewOrder`
  (non-zero quantity, positive limit price, known instrument, no
  duplicate order ID) regardless of what the wire-protocol layer
  already checked, so a malformed or malicious `Command` constructed
  by any path (not just the network parsers) cannot corrupt book state
  -- it is rejected with an `OrderRejected` event instead.
- `tests/test_fuzz_parsers.cpp` exercises both parsers with random and
  semi-random input under normal and AddressSanitizer/UndefinedBehaviorSanitizer
  builds (see `docs/testing.md`) to catch any out-of-bounds access or
  UB that manual review might miss.

## Future improvements

- Authenticated sessions / per-client rate limiting at the gateway.
- epoll/io_uring-based gateway for higher connection counts.
- A structured response channel back to clients (current CLI server
  prints execution reports to its own stdout rather than echoing them
  back over the client's socket -- see `tools/client_main.cpp` comments).
- Network-byte-order (big-endian) binary protocol if this ever needs to
  cross machine architectures.
