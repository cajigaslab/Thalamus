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
* **Frequency**: Frame rate (Hz) at which new images are generated; also governs the
  node's reported sample interval.  Defaults to ``30``.
* **Jitter (Pixels)**: When non-zero, the pupil's x-position oscillates by ± this
  many pixels every frame (on top of any saccade motion), simulating pixel-level
  tracking noise.  Defaults to ``0`` (no jitter).

In the node's **View** window, clicking (or dragging with a button held) moves the
simulated pupil to the cursor position; passive mouse movement no longer relocates
it.
