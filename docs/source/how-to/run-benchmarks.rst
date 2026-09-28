How to run the benchmark suite and generate a chart
=====================================================

This guide runs the Google Benchmark suite over order-book operations,
matching-engine operations, and the SPSC queue, and turns the results
into a chart.

1. Build in Release mode (benchmarks are included by default):

   .. code-block:: bash

      ./scripts/build.sh

2. Run the benchmark suite and write results to a JSON file:

   .. code-block:: bash

      ./build/exchange_benchmarks \
        --benchmark_out=results.json \
        --benchmark_out_format=json

3. Generate a chart from the results:

   .. code-block:: bash

      python3 scripts/plot_benchmarks.py results.json --out benchmark_report.png

4. Open ``benchmark_report.png``.

.. note::
   Every number that ships in this project's own performance
   documentation came from an actual run on real hardware — none are
   estimated or extrapolated. Re-run the suite before quoting any
   number as your own machine's performance; results depend on the
   hardware you run on.

See also
--------

- :doc:`../reference/cli` for the full flag reference.
- :doc:`../explanation/design-tradeoffs` for why the matching engine is
  single-threaded, which shapes what these numbers do (and don't) tell you.
