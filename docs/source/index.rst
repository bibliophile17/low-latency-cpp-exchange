Exchange Simulator
===================

A local, offline, educational simulation of an electronic exchange: order
gateway, deterministic price-time-priority matching engine, limit order
book, market-data publishing, replay engine, and a benchmark/stress-test
harness, built in C++20 for Linux.

.. note::
   This is a local educational and research-oriented simulation. It uses
   synthetic/local data only and does not connect to real financial
   markets, brokers, exchanges, or trading accounts.

This documentation is organized using the `Diátaxis <https://diataxis.fr/>`_
framework, which separates content by what the reader is trying to do:

.. grid:: 2

   .. grid-item-card:: Tutorials
      :link: tutorials/index
      :link-type: doc

      Learning-oriented. Start here if you are new to the project and want
      to get a trade executed end to end.

   .. grid-item-card:: How-to guides
      :link: how-to/index
      :link-type: doc

      Task-oriented. Steps for a specific job, such as running the
      benchmark suite or replaying a recorded session.

   .. grid-item-card:: Reference
      :link: reference/index
      :link-type: doc

      Information-oriented. The CLI flags and wire-protocol commands,
      described precisely and without narrative.

   .. grid-item-card:: Explanation
      :link: explanation/index
      :link-type: doc

      Understanding-oriented. Why the system is built the way it is, and
      the tradeoffs behind each design decision.

.. toctree::
   :maxdepth: 2
   :hidden:

   tutorials/index
   how-to/index
   reference/index
   explanation/index
