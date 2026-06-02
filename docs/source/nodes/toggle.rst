TOGGLE
======

The TOGGLE node is a transformer that converts an analog signal into a binary
0/1 state by comparing it against a threshold.  It is useful for turning a
continuous signal (such as a button voltage or a detector output) into a clean
digital event channel.

Properties
----------

* **Source**: The node supplying the input samples.
* **Threshold**: The comparison level.  Samples on one side of the threshold produce
  ``0`` and samples on the other side produce ``1``.
