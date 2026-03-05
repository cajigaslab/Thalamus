from __future__ import annotations

import datetime as dt
from functools import singledispatchmethod
from typing import (
    TYPE_CHECKING,
    Any,
    ClassVar,
    SupportsIndex,
    Union,
    cast,
    final,
    overload,
)

import hightime as ht
from typing_extensions import Self, TypeAlias

from nitypes._exceptions import invalid_arg_type, invalid_arg_value
from nitypes.bintime._timedelta import _OTHER_TIMEDELTA_TUPLE, _OtherTimeDelta

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.bintime import TimeDelta, TimeValueTuple
else:
    from nitypes.bintime._time_value_tuple import TimeValueTuple
    from nitypes.bintime._timedelta import TimeDelta

_DT_EPOCH_1904 = dt.datetime(1904, 1, 1, tzinfo=dt.timezone.utc)
_HT_EPOCH_1904 = ht.datetime(1904, 1, 1, tzinfo=dt.timezone.utc)

_OtherDateTime: TypeAlias = Union[dt.datetime, ht.datetime]
_OTHER_DATETIME_TUPLE = (dt.datetime, ht.datetime)


@final
class DateTime:
    """An absolute time in NI Binary Time Format (NI-BTF).

    DateTime represents time as a 128-bit fixed point number with 64-bit whole seconds and
    64-bit fractional seconds.

    .. warning::
        The fractional seconds are represented as a binary fraction, which is a sum of inverse
        powers of 2. Values that are not exactly representable as binary fractions will display
        rounding error or "bruising" similar to a floating point number.

    DateTime instances are duck typing compatible with a subset of the method and properties
    supported by :any:`datetime.datetime` and :any:`hightime.datetime`.

    This class only supports the UTC time zone and does not support timezone-naive times.

    This class does not support the ``fold`` property for disambiguating repeated times for daylight
    saving time and time zone changes.

    Constructing
    ^^^^^^^^^^^^

    As with :any:`datetime.datetime`, you can construct a :class:`DateTime` by specifying the year,
    month, day, etc.:

    >>> import datetime
    >>> DateTime(2025, 5, 25, 16, 45, tzinfo=datetime.timezone.utc)
    nitypes.bintime.DateTime(2025, 5, 25, 16, 45, tzinfo=datetime.timezone.utc)

    .. note::
        :class:`DateTime` only supports :any:`datetime.timezone.utc`. It does not support time-zone-naive
        objects or time zones other than UTC.

    You can also construct a :class:`DateTime` from a :any:`datetime.datetime` or
    :any:`hightime.datetime`:

    >>> DateTime(datetime.datetime(2025, 5, 25, 16, 45, tzinfo=datetime.timezone.utc))
    nitypes.bintime.DateTime(2025, 5, 25, 16, 45, tzinfo=datetime.timezone.utc)
    >>> import hightime
    >>> DateTime(hightime.datetime(2025, 5, 25, 16, 45, tzinfo=datetime.timezone.utc))
    nitypes.bintime.DateTime(2025, 5, 25, 16, 45, tzinfo=datetime.timezone.utc)

    You can get the current time of day by calling :any:`DateTime.now`:

    >>> DateTime.now(datetime.timezone.utc) # doctest: +ELLIPSIS
    nitypes.bintime.DateTime(...)

    Properties
    ^^^^^^^^^^

    Like other ``datetime`` objects, :class:`DateTime` has properties for the year, month, day, hour,
    minute, second, and microsecond.

    >>> import datetime
    >>> x = DateTime(datetime.datetime(2025, 5, 25, 16, 45, tzinfo=datetime.timezone.utc))
    >>> (x.year, x.month, x.day)
    (2025, 5, 25)
    >>> (x.hour, x.minute, x.second, x.microsecond)
    (16, 45, 0, 0)

    Like :any:`hightime.datetime`, it also supports the femtosecond and yoctosecond properties.

    >>> (x.femtosecond, x.yoctosecond)
    (0, 0)

    Resolution
    ^^^^^^^^^^

    NI-BTF is a high-resolution time format, so it has significantly higher resolution than
    :any:`datetime.datetime`. However, :any:`hightime.datetime` has even higher resolution:

    ========================   ================================
    Class                      Smallest Time Increment
    ========================   ================================
    :any:`datetime.datetime`   1 microsecond (1e-6 sec)
    :class:`DateTime`            54210 yoctoseconds (5.4e-20 sec)
    :any:`hightime.datetime`   1 yoctosecond (1e-24 sec)
    ========================   ================================

    As a result, :any:`hightime.datetime` can represent the time down to the exact yoctosecond, but
    :class:`DateTime` rounds the yoctosecond field.

    >>> import hightime
    >>> x = hightime.datetime(2025, 1, 1, yoctosecond=123456789, tzinfo=datetime.timezone.utc)
    >>> x
    hightime.datetime(2025, 1, 1, 0, 0, 0, 0, 0, 123456789, tzinfo=datetime.timezone.utc)
    >>> DateTime(x) # doctest: +NORMALIZE_WHITESPACE
    nitypes.bintime.DateTime(2025, 1, 1, 0, 0, 0, 0, 0, 123436417, tzinfo=datetime.timezone.utc)

    Rounding
    ^^^^^^^^

    NI-BTF represents fractional seconds as a binary fraction, which is a sum of inverse
    powers of 2. Values that are not exactly representable as binary fractions will display
    rounding error or "bruising" similar to a floating point number.

    For example, it may round 100 microseconds down to 99.9999... microseconds.

    >>> x = hightime.datetime(2025, 1, 1, microsecond=100, tzinfo=datetime.timezone.utc)
    >>> x
    hightime.datetime(2025, 1, 1, 0, 0, 0, 100, tzinfo=datetime.timezone.utc)
    >>> DateTime(x) # doctest: +NORMALIZE_WHITESPACE
    nitypes.bintime.DateTime(2025, 1, 1, 0, 0, 0, 99, 999999999, 999991239,
        tzinfo=datetime.timezone.utc)

    Class members
    ^^^^^^^^^^^^^
    """  # noqa: W505 - doc line too long

    min: ClassVar[DateTime]
    """The earliest supported :class:`DateTime` object, midnight on Jan 1, 0001, UTC."""

    max: ClassVar[DateTime]
    """The latest supported :class:`DateTime` object, before midnight on Dec 31, 9999, UTC."""

    __slots__ = ["_offset", "_hightime_cache"]

    _offset: TimeDelta
    _hightime_cache: ht.datetime | None

    @overload
    def __init__(self) -> None: ...  # noqa: D107 - missing docstring in __init__

    @overload
    def __init__(  # noqa: D107 - missing docstring in __init__
        self, value: _OtherDateTime, /
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - missing docstring in __init__
        self,
        year: SupportsIndex,
        month: SupportsIndex,
        day: SupportsIndex,
        hour: SupportsIndex = ...,
        minute: SupportsIndex = ...,
        second: SupportsIndex = ...,
        microsecond: SupportsIndex = ...,
        femtosecond: SupportsIndex = ...,
        yoctosecond: SupportsIndex = ...,
        tzinfo: dt.tzinfo | None = None,
    ) -> None: ...

    def __init__(
        self,
        year: SupportsIndex | _OtherDateTime | None = None,
        month: SupportsIndex | None = None,
        day: SupportsIndex | None = None,
        hour: SupportsIndex = 0,
        minute: SupportsIndex = 0,
        second: SupportsIndex = 0,
        microsecond: SupportsIndex = 0,
        femtosecond: SupportsIndex = 0,
        yoctosecond: SupportsIndex = 0,
        tzinfo: dt.tzinfo | None = None,
    ) -> None:
        """Initialize a DateTime."""
        if isinstance(year, SupportsIndex):
            self._offset = self.__class__._to_offset(
                ht.datetime(
                    year,
                    cast(SupportsIndex, month),
                    cast(SupportsIndex, day),
                    hour,
                    minute,
                    second,
                    microsecond,
                    femtosecond,
                    yoctosecond,
                    tzinfo,
                )
            )
        else:
            self._offset = self.__class__._to_offset(year)

        # Do not cache the passed-in ht.datetime because that would hide the rounding error
        # caused by using a binary fraction.
        self._hightime_cache = None

    @singledispatchmethod
    @classmethod
    def _to_offset(cls, value: object) -> TimeDelta:
        raise invalid_arg_type("value", "datetime", value)

    # Python 3.9: pass the type to register() in order to work around
    # https://github.com/python/cpython/issues/86153 - singledispatchmethod raises an error when
    # relying on a forward declaration
    @_to_offset.register(ht.datetime)
    @classmethod
    def _(cls, value: ht.datetime) -> TimeDelta:
        if value.tzinfo != dt.timezone.utc:
            raise ValueError("The tzinfo must be datetime.timezone.utc.")
        return TimeDelta(value - _HT_EPOCH_1904)

    @_to_offset.register(dt.datetime)
    @classmethod
    def _(cls, value: dt.datetime) -> TimeDelta:
        if value.tzinfo != dt.timezone.utc:
            raise ValueError("The tzinfo must be datetime.timezone.utc.")
        return TimeDelta(value - _DT_EPOCH_1904)

    @_to_offset.register(type(None))
    @classmethod
    def _(cls, value: None) -> TimeDelta:
        return TimeDelta()

    @classmethod
    def from_ticks(cls, ticks: SupportsIndex) -> Self:
        """Create an DateTime from a 128-bit fixed point number expressed as an integer."""
        self = cls.__new__(cls)
        self._offset = TimeDelta.from_ticks(ticks)
        self._hightime_cache = None
        return self

    @classmethod
    def from_tuple(cls, value: TimeValueTuple) -> Self:
        """Create a DateTime from whole and fractional seconds as 64-bit ints."""
        self = cls.__new__(cls)
        self._offset = TimeDelta.from_tuple(value)
        self._hightime_cache = None
        return self

    @classmethod
    def from_offset(cls, offset: TimeDelta) -> Self:
        """Create an DateTime from a TimeValue offset from the epoch, Jan 1, 1904."""
        self = cls.__new__(cls)
        self._offset = offset
        self._hightime_cache = None
        return self

    def _to_datetime_datetime(self) -> dt.datetime:
        """Return self as a :any:`datetime.datetime`."""
        return _DT_EPOCH_1904 + self._offset._to_datetime_timedelta()

    def _to_hightime_datetime(self) -> ht.datetime:
        """Return self as a :any:`hightime.datetime`."""
        if self._hightime_cache is None:
            self._hightime_cache = _HT_EPOCH_1904 + self._offset._to_hightime_timedelta()
        return self._hightime_cache

    # Calculating the year/month/day requires knowledge of leap years, days per month, etc., so
    # defer to hightime.datetime.
    @property
    def year(self) -> int:
        """The year."""
        return self._to_hightime_datetime().year

    @property
    def month(self) -> int:
        """The month, between 1 and 12 inclusive."""
        return self._to_hightime_datetime().month

    @property
    def day(self) -> int:
        """The day of the month, between 1 and 31 inclusive."""
        return self._to_hightime_datetime().day

    # The hour/minute/second properties currently assume that tzinfo.utcoffset() == 0, which is
    # true because this class only supports UTC.
    @property
    def hour(self) -> int:
        """The hour, between 0 and 23 inclusive."""
        return self._offset.seconds // 3600

    @property
    def minute(self) -> int:
        """The minute, between 0 and 59 inclusive."""
        return (self._offset.seconds // 60) % 60

    @property
    def second(self) -> int:
        """The second, between 0 and 59 inclusive."""
        return self._offset.seconds % 60

    @property
    def microsecond(self) -> int:
        """The microsecond, between 0 and 999_999 inclusive."""
        return self._offset.microseconds

    @property
    def femtosecond(self) -> int:
        """The femtosecond, between 0 and 999_999_999 inclusive."""
        return self._offset.femtoseconds

    @property
    def yoctosecond(self) -> int:
        """The yoctosecond, between 0 and 999_999_999 inclusive.

        .. warning::
            Because this class uses a 64-bit binary fraction, the smallest time increment it can
            represent is ``1.0 / (1 << 64)`` seconds, which is about 54210 yoctoseconds.
        """
        return self._offset.yoctoseconds

    @property
    def ticks(self) -> int:
        """The number of ticks since the epoch, Jan 1, 1904."""
        return self._offset.ticks

    @property
    def tzinfo(self) -> dt.tzinfo | None:
        """The time zone."""
        return dt.timezone.utc

    def to_tuple(self) -> TimeValueTuple:
        """Convert to the number of whole and fractional seconds since the epoch, Jan 1, 1904."""
        return self._offset.to_tuple()

    @classmethod
    def now(cls, tz: dt.tzinfo | None = None) -> Self:
        """Return the current absolute time."""
        if tz != dt.timezone.utc:
            raise invalid_arg_value("tz", "datetime.timezone.utc", tz)
        return cls(ht.datetime.now(tz))

    def __add__(self, value: TimeDelta | _OtherTimeDelta, /) -> DateTime:
        """Return self+value."""
        if isinstance(value, TimeDelta):
            return self.__class__.from_offset(self._offset + value)
        elif isinstance(value, _OTHER_TIMEDELTA_TUPLE):
            return self + TimeDelta(value)
        else:
            return NotImplemented

    __radd__ = __add__

    @overload
    def __sub__(  # noqa: D105 - missing docstring for magic method
        self, value: DateTime | _OtherDateTime, /
    ) -> TimeDelta: ...
    @overload
    def __sub__(  # noqa: D105 - missing docstring for magic method
        self, value: TimeDelta | _OtherTimeDelta, /
    ) -> DateTime: ...

    def __sub__(
        self, value: DateTime | _OtherDateTime | TimeDelta | _OtherTimeDelta, /
    ) -> TimeDelta | DateTime:
        """Return self-value."""
        if isinstance(value, DateTime):
            return self._offset - value._offset
        elif isinstance(value, _OTHER_DATETIME_TUPLE):
            return self - self.__class__(value)
        elif isinstance(value, TimeDelta):
            return self.__class__.from_offset(self._offset - value)
        elif isinstance(value, _OTHER_TIMEDELTA_TUPLE):
            return self - TimeDelta(value)
        else:
            return NotImplemented

    @overload
    def __rsub__(  # noqa: D105 - missing docstring for magic method
        self, value: DateTime | _OtherDateTime, /
    ) -> TimeDelta: ...
    @overload
    def __rsub__(  # noqa: D105 - missing docstring for magic method
        self, value: TimeDelta | _OtherTimeDelta, /
    ) -> DateTime: ...

    def __rsub__(
        self, value: DateTime | _OtherDateTime | TimeDelta | _OtherTimeDelta, /
    ) -> TimeDelta | DateTime:
        """Return value-self."""
        if isinstance(value, DateTime):
            return value._offset - self._offset
        elif isinstance(value, _OTHER_DATETIME_TUPLE):
            return self.__class__(value) - self
        elif isinstance(value, TimeDelta):
            return self.__class__.from_offset(value - self._offset)
        elif isinstance(value, _OTHER_TIMEDELTA_TUPLE):
            return TimeDelta(value) - self
        else:
            return NotImplemented

    # In comparison operators, always promote to the more precise data type (dt -> bt, bt -> ht).
    def __lt__(self, value: DateTime | _OtherDateTime, /) -> bool:
        """Return self<value."""
        if isinstance(value, self.__class__):
            return self._offset < value._offset
        elif isinstance(value, ht.datetime):
            return self._to_hightime_datetime() < value
        elif isinstance(value, dt.datetime):
            return self < self.__class__(value)
        else:
            return NotImplemented

    def __le__(self, value: DateTime | _OtherDateTime, /) -> bool:
        """Return self<=value."""
        if isinstance(value, self.__class__):
            return self._offset <= value._offset
        elif isinstance(value, ht.datetime):
            return self._to_hightime_datetime() <= value
        elif isinstance(value, dt.datetime):
            return self <= self.__class__(value)
        else:
            return NotImplemented

    def __eq__(self, value: object, /) -> bool:
        """Return self==value."""
        if isinstance(value, self.__class__):
            return self._offset == value._offset
        elif isinstance(value, ht.datetime):
            return self._to_hightime_datetime() == value
        elif isinstance(value, dt.datetime):
            return self == self.__class__(value)
        else:
            return NotImplemented

    def __gt__(self, value: DateTime | _OtherDateTime, /) -> bool:
        """Return self<value."""
        if isinstance(value, self.__class__):
            return self._offset > value._offset
        elif isinstance(value, ht.datetime):
            return self._to_hightime_datetime() > value
        elif isinstance(value, dt.datetime):
            return self > self.__class__(value)
        else:
            return NotImplemented

    def __ge__(self, value: DateTime | _OtherDateTime, /) -> bool:
        """Return self>=value."""
        if isinstance(value, self.__class__):
            return self._offset >= value._offset
        elif isinstance(value, ht.datetime):
            return self._to_hightime_datetime() >= value
        elif isinstance(value, dt.datetime):
            return self >= self.__class__(value)
        else:
            return NotImplemented

    def __hash__(self) -> int:
        """Return hash(self)."""
        return hash(self._offset)

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        return (self.__class__.from_ticks, (self._offset._ticks,))

    def __str__(self) -> str:
        """Return repr(self)."""
        return str(self._to_hightime_datetime())

    def __repr__(self) -> str:
        """Return repr(self)."""
        args: list[int | str] = [self.year, self.month, self.day, self.hour, self.minute]
        # Only display sub-minute fields if they aren't 0.
        if self.yoctosecond:
            args.extend([self.second, self.microsecond, self.femtosecond, self.yoctosecond])
        elif self.femtosecond:
            args.extend([self.second, self.microsecond, self.femtosecond])
        elif self.microsecond:
            args.extend([self.second, self.microsecond])
        elif self.second:
            args.append(self.second)
        args.append("tzinfo=datetime.timezone.utc")
        return f"{self.__class__.__module__}.{self.__class__.__name__}({', '.join(map(str, args))})"


# These have to be within dt.datetime.max/min or else delegating to dt.datetime or ht.datetime for
# year/month/day, str(), repr(), etc. will fail. Use ticks to specify the maximum fractional second
# without rounding up to MAXYEAR+1.
DateTime.max = DateTime(
    dt.MAXYEAR,
    12,
    31,
    23,
    59,
    59,
    tzinfo=dt.timezone.utc,
) + TimeDelta.from_ticks(0xFFFF_FFFF_FFFF_FFFF)
DateTime.min = DateTime(dt.MINYEAR, 1, 1, tzinfo=dt.timezone.utc)
