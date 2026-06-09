FREQUENCY
=========

The FREQUENCY node monitors the rate of a channel and flags when it drifts outside
expected bounds.  It is a watchdog for acquisition health: if a device starts
dropping samples or running fast/slow, the node raises an alert.

Properties
----------

* **Source**: The node to monitor.
* **Channel Number**: Which channel of the source to measure.
* **Expected Frequency**: The frequency the channel should run at.
* **Expected Frequency Std**: The allowed variability (standard deviation) around the
  expected frequency before an alert is raised.

The node reports the measured **Mean** and **Std** of the frequency and sets an
**Alert** when the channel is outside the expected parameters.
