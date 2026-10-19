Command-line tools
==================

Thalamus ships several command-line modules.  Each is run with ``python -m``.

registry — live state editing
------------------------------

The ``registry`` tool reads and edits the configuration of a **running** Thalamus
pipeline over gRPC, using `JSONPath <https://github.com/h2non/jsonpath-ng>`_ to
address any part of the state tree.  This lets you inspect or change node parameters
(thresholds, addresses, task settings, ...) live, without restarting.  Reach for it
to tune a parameter mid-experiment, or to script changes to a running pipeline.

.. code-block::

   python -m thalamus.registry -a ADDRESS -p JSONPATH [-s VALUE] [-d]

* ``-a, --address`` -- the pipeline address (default ``localhost:50050``).
* ``-p, --path`` -- a JSONPath expression selecting the element(s) to act on.
* ``-s, --set`` -- a JSON value to assign to the selected element(s).
* ``-d, --delete`` -- delete the selected element(s).

With neither ``--set`` nor ``--delete``, the tool prints the current value.

.. note::

   The pipeline's gRPC server also has `reflection
   <https://grpc.io/docs/guides/reflection/>`_ enabled, so generic gRPC clients such
   as ``grpcurl``/``grpcui`` can introspect it without the ``.proto`` files, e.g.
   ``grpcurl -plaintext localhost:50051 list``.

Examples::

   # Print the whole node list
   python -m thalamus.registry -p '$.nodes'

   # Read one node's Running flag (assuming a node named "wave")
   python -m thalamus.registry -p "$.nodes[?(@.name=='wave')].Running"

   # Start that node
   python -m thalamus.registry -p "$.nodes[?(@.name=='wave')].Running" -s true

Because the pipeline state is observable, assignments propagate to every connected
client immediately.

Data tools
----------

These convert and inspect ``.tha`` capture files (see :doc:`concepts` and
:doc:`examples/index`):

* ``python -m thalamus.record_reader2 [-i] FILE`` -- print the records in a capture
  file.  ``-n, --node`` filters to specific node(s); ``-s, --stats`` prints a live
  per-node/per-record-type count summary instead of every record (useful for large
  files).
* ``python -m thalamus.dataframe -n NODE -i FILE`` -- export a node's analog or text
  channels to CSV, Parquet, and other tabular formats.
* ``python -m thalamus.av_muxer -i FILE [-o OUTPUT] [-v VIDEO_NODE] [-a AUDIO_NODE]``
  -- mux a recorded video node (``MPEG4``, ``H264`` or ``VP9``) and audio node
  (``AAC`` or ``Opus``) into an mp4 without re-encoding (``-o`` defaults to
  ``output.mp4``; the nodes default to the first with images / encoded audio).  Each
  video frame is placed at its recorded time and the audio by sample count, using each
  record's time and ``encoded_count``.  Both tracks start at the first video frame
  (earlier audio is cut at the nearest packet boundary), and frames before the first
  keyframe are skipped.  Recordings made before encoded audio records carried packets
  timed this way are rejected.  Requires PyAV (``pip install av``).
* ``python -m thalamus.hydrate FILE`` -- convert an entire capture into a single
  HDF5 file (per-channel ``data`` plus ``received`` timing).
* ``python -m thalamus.video_writer -i FILE -n NODE [-o OUTPUT]`` -- extract a
  node's image stream from a capture into a video file (``-o`` defaults to
  ``<node>.mp4``).

Applications
------------

* ``python -m thalamus.pipeline`` -- the data pipeline (no task controller).
* ``python -m thalamus.task_controller`` -- the pipeline plus the behavioral
  :doc:`Task Controller <task_controller>`.
* ``python -m thalamus.eye_calibration`` -- the interactive
  :doc:`Eye Calibration <eye_calibration>` tool.

Both ``thalamus.pipeline`` and ``thalamus.task_controller`` accept:

* ``--open`` -- bind their gRPC/HTTP servers to ``0.0.0.0`` instead of the default
  ``localhost`` only.  By default (since 1.0.41) Thalamus only accepts connections
  from the local machine; pass ``--open`` to allow other machines on the network to
  connect (e.g. a remote :doc:`registry <tools>` client or a multi-machine rig).
  Only do this on a trusted network unless you also enable TLS (below) -- the gRPC
  interface has no authentication of its own.  The HTTP/WebSocket server is never
  exposed: it always binds to ``127.0.0.1``, with or without ``--open``.
* ``-C, --contrib`` -- equivalent to ``--ext thalamus.contrib`` (load the
  thalamus-contrib plugin pack).
* ``-t, --trace`` (``thalamus.pipeline``) -- record a Perfetto trace to
  ``thalamus_<time>.perfetto-trace`` in the working directory.  It is flushed every
  second, so a crash loses at most the last second.
* ``-r, --remote-executor`` (``thalamus.task_controller`` only) -- send task
  execution to a remote executor process instead of running it locally; see
  :ref:`remote-executor` for how trial cancellation is handled in this mode.

At startup both applications also download Cisco's OpenH264 library into
``~/.thalamus`` in the background, which thalamus-contrib uses for H.264 encoding.
Cisco's license requires that you can turn this off: untick *Use OpenH264 to encode
H.264* in *Preferences > H264* (the dialog shows Cisco's attribution and license
text).  Opting out deletes the binary and keeps it from being downloaded; a running
Thalamus keeps encoding until it is restarted.

.. _grpc-tls:

gRPC TLS
^^^^^^^^

``thalamus.pipeline``, ``thalamus.task_controller``, ``thalamus.image_viewer``
and ``thalamus.eye_calibration`` (and the native pipeline and .NET sidecar they
launch, which are passed the same options automatically) accept these flags to
encrypt and authenticate gRPC traffic:

* ``--cert FILE`` -- PEM certificate presented by this process's gRPC servers and
  used as its client certificate.
* ``--key FILE`` -- PEM private key for ``--cert``.  ``--cert`` and ``--key`` must be
  given together.
* ``--ca FILE`` -- PEM certificate authority.  Servers then **require** clients to
  present a certificate signed by it (mutual TLS), and clients verify servers
  against it.  Without ``--ca`` clients verify servers against the system roots and
  servers do not require client certificates.
* ``--server-name NAME`` -- the name clients expect in every server's certificate
  (default ``thalamus.internal``), whatever address they connect to, so
  certificates don't need to list hostnames or IP addresses.

With none of these flags gRPC runs unencrypted, as before.  Because TLS
authenticates clients, enabling it makes the gRPC servers listen on all interfaces
without ``--open``.  Give every machine in a rig (including
:doc:`REMOTE <nodes/remote>` / :doc:`RUNNER2 <nodes/runner2>` targets) the same
``--ca`` and a certificate issued to the same ``--server-name``:

.. code-block:: bash

   python -m thalamus.task_controller --cert rig.pem --key rig.key --ca ca.pem

``thalamus.pipeline`` additionally accepts ``--no-gpu`` to disable GPU/Vulkan
rendering (falls back to software rendering; useful on machines without a usable
GPU driver).
