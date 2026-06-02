XSENS
=====

The XSENS node streams motion-capture pose data and republishes it as a motion
(MOCAP) stream of body segments, each with a position and a quaternion rotation.
It can connect to an Xsens motion-capture source.

Properties
----------

* **Xsens Address**: Address of the motion-capture data source (default
  ``127.0.0.1:6004``).
* **Hand**: Which hand the pose corresponds to (``Left`` or ``Right``).
* **Send Type**: How pose data is sampled/sent (e.g. ``Current``).
* **Actor**: The actor index to stream when multiple actors are present.
* **Poses**: A list of poses to publish, each identified by a segment id and name.

The published segments can be recorded by a STORAGE2 node (via its **Motion**
modality) and consumed by downstream nodes such as HEXASCOPE.
