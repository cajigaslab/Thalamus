GENICAM
=======

The GENICAM node acquires image streams from cameras that implement the GenICam /
GenTL standard (many machine-vision USB3, GigE, and CoaXPress cameras).  It is a
generator: it produces an image stream that other nodes (STORAGE2, OCULOMATIC,
ARUCO, DISTORTION, ...) can consume.

The appropriate GenTL producer (``.cti``) for your camera vendor must be installed
and discoverable for the camera to appear.

Properties
----------

* **Camera**: The camera to open, selected from the cameras discovered on the system.
* **Frame Rate**: Acquisition frame rate.
* **Exposure**: Exposure time (the value is stored internally as ``ExposureTime`` in
  microseconds).
* **Region of interest**: ``OffsetX``, ``OffsetY``, ``Width``, and ``Height`` define the
  acquired image region.  ``WidthMax`` / ``HeightMax`` reflect the sensor's maximum
  dimensions.  The node widget lets you drag the ROI rectangle directly on the live
  image.

Advanced camera features
------------------------

Cameras expose additional GenICam feature nodes (gain, pixel format, trigger mode,
etc.).  These are surfaced under **Camera Values**, allowing vendor-specific
features to be inspected and set without a code change.
