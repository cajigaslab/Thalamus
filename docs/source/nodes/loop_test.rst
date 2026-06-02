LOOP_TEST
=========

The LOOP_TEST node is a diagnostic used to verify signal routing and measure
round-trip latency through a loop.  It generates a sinusoidal test signal and can
compare it against a returning signal, which is useful for validating that data
flows correctly through a chain of nodes (or out to hardware and back).

Properties
----------

* **Source**: The node carrying the returning signal to compare against.
* **Channel**: The channel of the source to use.

The generated test signal is a sinusoid (``Sin``) that the node emits and watches for
on the source.
