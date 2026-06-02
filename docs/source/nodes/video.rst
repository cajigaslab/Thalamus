VIDEO
=====

The VIDEO node is a generator that plays a video file and republishes its frames as
an image stream.  Use it to feed recorded footage into a pipeline -- for example to
replay a session through image-processing nodes such as OCULOMATIC or ARUCO.

Properties
----------

* **File Name**: Path to the video file to play.
* **Framerate**: The rate at which frames are emitted.
* **Running**: Begin playback.

The node reports its measured throughput (``BPS``) while running.  For ingesting
live capture devices or a wider range of container/codec inputs, see the
:doc:`FFMPEG <ffmpeg>` node.
