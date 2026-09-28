How to build and run under sanitizers
========================================

Use this when you're chasing a memory-safety or data-race bug rather
than measuring performance.

AddressSanitizer + UndefinedBehaviorSanitizer
------------------------------------------------

.. code-block:: bash

   ./scripts/build.sh asan
   ./scripts/test.sh

ThreadSanitizer
------------------

.. code-block:: bash

   ./scripts/build.sh tsan
   ./scripts/test.sh

Debug build for gdb / Valgrind
----------------------------------

.. code-block:: bash

   ./scripts/build.sh debug
   ./scripts/profile.sh gdb --orders 1000 --seed 1
   ./scripts/profile.sh valgrind-memcheck --orders 2000 --seed 1

Both sanitizer modes are wired directly into CMake via the
``EXCHANGE_ENABLE_ASAN`` and ``EXCHANGE_ENABLE_TSAN`` options, and are
confirmed clean against the full test suite on the development machine.
