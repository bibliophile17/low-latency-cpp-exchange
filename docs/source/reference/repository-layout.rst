Repository layout
==================

.. code-block:: text

   include/exchange/    public headers, mirrors src/ module layout
   src/
     core/               strong types (Price/Quantity/OrderId/...), Order, Command
     orderbook/          price levels, the OrderBook itself
     matching/           MatchingEngine (deterministic, no socket knowledge)
     gateway/            TCP order-entry server, symbol table
     protocol/           text + binary wire formats
     replay/             binary record/replay engine
     tools/              synthetic order-flow generator
     main.cpp            exchange CLI entry point
   tools/                client_main.cpp, market_generator_main.cpp, stress_test_main.cpp
   tests/                GoogleTest suite (unit, invariant, fuzz/property)
   benchmarks/           Google Benchmark suite + results/sample_run.json
   scripts/              build.sh, test.sh, profile.sh, plot_benchmarks.py
   docs/                 architecture, order-book, concurrency, network-protocol,
                         performance, testing deep-dives
   config/               default instrument symbol list
   CMakeLists.txt
