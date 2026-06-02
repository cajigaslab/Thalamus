CHESSBOARD
==========

The CHESSBOARD node is a generator that *renders* a chessboard pattern as an image
stream.  It produces the calibration target itself (rather than detecting one),
which is useful for displaying a known pattern on a screen or projector -- for
example to calibrate a camera or a display with the :doc:`distortion` node.

Properties
----------

* **Running**: Generate frames (the pattern is redrawn on a timer at roughly 60 Hz).
* **Rows** / **Columns**: The number of squares drawn along each dimension of the
  board.
* **Height**: The height of the generated image; the square size and overall width
  follow from the height and the row/column counts.
