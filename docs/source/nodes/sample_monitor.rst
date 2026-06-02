SAMPLE_MONITOR
==============

The SAMPLE_MONITOR node is a diagnostic that watches the sample rate of other nodes
and raises an alert when a node's rate falls outside expected parameters.  Use it to
catch a device that has started dropping samples or stalled during a recording.

Properties
----------

* **Nodes**: The list of nodes to monitor.  Each entry is identified by its **Name**.

For each monitored node the SAMPLE_MONITOR reports the observed rate alongside the
expected rate and sets an **Alert** when the sample rate is outside the expected
parameters.

For monitoring a single channel's frequency against a specific expected value, see
the :doc:`FREQUENCY <frequency>` node.
