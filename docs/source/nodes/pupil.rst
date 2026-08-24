PUPIL
=====

The PUPIL node is a generator that produces a **synthetic eye image** containing a
moving pupil.  It is a test/simulation source: feed its image stream into an
OCULOMATIC node to exercise an eye-tracking pipeline without a real eye camera.

Properties
----------

* **Running**: Generate frames.
* **Width** / **Height**: Dimensions of the generated image.
* **Random Saccade**: When enabled, the simulated pupil jumps to a new random
  position roughly once per second, imitating saccadic eye movements.  When
  disabled the pupil holds still.
* **Frequency**: Frame rate of the generated image stream, in Hz.  Previously the
  node always generated frames on a fixed ~31 Hz (32 ms) interval; this parameter
  makes the rate configurable, e.g. to match the frame rate of the real eye camera
  you are simulating.  Defaults to ``30``.
* **Jitter (Pixels)**: Adds an alternating horizontal offset of ± this many pixels
  to the pupil position on successive frames.  Use it to simulate tracking noise
  and verify that downstream processing (e.g. OCULOMATIC detection or your
  eye-calibration filtering) is robust to frame-to-frame jitter.  ``0`` (the
  default) disables jitter.

In the node's **View** window, clicking (or dragging with a button held) moves the
simulated pupil to the cursor position; passive mouse movement no longer relocates
it.
