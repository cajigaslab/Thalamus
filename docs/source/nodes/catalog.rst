Node Catalog
============

Thalamus ships with a large library of node types.  Every node falls into one of
four roles:

* **Generators** produce data (hardware acquisition, signal generation).
* **Consumers** take data in and do something terminal with it (storage, logging, display).
* **Transformers** consume data and produce new data (analysis, coordinate mapping, scripting).
* **Controllers** coordinate the pipeline (starting/stopping groups of nodes).

The tables below list the node types available in this release.  Select a node's
type from the dropdown in its row in the node list to switch a node to that type.
Nodes documented in detail have their own page linked from the
:doc:`Nodes index <index>`.

Generators
----------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Type
     - Description
   * - ``WAVE``
     - Software signal generator (sine, square, triangle, random) with configurable frequency, amplitude, phase, offset and duty cycle.  See :doc:`wave`.
   * - ``NIDAQ (NIDAQMX)``
     - Reads analog signals from a National Instruments DAQ.  See :doc:`nidaq`.
   * - ``WALLCLOCK``
     - Emits the current time (system clock, NTP, or PTP) as a time series for synchronization.
   * - ``INTAN``
     - Streams neural recordings from an Intan RHX acquisition system over TCP.  See :doc:`intan`.
   * - ``SPIKEGLX``
     - Streams high-density electrophysiology data from a SpikeGLX / Neuropixels system.  See :doc:`spikeglx`.
   * - ``BRAINPRODUCTS``
     - Streams EEG/biosignal data from Brain Products amplifiers.
   * - ``DELSYS``
     - Streams wireless EMG data from Delsys sensors.  See :doc:`delsys`.
   * - ``GENICAM``
     - Acquires image streams from GenICam/GenTL-compliant cameras.  See :doc:`genicam`.
   * - ``VIDEO`` / ``FFMPEG``
     - Decodes video/image streams from files or capture devices via FFmpeg.
   * - ``XSENS`` / ``MOCAP``
     - Streams motion-capture pose data (segment positions and quaternion rotations).  See :doc:`xsens`.
   * - ``HAND_ENGINE``
     - Streams hand/finger pose data from StretchSense Hand Engine.
   * - ``PUPIL``
     - Detects pupil position and size from a video stream.
   * - ``REMOTE``
     - Proxies a data stream from another Thalamus instance over gRPC.
   * - ``SAMPLE_MONITOR``
     - Reports sample counts and timing statistics from upstream nodes (diagnostic).

Consumers
---------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Type
     - Description
   * - ``STORAGE2``
     - Primary node for recording data to a ``.tha`` capture file, with per-modality selection, file copying, and metadata.  See :doc:`storage2`.
   * - ``STORAGE``
     - Earlier-generation storage node (retained for backward compatibility).
   * - ``NIDAQ_OUT (NIDAQMX)``
     - Writes analog signals from Thalamus out to a National Instruments DAQ.
   * - ``LOG``
     - Receives and displays text log messages.
   * - ``REMOTE_LOG``
     - Aggregates log messages from a remote Thalamus instance.
   * - ``STIM_PRINTER``
     - Logs JSON-formatted electrical stimulation declarations.
   * - ``TOUCH_SCREEN``
     - Maps raw touch-screen coordinates to calibrated screen coordinates.  See :doc:`touch_screen`.
   * - ``OPHANIM``
     - Drives a panoramic/sphere projection display over gRPC.
   * - ``TASK_CONTROLLER``
     - Starts/stops an external behavioral task-controller service.

Transformers
------------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Type
     - Description
   * - ``ALGEBRA``
     - Evaluates a user-supplied algebraic expression on incoming samples.  See :doc:`algebra`.
   * - ``LUA``
     - Evaluates Lua expressions on incoming samples, with state preserved across samples.  See :doc:`lua`.
   * - ``NORMALIZE``
     - Linearly rescales an input range to a configured output range, with calibration caching.  See :doc:`normalize`.
   * - ``ANALOG``
     - Pass-through / touchpad analog node that can inject synthetic input from the mouse.
   * - ``TOGGLE``
     - Emits a binary 0/1 state based on a threshold.
   * - ``CHANNEL_PICKER``
     - Selects and reorders channels from one or more upstream sources.  See :doc:`channel_picker`.
   * - ``FREQUENCY``
     - Monitors a channel's frequency and flags drift outside configured margins.
   * - ``SYNC``
     - Cross-correlates paired channels from different nodes to measure timing alignment.  See :doc:`sync`.
   * - ``OCULOMATIC``
     - Detects gaze position from an eye-camera video stream and outputs scaled analog coordinates.  See :doc:`oculomatic`.
   * - ``ARUCO``
     - Detects ArUco/AprilTag fiducial markers in images and outputs board poses.  See :doc:`aruco`.
   * - ``CHESSBOARD``
     - Detects chessboard corners in images for camera calibration / pose estimation.
   * - ``DISTORTION``
     - Applies camera-distortion correction to image streams.
   * - ``HEXASCOPE``
     - Servos a motorized mirror platform to track a target from a motion-capture source.
   * - ``CECI``
     - Generates multi-channel biphasic electrical stimulation with MUX/digital control.
   * - ``ROS2``
     - Bridges Thalamus data (images, gaze, transforms) to/from ROS 2 topics and TF2.

Controllers and Utilities
--------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Type
     - Description
   * - ``RUNNER2``
     - Propagates its Running state to a configured list of (possibly remote) nodes so multiple nodes start/stop together.  See :doc:`runner2`.
   * - ``RUNNER``
     - Simpler controller that mirrors its Running state to a list of local node names.
   * - ``NONE``
     - The default empty node; does nothing until you choose a type.
   * - ``THREAD_POOL``
     - Exposes the shared worker thread pool (system/diagnostic).
   * - ``LOOP_TEST`` / ``TEST_PULSE_NODE``
     - Diagnostic nodes that generate test signals for verifying signal routing and latency.
