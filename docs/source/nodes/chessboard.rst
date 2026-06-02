CHESSBOARD
==========

The CHESSBOARD node detects the corners of a chessboard calibration target in an
image stream.  It is a transformer used for camera calibration and pose estimation:
the detected corner grid provides the correspondences needed to solve for a
camera's geometry, and can also track the board's pose over time.

Properties
----------

* **Source**: The camera node to analyze.
* **Rows** / **Columns**: The number of internal corners along each dimension of the
  chessboard.
* **Running**: Begin detecting corners.

For full lens-distortion correction built on chessboard detection, see the
:doc:`distortion` node.
