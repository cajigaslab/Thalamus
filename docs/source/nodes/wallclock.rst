WALLCLOCK
=========

The WALLCLOCK node is a generator that emits the current time as an analog time
series.  It is primarily used as a synchronization reference so that a recording
can be aligned to an absolute clock.

Properties
----------

* **Type**: The clock source to read.

  * ``System``: The local system clock.
  * ``NTP``: A network time (NTP) source.
  * ``PTP``: A Precision Time Protocol source.
* **Integer Values**: Emit the time as integer values instead of floating point.
