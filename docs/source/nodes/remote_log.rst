REMOTE_LOG
==========

The REMOTE_LOG node proxies the log stream from another Thalamus instance over
gRPC, so log messages produced on a remote machine can be aggregated locally.  It
is the logging counterpart to the :doc:`REMOTE <remote>` data node.

Properties
----------

* **Address**: The address of the remote Thalamus instance to connect to.
* **Probe Size** / **Probe Frequency**: Parameters of the periodic ping/probe used to
  measure the connection.
* **Running**: Connect and begin streaming the remote log.

While connected the node reports a **Connected** status, the measured **Bytes Per
Second**, and ping/latency information.
