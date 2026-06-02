NORMALIZE
=========

The NORMALIZE node is a transformer that linearly rescales an input signal so that
its observed range maps onto a configured output range.  It is useful for putting a
raw sensor signal into normalized units (for example 0–1) before further
processing or display.

Usage
-----

Set the node's **Source** to the node to normalize and configure the desired output
range with **Min Out** and **Max Out**.  As data flows through, the node tracks the
minimum and maximum values it has seen and maps that observed range onto
``[Min Out, Max Out]``.

Properties
----------

* **Source**: The node supplying the input samples.
* **Min** (Min Out): The output value the observed minimum maps to (default ``0``).
* **Max** (Max Out): The output value the observed maximum maps to (default ``1``).

Calibration controls
---------------------

* **Reset**: Clears the tracked input range so the node re-learns the minimum and
  maximum from subsequent data.
* **Cache**: Persists the current calibration so it is reused across runs instead of
  being re-learned each time.
