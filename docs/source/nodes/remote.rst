REMOTE
======

The REMOTE node proxies a data stream from another Thalamus instance over gRPC.  It
lets a pipeline consume data produced on a different machine as if it were a local
node, which is the basis for distributing acquisition and computation across
several computers.

Properties
----------

* **Address**: The address of the remote Thalamus instance to connect to.
* **Node**: The name of the node on the remote instance whose data to stream.
* **Probe Size** / **Probe Frequency**: Parameters of the periodic ping/probe the node
  uses to measure the connection (latency and bandwidth).
* **Running**: Connect and begin streaming.

While connected the node reports a **Connected** status, the measured **Bytes Per
Second**, and ping/latency information.  See :doc:`runner2` for starting and stopping
remote nodes, and :doc:`remote_log` for the logging equivalent.
