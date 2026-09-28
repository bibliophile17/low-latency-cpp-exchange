Wire protocol
=============

The order gateway accepts a human-readable text protocol over TCP (a
compact fixed-size binary protocol is also implemented). The matching
engine has no knowledge of either — see
:doc:`../explanation/architecture`.

Commands
--------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Command
     - Syntax
   * - New order
     - ``NEW <BUY|SELL> <symbol> <quantity> <price>``
   * - Cancel order
     - ``CANCEL <order_id> <symbol>``
   * - Modify order
     - ``MODIFY <order_id> <symbol> <new_quantity>``

Order types supported: limit and market. Time-in-force: Day, IOC
(Immediate-Or-Cancel), FOK (Fill-Or-Kill). Modify is a quantity-reduce
only.

Example
-------

.. code-block:: text

   NEW BUY AAPL 100 185.20
   NEW SELL AAPL 50 185.15
   CANCEL 1 AAPL
   MODIFY 2 AAPL 25

Market-data events
---------------------

Emitted by the matching engine via an event-sink callback and delivered
synchronously to every subscriber:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Event
     - Meaning
   * - ``OrderAccepted``
     - The order passed validation and entered the book (or matched immediately).
   * - ``OrderCancelled``
     - A resting order was removed by a ``CANCEL`` command.
   * - ``OrderModified``
     - A resting order's quantity was reduced by a ``MODIFY`` command.
   * - ``OrderRejected``
     - The order failed validation and was not accepted.
   * - ``TradeExecuted``
     - Two orders matched; includes the traded quantity and price.
   * - ``BookUpdate``
     - The top of book changed for the instrument.
