ANALOG
======

The ANALOG node is a flexible analog transformer.  In its default mode it passes an
analog stream through, but it can also act as a **touchpad**: when configured as a
touchpad it injects synthetic analog samples derived from the mouse position over
its widget, which is handy for testing downstream nodes without real hardware.

Properties
----------

* **Widget is Touchpad**: When enabled, the node's widget becomes a touchpad and the
  mouse position is injected as analog data.  When disabled the node behaves as a
  plain analog pass-through.

Related nodes: see :doc:`toggle` for thresholding an analog signal into a binary
state, and :doc:`algebra` / :doc:`lua` for arithmetic transforms.
