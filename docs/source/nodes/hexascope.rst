HEXASCOPE
=========

The HEXASCOPE node controls a motorized mirror platform that steers an optical path
to follow a moving target.  It is a transformer that consumes motion-capture pose
data and drives the platform so that a chosen objective stays aimed at a chosen
field point.

Properties
----------

* **Motion Tracking Node**: The motion-capture (MOCAP) source that provides target
  poses.
* **Objective Pose**: The pose (from the source) that represents the objective being
  aimed.
* **Field Pose**: The pose that represents the field/target point to track.
* **Running**: Engage tracking and begin moving the platform.

Before tracking, the platform must be **homed**; the node reports its **Homing** /
**Homed** status while it establishes a known reference position.
