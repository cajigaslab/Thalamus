RUNNER
======

The RUNNER node is a simple controller that mirrors its Running state onto a list of
other nodes in the local pipeline.  When you start or stop the RUNNER, every node it
targets is started or stopped to match, so you can control a group of nodes with a
single action.

Properties
----------

* **Targets**: A comma-separated list of node names to control.
* **Running**: When toggled, every node named in **Targets** is set to the same state.

For controlling nodes on remote Thalamus instances (each with its own address), use
the :doc:`RUNNER2 <runner2>` node instead.
