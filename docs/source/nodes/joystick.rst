JOYSTICK
========

The JOYSTICK node is a generator that reads a 2-axis analog joystick over a serial
connection and publishes normalized ``X`` / ``Y`` position channels plus a
measured update-rate channel.  It owns the serial connection directly, so it has
no ``Source`` field -- point it at the device's serial port and turn it on.

The device is expected to send newline-terminated lines, either as bare CSV
(``raw_x,raw_y``) or as labeled fields (``x=<value>, y=<value>``, case
insensitive).  Raw values are treated as 0-1023 ADC-style readings.

Properties
----------

* **Port**: Serial device path for the joystick (for example ``/dev/ttyACM0``).
  The connection is opened at a fixed 115200 baud.
* **Running**: Open the serial port and start reading.  If the port fails to
  open, an error dialog is shown and Running is reset to off.
* **X Center** / **Y Center**: The raw ADC value that corresponds to the
  joystick's rest position for each axis (defaults ``516`` / ``514``).  Used to
  center the normalized output around 0.
* **Dead Zone**: Half-width, in raw ADC units, of a band around each axis's
  center that is snapped to exactly 0 -- suppresses jitter when the joystick is
  at rest (default ``3``).
* **Invert X** / **Invert Y**: Negate the normalized value for that axis
  (``Invert X`` defaults on, ``Invert Y`` defaults off).

Outputs
-------

An analog channel set:

* ``X`` / ``Y`` -- normalized axis position, roughly in ``[-1, 1]`` after
  centering, dead-zone snapping, and any inversion.
* ``Frequency`` -- the measured rate (Hz) at which the device is sending
  updates, computed from the time between successive parsed lines.

This node cannot accept externally injected data (it is a hardware source
only).
