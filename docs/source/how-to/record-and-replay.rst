How to record and replay a session deterministically
=======================================================

Use this when you need the exact same sequence of orders to run twice —
for example, to reproduce a bug or to compare performance before and
after a code change.

Generate reproducible synthetic order flow
--------------------------------------------

.. code-block:: bash

   ./build/market_generator --orders 1000000 --seed 12345 --out orders.bin

The ``--seed`` value determines the order flow. The same seed always
produces the same sequence of orders.

Record a session
-------------------

.. code-block:: bash

   ./build/exchange --record orders.bin --generate 100000 --seed 42

This generates 100,000 synthetic orders with seed ``42`` and records the
resulting command stream to ``orders.bin`` as it runs.

Replay a session
-------------------

.. code-block:: bash

   ./build/exchange --replay orders.bin

The matching engine processes the recorded command stream exactly as it
did the first time. Replay determinism is checked directly by
``tests/test_replay.cpp``.

See also
--------

- :doc:`../reference/cli` for every flag these executables accept.
- :doc:`../explanation/architecture` for where the replay engine sits
  relative to the live gateway.
