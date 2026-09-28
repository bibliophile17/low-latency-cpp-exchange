Architecture
============

Overview
--------

The system simulates a simplified electronic exchange: clients submit
orders over TCP using a text (or binary) wire protocol, an order gateway
turns those into typed ``Command``\ s, a single-threaded matching engine
applies price-time-priority matching against a per-instrument limit
order book, and the resulting trades and book changes are published as
market-data events.

.. code-block:: text

   Client --TCP--> Gateway --Command--> SPSC Queue --> Matching Engine
                                                           |
                                                           v
                                                       Order Book
                                                           |
                                                           v
                                              Market Data Publisher --> Subscribers

   Replay Recorder <-- records Commands -- Matching Engine
   Replay File --replays Commands--> Matching Engine

   Synthetic Generator --seeded order flow--> Benchmark Suite --drives load--> Matching Engine

Component responsibilities
---------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Component
     - Location
     - Responsibility
   * - ``core``
     - ``include/exchange/core``, ``src/core``
     - Strong types (``Price``, ``Quantity``, ``OrderId``, ...), ``Order``, ``Command`` variants
   * - ``orderbook``
     - ``include/exchange/orderbook``, ``src/orderbook``
     - Price-level book, FIFO queues, best-price/depth queries
   * - ``matching``
     - ``include/exchange/matching``, ``src/matching``
     - Deterministic price-time-priority matching, no knowledge of sockets
   * - ``gateway``
     - ``include/exchange/gateway``, ``src/gateway``
     - TCP accept/parse loop, assigns order IDs, no knowledge of matching
   * - ``protocol``
     - ``include/exchange/protocol``, ``src/protocol``
     - Text and binary wire formats
   * - ``marketdata``
     - ``include/exchange/marketdata``
     - Typed event structs (``OrderAccepted``, ``TradeExecuted``, ...)
   * - ``concurrency``
     - ``include/exchange/concurrency``
     - SPSC ring buffer
   * - ``memory``
     - ``include/exchange/memory``
     - Fixed-capacity object pool (no hot-path allocation)
   * - ``replay``
     - ``include/exchange/replay``, ``src/replay``
     - Binary record/replay of the command stream

Why this separation
----------------------

The matching engine never includes a socket header and never spawns a
thread. It receives ``Command``\ s and calls a caller-supplied
``EventSink`` callback. This means it can be:

- driven directly and synchronously from unit tests, with no network setup
- driven from the replay engine for deterministic regression and
  performance testing
- driven from a live gateway thread in production use

The wire protocol, transport, and threading model can all change
independently of the matching logic, and vice versa. This mirrors how
real exchange and market-making systems are usually structured: the
matching/decision logic is kept as a pure, synchronous, deterministic
core, with I/O and concurrency handled at the edges.

Data flow for a single new order
-----------------------------------

#. Client sends ``NEW BUY AAPL 100 185.20`` over TCP.
#. The gateway reads the line, assigns the next ``OrderId``, and parses
   it into a ``Command``.
#. The resulting ``NewOrderCommand`` is pushed onto the SPSC queue
   shared with the matching engine thread.
#. The matching engine thread pops the command and processes it.
#. Processing validates the order, emits ``OrderAccepted`` (or
   ``OrderRejected``), attempts to match it against the opposite side
   of the book (emitting zero or more ``TradeExecuted`` events),
   inserts any remaining quantity as a resting order if appropriate,
   and finally emits a ``BookUpdate`` if the top of book changed.
#. All of these market-data events are delivered synchronously to
   whatever ``EventSink`` the engine was constructed with — in the CLI
   server this prints to stdout; in tests it's a collector.
