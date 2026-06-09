VIDEO
=====

The VIDEO node is a generator that plays a video file and republishes its frames as
an image stream.  Use it to feed recorded footage into a pipeline -- for example to
replay a session through image-processing nodes such as OCULOMATIC or ARUCO.

Properties
----------

* **File Name**: Path to the video file to play.
* **Running**: Begin playback.

The node plays back at the file's native frame timing and reports the resulting
``Framerate`` and its measured throughput (``BPS``) while running.  For ingesting
live capture devices or a wider range of container/codec inputs, see the
:doc:`FFMPEG <ffmpeg>` node.
