FFMPEG
======

The FFMPEG node is a generator that ingests media through FFmpeg/libav and
republishes it as an image stream.  Because it uses FFmpeg's input layer it can
read a wide range of file formats as well as live capture devices (cameras,
screen-grab inputs, etc.) by specifying an input format.

Properties
----------

* **Input Name**: The input to open -- a file path or a device/URL understood by
  FFmpeg.
* **Input Format**: The FFmpeg input format/demuxer to use (for example a webcam or
  screen-capture backend).  Leave unset to let FFmpeg infer it from the input.
* **Options**: Additional FFmpeg input options (key/value pairs passed to the
  demuxer/decoder).
* **Time Source**: How frame timestamps are assigned.
* **Running**: Begin decoding.

The node reports the **Target Framerate** (derived from the input stream) and its
throughput while running.  To replay a plain video file, the simpler
:doc:`VIDEO <video>` node is usually sufficient.
