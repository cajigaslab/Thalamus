INTAN
=====

The INTAN node streams electrophysiology data from an Intan RHX acquisition system
(e.g. an RHD/RHS controller).  It is a generator: Thalamus connects to the RHX
software over TCP and republishes the waveform data as an analog stream.

In the RHX software, enable the TCP command and waveform servers so Thalamus can
connect.

Properties
----------

* **Address**: Host address of the machine running the Intan RHX software.
* **Command Port**: TCP port of the RHX command server (used to query configuration
  and start/stop streaming).
* **Waveform Port**: TCP port of the RHX waveform server (the data stream).
* **Channels**: The set of channels to acquire.
* **Metadata Node**: Optional node whose metadata is associated with this stream.
* **Running**: Connect and begin streaming.  The node reports the negotiated
  ``SampleRateHertz`` and a **Connected** status once the link is established.
