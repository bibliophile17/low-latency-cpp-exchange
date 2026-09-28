CLI reference
=============

``exchange``
------------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Flag
     - Description
   * - ``--server``
     - Run as the exchange server.
   * - ``--port <int>``
     - TCP port to listen on.
   * - ``--symbols <list>``
     - Comma-separated list of instrument symbols, e.g. ``AAPL,MSFT``.
   * - ``--record <file>``
     - Record the command stream to ``<file>`` while running.
   * - ``--generate <n>``
     - Generate ``n`` synthetic orders while recording.
   * - ``--seed <int>``
     - Seed for synthetic order generation.
   * - ``--replay <file>``
     - Replay a previously recorded command stream from ``<file>``.

``exchange_client``
--------------------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Flag
     - Description
   * - ``--host <address>``
     - Address of the running exchange server.
   * - ``--port <int>``
     - Port the exchange server is listening on.

``market_generator``
----------------------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Flag
     - Description
   * - ``--orders <n>``
     - Number of synthetic orders to generate.
   * - ``--seed <int>``
     - Seed for reproducible order flow.
   * - ``--out <file>``
     - Output file for the generated command stream.

``stress_test``
------------------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Flag
     - Description
   * - ``--orders <n>``
     - Number of randomized orders to process.
   * - ``--seed <int>``
     - Seed for reproducible randomized flow.
   * - ``--instruments <n>``
     - Number of distinct instruments to spread orders across.

``exchange_benchmarks``
--------------------------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Flag
     - Description
   * - ``--benchmark_out=<file>``
     - Write benchmark results to ``<file>``.
   * - ``--benchmark_out_format=json``
     - Output format for the results file.

``exchange_tests``
---------------------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Flag
     - Description
   * - ``--gtest_filter=<pattern>``
     - Run only tests matching ``<pattern>``, e.g. ``'MatchingEngineTest.*'``.
