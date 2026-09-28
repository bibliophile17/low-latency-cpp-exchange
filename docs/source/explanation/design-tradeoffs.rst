Design tradeoffs
=================

Every non-obvious decision below was made deliberately, with a
cheaper-but-worse alternative available. This page explains what was
chosen, what it costs, and why the cost was accepted.

``std::map<Price, PriceLevel>`` over a flat tick-indexed array
------------------------------------------------------------------

**Chosen** to support unbounded or arbitrary price ranges across
multiple synthetic instruments.

**Cost**: O(log L) new-price-level insertion instead of O(1), where L
is the number of active price levels — typically small in practice.

**Alternative considered**: a flat array indexed by price tick, which
would give O(1) insertion at the cost of requiring a known, bounded
price range per instrument. Listed as a possible future ``OrderBook``
backend for instruments where that bound is known.

Single-threaded matching engine
-----------------------------------

**Chosen** to guarantee exact price-time priority with no lost updates.

**Cost**: no parallel throughput across instruments within one engine
instance.

**Why the cost was accepted**: the project's own measured single-thread
numbers show this isn't the bottleneck at this project's scale.
Multi-instrument sharding across separate engine instances — each still
single-threaded internally — is listed as a future improvement rather
than solved by making one engine instance concurrent.

Thread-per-connection gateway, not epoll/io_uring
------------------------------------------------------

**Chosen** because this project's latency claims are about the matching
core, not connection-count scalability.

**Cost**: doesn't scale to a large number of concurrent connections.

**Why the cost was accepted**: adequate for the benchmark and demo
client counts this project targets. An epoll/io_uring-based gateway for
higher connection counts is listed as a future improvement.

``std::unordered_map`` for order-ID lookup
-----------------------------------------------

**Chosen** because the wire protocol references orders by ID, so
cancel/modify needs O(1) average lookup by ``OrderId``.

**Cost**: the one heap-backed hash map on the otherwise allocation-free
matching hot path.

**Mitigation**: capacity is reserved up front, so steady-state operation
doesn't trigger rehashing.
