TEST_PULSE_NODE
===============

The ``TEST_PULSE_NODE`` is a diagnostic that emits test pulses and watches for them
on a returning channel.  Like :doc:`LOOP_TEST <loop_test>` it is used to verify that
a signal path is intact and to characterize its latency.

Properties
----------

* **Input**: The channel on which the returning pulse is expected.
* **Output**: The channel on which the test pulse is emitted.
