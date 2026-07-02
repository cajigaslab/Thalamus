SPIKEGLX
========

The SPIKEGLX node streams high-density electrophysiology data from a SpikeGLX /
Neuropixels acquisition system.  It is a generator: Thalamus connects to the
SpikeGLX remote-command server and republishes the selected stream as analog data.

Enable the SpikeGLX *Remote Command Server* so Thalamus can connect.

Properties
----------

* **Address**: Host (and port) of the SpikeGLX remote-command server.
* **Stream**: The stream/probe to acquire (SpikeGLX exposes IMEC probe streams; the
  node queries the available probes and stream count when it connects).
* **Metadata Node**: Optional node whose metadata is associated with this stream.
* **Publish**: Whether to publish the acquired data into the pipeline.
* **Running**: Connect and begin streaming.  The node reports a **Connected** status
  and any connection **Error** once it attempts the link.
