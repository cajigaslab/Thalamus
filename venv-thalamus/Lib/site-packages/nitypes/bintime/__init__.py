r"""Binary time data types for NI Python APIs.

NI Binary Time Format
=====================

This module implements the NI Binary Time Format (`NI-BTF`_), a high-resolution time format used by
NI software.

An NI-BTF time value is a 128-bit fixed point number consisting of a 64-bit whole seconds part and a
64-bit fractional seconds part. There are two types of NI-BTF time values:

* An NI-BTF absolute time represents a point in time as the number of seconds after midnight,
  January 1, 1904, UTC.
* An NI-BTF time interval represents a difference between two points in time.

NI-BTF time types are also supported in LabVIEW, LabWindows/CVI, and .NET. You can use NI-BTF time
to efficiently share high-resolution date-time information with other NI application development
environments.

.. _ni-btf: https://www.ni.com/docs/en-US/bundle/labwindows-cvi/page/cvi/libref/ni-btf.htm

NI-BTF Python Data Types
========================

* :class:`DateTime`: represents an NI-BTF absolute time as a Python object.
* :class:`DateTimeArray`: an array of :class:`DateTime` values.
* :class:`TimeDelta`: represents a NI-BTF time interval as a Python object.
* :class:`TimeDeltaArray`: an array of :class:`TimeDelta` values.

NI-BTF NumPy Structured Data Types
==================================

:any:`CVIAbsoluteTimeDType` and :any:`CVITimeIntervalDType` are NumPy structured data type objects
representing the ``CVIAbsoluteTime`` and ``CVITimeInterval`` C structs. These structured data types
can be used to efficiently represent NI-BTF time values in NumPy arrays or pass them to/from C DLLs.

.. warning::
    :any:`CVIAbsoluteTimeDType` and :any:`CVITimeIntervalDType` have the same layout and field
    names, so NumPy and type checkers such as Mypy currently treat them as the same type.

NI-BTF versus ``hightime``
==========================

NI also provides the ``hightime`` Python package, which extends the standard Python :mod:`datetime`
module to support up to yoctosecond precision.

``nitypes.bintime`` is not a replacement for ``hightime``. The two time formats have different
strengths and weaknesses.

* ``hightime`` supports local time zones and time-zone-naive times. ``bintime`` only supports UTC.
* ``hightime`` classes supports the same operations as the standard ``datetime`` classes.
  ``bintime`` classes support a subset of the standard ``datetime`` operations.
* ``hightime`` has a larger memory footprint than NI-BTF. ``hightime`` objects are separately
  allocated from the heap. ``bintime`` offers the choice of separately allocated Python objects or
  a more compact NumPy representation that can store multiple timestamps in a single block of
  memory.
* ``hightime`` requires conversion to/from NI-BTF when calling the NI driver C APIs from Python.
  ``nitypes.bintime`` includes reusable conversion routines for NI driver Python APIs to use.

NI-BTF versus :any:`numpy.datetime64`
=====================================

NumPy provides the :any:`numpy.datetime64` data type, which is even more compact than NI-BTF.
However, it has lower resolution than NI-BTF and is not interoperable with NI driver C APIs that use
NI-BTF.
"""

from __future__ import annotations

from nitypes.bintime._datetime import DateTime
from nitypes.bintime._datetime_array import DateTimeArray
from nitypes.bintime._dtypes import (
    CVIAbsoluteTimeBase,
    CVIAbsoluteTimeDType,
    CVITimeIntervalBase,
    CVITimeIntervalDType,
)
from nitypes.bintime._time_value_tuple import TimeValueTuple
from nitypes.bintime._timedelta import TimeDelta
from nitypes.bintime._timedelta_array import TimeDeltaArray

__all__ = [
    "DateTime",
    "DateTimeArray",
    "CVIAbsoluteTimeBase",
    "CVIAbsoluteTimeDType",
    "CVITimeIntervalBase",
    "CVITimeIntervalDType",
    "TimeDelta",
    "TimeDeltaArray",
    "TimeValueTuple",
]

# Hide that it was defined in a helper file
DateTime.__module__ = __name__
DateTimeArray.__module__ = __name__
TimeDelta.__module__ = __name__
TimeDeltaArray.__module__ = __name__
TimeValueTuple.__module__ = __name__
