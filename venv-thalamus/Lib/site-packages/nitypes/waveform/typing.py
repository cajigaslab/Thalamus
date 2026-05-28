"""Type aliases and type variables for waveforms."""

# These types are in a submodule so they don't show up in autocomplete for "nitypes.waveform." and
# overwhelm users.

from __future__ import annotations

import datetime as dt
from typing import Union

import numpy as np
from typing_extensions import TypeAlias, TypeVar

from nitypes._numpy import bool as _np_bool
from nitypes.time.typing import AnyDateTime, AnyTimeDelta

ExtendedPropertyValue: TypeAlias = Union[bool, float, int, str]
"""Type alias for an ExtendedPropertyDictionary value.

This type alias is a union of the following types:
* :class:`bool`
* :class:`float`
* :class:`int`
* :class:`str`
"""

AnyDigitalPort: TypeAlias = Union[np.uint8, np.uint16, np.uint32]
"""Type alias for any digital port data type.

This type alias is a union of the following types:

* :class:`numpy.uint8`
* :class:`numpy.uint16`
* :class:`numpy.uint32`
"""

# np.byte == np.int8, np.ubyte == np.uint8
AnyDigitalState: TypeAlias = Union[_np_bool, np.int8, np.uint8]
"""Type alias for any digital state data type.

This type alias is a union of the following types:

* :class:`numpy.bool` (NumPy 2.x) or :class:`numpy.bool_` (NumPy 1.x)
* :class:`numpy.int8`
* :class:`numpy.uint8`
"""

TDigitalState = TypeVar("TDigitalState", bound=AnyDigitalState)
"""Type variable with a bound of :any:`AnyDigitalState`."""

TOtherDigitalState = TypeVar("TOtherDigitalState", bound=AnyDigitalState)
"""Another type variable with a bound of :any:`AnyDigitalState`."""

TTimestamp = TypeVar("TTimestamp", bound=AnyDateTime, default=dt.datetime)
"""Type variable for a timestamp."""

TTimestamp_co = TypeVar(
    "TTimestamp_co",
    bound=AnyDateTime,
    covariant=True,
    default=dt.datetime,
)
"""Covariant type variable for a timestamp."""

TTimeOffset = TypeVar(
    "TTimeOffset",
    bound=AnyTimeDelta,
    default=dt.timedelta,
)
"""Type variable for a time offset."""

TTimeOffset_co = TypeVar(
    "TTimeOffset_co",
    bound=AnyTimeDelta,
    covariant=True,
    default=dt.timedelta,
)
"""Covariant type variable for a time offset."""

TSampleInterval = TypeVar(
    "TSampleInterval",
    bound=AnyTimeDelta,
    default=dt.timedelta,
)
"""Type variable for a sample interval."""

TSampleInterval_co = TypeVar(
    "TSampleInterval_co",
    bound=AnyTimeDelta,
    covariant=True,
    default=dt.timedelta,
)
"""Covariant type variable for a sample interval."""

TOtherTimestamp = TypeVar("TOtherTimestamp", bound=AnyDateTime)
"""Another type variable for a timestamp."""

TOtherTimeOffset = TypeVar("TOtherTimeOffset", bound=AnyTimeDelta)
"""Another type variable for a time offset."""

TOtherSampleInterval = TypeVar("TOtherSampleInterval", bound=AnyTimeDelta)
"""Another type variable for a sample interval."""
