STORAGE
=======

STORAGE is the earlier-generation recording node.  It saves data from a set of
source nodes to a ``.tha`` capture file.  New experiments should prefer the
:doc:`STORAGE2 <storage2>` node, which adds per-modality selection, file copying,
and structured metadata; STORAGE is retained for compatibility with existing
configurations.

Properties
----------

* **Sources**: The nodes whose data is recorded.
* **Output File**: The base name of the output file (the recording suffix is appended
  as with STORAGE2).
* **Compress Analog**: Compress time-series data with zlib.
* **Compress Video**: Compress image data with MPEG-4 video encoding.
* **Running**: Begin recording.

While running, the node reports its output queue depth (**Output Queue Bytes** /
**Output Queue Count**) as a backpressure indicator.
