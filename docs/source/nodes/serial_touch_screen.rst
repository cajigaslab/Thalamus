SERIAL_TOUCH_SCREEN
====================

The SERIAL_TOUCH_SCREEN node is a generator that reads raw touch coordinates
directly from a serial-attached touch controller and publishes them as an
analog channel set, plus a measured update-rate channel.  It owns the serial
connection itself (there is no ``Source`` field) -- this is the raw hardware
source, in contrast to the :doc:`TOUCH_SCREEN <touch_screen>` node, which
*transforms* an already-flowing raw touch stream into calibrated screen
coordinates.

The controller is expected to speak a fixed byte-oriented protocol (a
``0x55 0x54`` header followed by status/X/Y/touch-id/checksum fields); the
node parses this stream directly, so no additional framing configuration is
needed.

Properties
----------

* **Port**: Serial device path for the touch controller (for example
  ``/dev/ttyUSB0``).  The connection is opened at a fixed 19200 baud.
* **Running**: Open the serial port and start reading.  If the port fails to
  open, an error dialog is shown and Running is reset to off.
* **No Touch Timeout (ms)**: If no valid packet has been parsed within this
  many milliseconds of the last one, ``X`` / ``Y`` are reset to 0 to signal
  "no touch" (default ``30``).

Outputs
-------

An analog channel set:

* ``X`` / ``Y`` -- raw (uncalibrated) touch coordinates reconstructed from the
  device's byte protocol.
* ``Frequency`` -- the measured rate (Hz) at which complete, valid packets are
  being received.

Pipe this node's ``X`` / ``Y`` channels into a :doc:`TOUCH_SCREEN
<touch_screen>` node to map them onto calibrated screen coordinates.  This
node cannot accept externally injected data (it is a hardware source only).
