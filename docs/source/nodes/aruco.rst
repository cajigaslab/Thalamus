ARUCO
=====

The ARUCO node detects ArUco / AprilTag fiducial markers in an image stream and
outputs the pose (position and rotation) of each configured marker board.  It is a
transformer: it consumes images from a camera node (e.g. GENICAM) and produces
pose data.

Usage
-----

Set the node's source to the camera node to analyze, then configure the marker
dictionary and one or more boards.

Properties
----------

* **Dictionary**: The ArUco marker dictionary the markers were generated from (for
  example a 6x6 dictionary).  This must match the printed markers.
* **Boards**: A list of marker boards to detect.  Each board describes its layout
  (rows, columns, marker size and separation), the marker ids it contains, and an
  optional position/orientation offset so the reported pose is expressed in your
  chosen coordinate frame.

Accurate poses require a calibrated camera; use the DISTORTION node to rectify the
image stream first if your camera has significant lens distortion.
