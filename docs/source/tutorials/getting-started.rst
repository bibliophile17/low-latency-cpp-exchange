Getting started: execute your first trade
==========================================

In this tutorial you will build the exchange simulator, start the server,
connect a client, and watch a real trade execute between two orders. By
the end you will have seen every stage of the system's data flow at
least once.

Prerequisites
-------------

- Linux
- CMake 3.20 or newer
- A C++20 compiler (GCC or Clang)
- Internet access for the *first* build only, to fetch GoogleTest and
  Google Benchmark

Step 1 — Clone and build
-------------------------

.. code-block:: bash

   git clone <this-repo>
   cd exchange
   ./scripts/build.sh

This produces a Release build, including the test suite and benchmarks,
under ``build/``.

Step 2 — Start the exchange server
------------------------------------

.. code-block:: bash

   ./build/exchange --server --port 9999 --symbols AAPL,MSFT

Leave this running. The server is now listening on port 9999 and will
print every accepted order, trade, cancel, modify, and rejection to its
own stdout as they happen.

Step 3 — Connect a client
---------------------------

In a second terminal:

.. code-block:: bash

   ./build/exchange_client --host 127.0.0.1 --port 9999

You now have an interactive session with the running exchange.

Step 4 — Submit two orders that cross
----------------------------------------

Type the following into the client, one line at a time:

.. code-block:: text

   NEW BUY AAPL 100 185.20
   NEW SELL AAPL 50 185.15

The first line rests a buy order for 100 shares of AAPL at 185.20. The
second line submits a sell order at 185.15 — a price the resting buy is
willing to pay — so the two orders cross immediately.

Step 5 — Read the result
---------------------------

Switch back to the terminal running the server. You should see, in
order: the buy order accepted, the sell order accepted, and then a
trade execution report for the matched quantity, followed by a book
update showing the remaining 50 shares still resting on the buy side.

What just happened
---------------------

Each line you typed traveled the full path described in
:doc:`../explanation/architecture`: client → TCP gateway → command queue
→ matching engine → market-data event → printed to stdout. You have now
exercised every major component in the system by hand.

Next steps
------------

- Try :doc:`../how-to/record-and-replay` to capture this session and
  replay it deterministically.
- Try :doc:`../how-to/run-benchmarks` to see how fast the matching core
  runs under synthetic load.
- Look up the full command syntax in :doc:`../reference/wire-protocol`.
