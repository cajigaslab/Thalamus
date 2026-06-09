Eye Calibration
===============

The eye-calibration tool maps the **raw eye-camera signal** produced by the
:doc:`OCULOMATIC <nodes/oculomatic>` node into **gaze / screen coordinates**, so that
gaze can be used for gaze-contingent behavioral tasks and analysis.  It is an
interactive application: an operator collects a few known fixations, fits a
calibration model, and refines it live.

Running it
----------

Start a Thalamus pipeline that includes an OCULOMATIC node (producing ``X`` / ``Y``
gaze channels), then launch the calibrator, which connects to the pipeline over gRPC
(default ``localhost:50050``):

.. code-block::

   python -m thalamus.eye_calibration

Two windows open: a **subject view** (what the subject sees -- fixation point and
saccade targets) and an **operator view** (the interactive calibration interface).

Calibration models
-------------------

The model is chosen with the **Model** dropdown and stored under
``eye_scaling`` in the configuration.

* **Projective** -- an 8-parameter projective (homography) mapping from raw eye
  coordinates to eye angles, followed by an angle-to-pixel conversion that uses the
  screen **Distance (m)** and **DPI**.  Fitting solves a least-squares problem over
  the collected fixation/target pairs.
* **Angular Scaling** -- a polar model: the raw signal's angle selects an
  interpolated **scale** and **rotation** from a set of *pins*, allowing the gain to
  vary by direction (independent X/Y behavior).  Interpolation wraps around the
  circle, and a **Scale Default** is used before any pins are set.

The fitted parameters (the projective ``Parameters`` / ``Distance (m)`` / ``DPI``, or
the Angular-Scaling ``Pins`` / ``Scale Default``) live in the pipeline config, so the
calibration is saved with the experiment and reused downstream.

Workflow
--------

1. **Show targets.** Add saccade targets in the operator view and cycle through them
   so the subject fixates each in turn.
2. **Collect samples.** With the subject fixating a target, record the current gaze
   as a training sample for that target.
3. **Fit.** Press **Fit** to solve the selected model from the collected samples.
4. **Refine.** For Angular Scaling you can **nudge** pins by dragging to locally
   adjust scale/rotation; **undo/redo** is available for every change.
5. **Reset / Clear.** **Reset** restores the model to defaults; **Clear** wipes the
   accumulated gaze trace without discarding the calibration.

Operator controls
-----------------

* **Model** -- select the calibration model.
* **Fit** / **Reset** -- fit the model to the samples / restore defaults.
* **Clear** -- clear the displayed gaze trace.
* **Hold** -- keep accumulating the gaze trace instead of trimming it to the most
  recent points.
* **Fixation Radius** / **Saccade Radius** -- on-screen sizes of the fixation point
  and the saccade targets.
* **Reward (ms)** and **Reward Node** -- duration of the reward pulse and the name of
  the node it is injected into.  A keypress in the operator view delivers a reward,
  which is injected as an analog signal on the reward node -- useful for shaping
  behavior during calibration.

Keyboard shortcuts in the operator view cover undo/redo, delivering a reward, and
cycling the active saccade target.

Relationship to the rest of Thalamus
------------------------------------

* Input comes from the :doc:`OCULOMATIC <nodes/oculomatic>` node's gaze channels.
* The **reward** is delivered by injecting an analog signal into a node you nominate
  (e.g. a :doc:`NIDAQ_OUT <nodes/nidaq_out>` channel driving a reward device).
* The resulting calibration is consumed by the :doc:`Task Controller
  <task_controller>` so behavioral tasks can use calibrated gaze in screen
  coordinates.
