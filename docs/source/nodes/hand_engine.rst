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

The node also exposes haptic-feedback controls used to drive the glove's actuators:

* **Num Props**: Number of feedback props/actuators.
* **Amplitude**: Feedback amplitude.
* **Duration (ms)**: Feedback duration in milliseconds.
