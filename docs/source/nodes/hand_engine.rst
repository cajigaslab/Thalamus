HAND_ENGINE
===========

The HAND_ENGINE node streams hand and finger pose data from StretchSense Hand
Engine and republishes it as a motion-capture (MOCAP) stream of segments, each with
a position and rotation.  It is a generator used to capture hand kinematics from a
motion-capture glove.

Properties
----------

* **Address**: The address of the Hand Engine stream (``host:port``; the default port
  is ``9000``).
* **Running**: Connect to Hand Engine and begin streaming.

In addition to the MOCAP segments, the node emits analog channels derived from the
pose: a ``Pose Change`` channel and per-finger distance channels (``Thumb Distance
(m)``, ``Index Distance (m)``, etc.).  Two parameters shape the emitted ``Pose
Change`` signal:

* **Amplitude**: The value emitted on the ``Pose Change`` channel when a pose change
  occurs.
* **Duration (ms)**: How long, in milliseconds, that emitted pose-change pulse lasts.
