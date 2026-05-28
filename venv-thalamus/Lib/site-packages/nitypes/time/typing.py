"""Type aliases and type variables for time types."""

from __future__ import annotations

import datetime as dt
from typing import TypeVar, Union

import hightime as ht
from typing_extensions import TypeAlias

import nitypes.bintime as bt

AnyDateTime: TypeAlias = Union[bt.DateTime, dt.datetime, ht.datetime]
"""Type alias for any ``datetime`` class.

This type alias is a union of the following types:

* :class:`nitypes.bintime.DateTime`
* :class:`datetime.datetime`
* :class:`hightime.datetime`
"""

TDateTime = TypeVar("TDateTime", bound=AnyDateTime)
"""Type variable with a bound of :any:`AnyDateTime`."""

AnyTimeDelta: TypeAlias = Union[bt.TimeDelta, dt.timedelta, ht.timedelta]
"""Type alias for any ``timedelta`` class.

This type alias is a union of the following types:

* :class:`nitypes.bintime.TimeDelta`
* :class:`datetime.timedelta`
* :class:`hightime.timedelta`
"""

TTimeDelta = TypeVar("TTimeDelta", bound=AnyTimeDelta)
"""Type variable with a bound of :any:`AnyTimeDelta`."""
