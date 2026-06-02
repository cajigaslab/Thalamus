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
