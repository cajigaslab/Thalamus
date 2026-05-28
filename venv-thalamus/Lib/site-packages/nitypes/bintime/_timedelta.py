from __future__ import annotations

import datetime as dt
import decimal
import math
import operator
from decimal import Decimal
from functools import singledispatchmethod
from typing import TYPE_CHECKING, Any, ClassVar, SupportsIndex, Union, final, overload

import hightime as ht
from typing_extensions import Self, TypeAlias

from nitypes._arguments import arg_to_int
from nitypes._exceptions import int_out_of_range, invalid_arg_type

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.bintime import TimeValueTuple
else:
    from nitypes.bintime._time_value_tuple import TimeValueTuple


_INT64_MAX = (1 << 63) - 1
_INT64_MIN = -(1 << 63)
_UINT64_MAX = (1 << 64) - 1
_UINT64_MIN = 0
_INT128_MAX = (1 << 127) - 1
_INT128_MIN = -(1 << 127)

_BITS_PER_SECOND = 64
_TICKS_PER_SECOND = 1 << _BITS_PER_SECOND
_FRACTIONAL_SECONDS_MASK = _TICKS_PER_SECOND - 1

_SECONDS_PER_DAY = 86400

_MICROSECONDS_PER_SECOND = 10**6
_FEMTOSECONDS_PER_SECOND = 10**15
_YOCTOSECONDS_PER_SECOND = 10**24

_FEMTOSECONDS_PER_MICROSECOND = _FEMTOSECONDS_PER_SECOND // _MICROSECONDS_PER_SECOND
_YOCTOSECONDS_PER_FEMTOSECOND = _YOCTOSECONDS_PER_SECOND // _FEMTOSECONDS_PER_SECOND

_DECIMAL_DIGITS = 64
_REPR_TICKS = False


_OtherTimeDelta: TypeAlias = Union[dt.timedelta, ht.timedelta]
_OTHER_TIMEDELTA_TUPLE = (dt.timedelta, ht.timedelta)

_OtherDateTime: TypeAlias = Union[dt.datetime, ht.datetime]


@final
class TimeDelta:
    """A duration, represented in NI Binary Time Format (NI-BTF).

    TimeDelta represents time as a 128-bit fixed point number with 64-bit whole seconds and 64-bit
    fractional seconds.

    .. warning::
        The fractional seconds are represented as a binary fraction, which is a sum of inverse
        powers of 2. Values that are not exactly representable as binary fractions will display
        rounding error or "bruising" similar to a floating point number.

    TimeDelta instances are duck typing compatible with :any:`datetime.timedelta` and
    :any:`hightime.timedelta`.

    Constructing
    ^^^^^^^^^^^^

    You can construct a :class:`TimeDelta` from a number of seconds, expressed as an :any:`int`,
    :any:`float`, or :any:`decimal.Decimal`.

    >>> TimeDelta(100)
    nitypes.bintime.TimeDelta(Decimal('100'))
    >>> TimeDelta(100.125)
    nitypes.bintime.TimeDelta(Decimal('100.125'))
    >>> from decimal import Decimal
    >>> TimeDelta(Decimal("100.125"))
    nitypes.bintime.TimeDelta(Decimal('100.125'))

    :class:`TimeDelta` has the same resolution and rounding behavior as :class:`DateTime`.

    >>> TimeDelta(Decimal("100.01234567890123456789"))
    nitypes.bintime.TimeDelta(Decimal('100.012345678901234567889'))

    Unlike other ``timedelta`` objects, you cannot construct a :class:`TimeDelta` from separate weeks,
    days, hours, etc. If you want to do that, construct a :any:`datetime.timedelta` or
    :any:`hightime.timedelta` and then use it to construct a :class:`TimeDelta`.

    >>> import datetime, hightime
    >>> TimeDelta(datetime.timedelta(days=1, microseconds=1))
    nitypes.bintime.TimeDelta(Decimal('86400.0000010000000000000'))
    >>> TimeDelta(hightime.timedelta(days=1, femtoseconds=1))
    nitypes.bintime.TimeDelta(Decimal('86400.0000000000000010000'))

    Math Operations
    ^^^^^^^^^^^^^^^

    :class:`DateTime` and :class:`TimeDelta` support the same math operations as :any:`datetime.datetime`
    and :any:`datetime.timedelta`.

    For example, you can add or subtract :class:`TimeDelta` objects together:

    >>> TimeDelta(100.5) + TimeDelta(0.5)
    nitypes.bintime.TimeDelta(Decimal('101'))
    >>> TimeDelta(100.5) - TimeDelta(0.5)
    nitypes.bintime.TimeDelta(Decimal('100'))

    Or add/subtract a :class:`DateTime` with a :class:`TimeDelta`, :any:`datetime.timedelta`, or
    :any:`hightime.timedelta`:

    >>> DateTime(2025, 1, 1, tzinfo=datetime.timezone.utc) + TimeDelta(86400)
    nitypes.bintime.DateTime(2025, 1, 2, 0, 0, tzinfo=datetime.timezone.utc)
    >>> DateTime(2025, 1, 1, tzinfo=datetime.timezone.utc) + datetime.timedelta(days=1)
    nitypes.bintime.DateTime(2025, 1, 2, 0, 0, tzinfo=datetime.timezone.utc)
    >>> DateTime(2025, 1, 1, tzinfo=datetime.timezone.utc) + hightime.timedelta(femtoseconds=1)
    nitypes.bintime.DateTime(2025, 1, 1, 0, 0, 0, 0, 1, 13873, tzinfo=datetime.timezone.utc)

    Class members
    ^^^^^^^^^^^^^
    """  # noqa: W505 - doc line too long

    min: ClassVar[TimeDelta]
    """The most negative :class:`TimeDelta` object, approximately -292 million years."""

    max: ClassVar[TimeDelta]
    """The most positive :class:`TimeDelta` object, approximately 292 million years."""

    __slots__ = ["_ticks"]

    _ticks: int

    @overload
    def __init__(self) -> None: ...  # noqa: D107 - missing docstring in __init__

    @overload
    def __init__(  # noqa: D107 - missing docstring in __init__
        self, value: _OtherTimeDelta, /
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - missing docstring in __init__
        self, seconds: SupportsIndex | Decimal | float
    ) -> None: ...

    def __init__(
        self,
        seconds: SupportsIndex | Decimal | float | _OtherTimeDelta | None = None,
    ) -> None:
        """Initialize a TimeDelta."""
        ticks = self.__class__._to_ticks(seconds)
        if not (_INT128_MIN <= ticks <= _INT128_MAX):
            raise OverflowError(
                "The seconds value is out of range.\n\n"
                f"Requested value: {seconds}\n"
                f"Minimum value: {self.__class__.min.precision_total_seconds()}\n"
                f"Maximum value: {self.__class__.max.precision_total_seconds()}"
            )
        self._ticks = ticks

    @singledispatchmethod
    @classmethod
    def _to_ticks(cls, seconds: object) -> int:
        raise invalid_arg_type("seconds", "number or timedelta", seconds)

    # Python 3.9: pass the type to register() in order to work around
    # https://github.com/python/cpython/issues/86153 - singledispatchmethod raises an error when
    # relying on a forward declaration
    @_to_ticks.register(SupportsIndex)
    @classmethod
    def _(cls, seconds: SupportsIndex) -> int:
        return operator.index(seconds) << _BITS_PER_SECOND

    @_to_ticks.register(Decimal)
    @classmethod
    def _(cls, seconds: Decimal) -> int:
        with decimal.localcontext() as ctx:
            ctx.prec = _DECIMAL_DIGITS
            whole_seconds, fractional_seconds = divmod(seconds, 1)
            ticks = int(whole_seconds) * _TICKS_PER_SECOND
            ticks += round(fractional_seconds * _TICKS_PER_SECOND)
            return ticks

    @_to_ticks.register(float)
    @classmethod
    def _(cls, seconds: float) -> int:
        fractional_seconds, whole_seconds = math.modf(seconds)
        ticks = int(whole_seconds) * _TICKS_PER_SECOND
        ticks += round(fractional_seconds * _TICKS_PER_SECOND)
        return ticks

    @_to_ticks.register(ht.timedelta)
    @classmethod
    def _(cls, seconds: ht.timedelta) -> int:
        return cls._to_ticks(seconds.precision_total_seconds())

    @_to_ticks.register(dt.timedelta)
    @classmethod
    def _(cls, seconds: dt.timedelta) -> int:
        # Do not use total_seconds() because it loses precision.
        ticks = (seconds.days * _SECONDS_PER_DAY) << _BITS_PER_SECOND
        ticks += seconds.seconds << _BITS_PER_SECOND
        ticks += (seconds.microseconds << _BITS_PER_SECOND) // _MICROSECONDS_PER_SECOND
        return ticks

    @_to_ticks.register(type(None))
    @classmethod
    def _(cls, seconds: None) -> int:
        return 0

    @classmethod
    def from_ticks(cls, ticks: SupportsIndex) -> Self:
        """Create a TimeDelta from a 128-bit fixed point number expressed as an integer."""
        ticks = arg_to_int("ticks", ticks)
        if not (_INT128_MIN <= ticks <= _INT128_MAX):
            raise int_out_of_range(ticks, _INT128_MIN, _INT128_MAX)
        self = cls.__new__(cls)
        self._ticks = ticks
        return self

    @classmethod
    def from_tuple(cls, value: TimeValueTuple) -> Self:
        """Create a TimeDelta from 64-bit whole seconds and fractional seconds ints."""
        if not (_INT64_MIN <= value.whole_seconds <= _INT64_MAX):
            raise int_out_of_range(value.whole_seconds, _INT64_MIN, _INT64_MAX)
        if not (_UINT64_MIN <= value.fractional_seconds <= _UINT64_MAX):
            raise int_out_of_range(value.fractional_seconds, _UINT64_MIN, _UINT64_MAX)
        ticks = value.whole_seconds << _BITS_PER_SECOND
        ticks = ticks | value.fractional_seconds
        return cls.from_ticks(ticks)

    def _to_datetime_timedelta(self) -> dt.timedelta:
        """Return self as a :any:`datetime.timedelta`."""
        whole_seconds = self._ticks >> _BITS_PER_SECOND
        microseconds = (
            _MICROSECONDS_PER_SECOND * (self._ticks & _FRACTIONAL_SECONDS_MASK)
        ) >> _BITS_PER_SECOND
        return dt.timedelta(seconds=whole_seconds, microseconds=microseconds)

    def _to_hightime_timedelta(self) -> ht.timedelta:
        """Return self as a :any:`hightime.timedelta`."""
        whole_seconds = self._ticks >> _BITS_PER_SECOND
        yoctoseconds = (
            _YOCTOSECONDS_PER_SECOND * (self._ticks & _FRACTIONAL_SECONDS_MASK)
        ) >> _BITS_PER_SECOND
        return ht.timedelta(seconds=whole_seconds, yoctoseconds=yoctoseconds)

    @property
    def days(self) -> int:
        """The number of days in the time delta."""
        return (self._ticks >> _BITS_PER_SECOND) // _SECONDS_PER_DAY

    @property
    def seconds(self) -> int:
        """The number of seconds in the time delta, up to the nearest day."""
        return (self._ticks >> _BITS_PER_SECOND) % _SECONDS_PER_DAY

    @property
    def microseconds(self) -> int:
        """The number of microseconds in the time delta, up to the nearest second."""
        return (
            _MICROSECONDS_PER_SECOND * (self._ticks & _FRACTIONAL_SECONDS_MASK)
        ) >> _BITS_PER_SECOND

    @property
    def femtoseconds(self) -> int:
        """The number of femtoseconds in the time delta, up to the nearest microsecond."""
        value = (
            _FEMTOSECONDS_PER_SECOND * (self._ticks & _FRACTIONAL_SECONDS_MASK) >> _BITS_PER_SECOND
        )
        return value % _FEMTOSECONDS_PER_MICROSECOND

    @property
    def yoctoseconds(self) -> int:
        """The number of yoctoseconds in the time delta, up to the nearest femtosecond.

        .. warning::
            Because this class uses a 64-bit binary fraction, the smallest value it can represent
            is ``1.0 / (1 << 64)`` seconds, which is about 54210 yoctoseconds.
        """
        value = (
            _YOCTOSECONDS_PER_SECOND * (self._ticks & _FRACTIONAL_SECONDS_MASK)
        ) >> _BITS_PER_SECOND
        return value % _YOCTOSECONDS_PER_FEMTOSECOND

    @property
    def ticks(self) -> int:
        """The total ticks in the time delta as a 128-bit integer."""
        return self._ticks

    def to_tuple(self) -> TimeValueTuple:
        """The whole seconds and fractional seconds parts of the time delta as 64-bit ints."""
        whole_seconds = self._ticks >> _BITS_PER_SECOND
        fractional_seconds = self._ticks & _FRACTIONAL_SECONDS_MASK
        return TimeValueTuple(whole_seconds, fractional_seconds)

    def total_seconds(self) -> float:
        """The total seconds in the time delta.

        .. warning::
            Converting a time value to a floating point number loses precision. Consider using
            :any:`precision_total_seconds` instead.
        """
        seconds = float(self._ticks >> _BITS_PER_SECOND)
        seconds += float((self._ticks & _FRACTIONAL_SECONDS_MASK) / _TICKS_PER_SECOND)
        return seconds

    def precision_total_seconds(self) -> Decimal:
        """The precise total seconds in the time delta.

        Note: up to 64 significant digits are used in computation.
        """
        with decimal.localcontext() as ctx:
            ctx.prec = _DECIMAL_DIGITS
            seconds = Decimal(self._ticks >> _BITS_PER_SECOND)
            seconds += Decimal(self._ticks & _FRACTIONAL_SECONDS_MASK) / Decimal(_TICKS_PER_SECOND)
            return seconds

    def __neg__(self) -> TimeDelta:
        """Return -self."""
        return self.__class__.from_ticks(-self._ticks)

    def __pos__(self) -> TimeDelta:
        """Return +self."""
        return self

    def __abs__(self) -> TimeDelta:
        """Return abs(self)."""
        return -self if self._ticks < 0 else self

    @overload
    def __add__(  # noqa: D105 - missing docstring in magic method
        self, value: TimeDelta | _OtherTimeDelta, /
    ) -> TimeDelta: ...

    @overload
    def __add__(  # noqa: D105 - missing docstring in magic method
        self, value: ht.datetime, /
    ) -> ht.datetime: ...

    @overload
    def __add__(  # noqa: D105 - missing docstring in magic method
        self, value: dt.datetime, /
    ) -> dt.datetime: ...

    def __add__(
        self, value: TimeDelta | _OtherTimeDelta | _OtherDateTime, /
    ) -> TimeDelta | _OtherDateTime:
        """Return self+value."""
        if isinstance(value, TimeDelta):
            return self.__class__.from_ticks(self._ticks + value._ticks)
        elif isinstance(value, _OTHER_TIMEDELTA_TUPLE):
            return self + self.__class__(value)
        elif isinstance(value, ht.datetime):
            return self._to_hightime_timedelta() + value
        # Handle dt.datetime separately to round to the nearest microsecond instead of truncating.
        elif isinstance(value, dt.datetime):
            return self._to_datetime_timedelta() + value
        else:
            return NotImplemented

    __radd__ = __add__

    # __sub__ doesn't support _TOtherDateTime because it doesn't make sense to negate a datetime.
    def __sub__(self, value: TimeDelta | _OtherTimeDelta, /) -> TimeDelta:
        """Return self-value."""
        if isinstance(value, TimeDelta):
            return self.__class__.from_ticks(self._ticks - value._ticks)
        elif isinstance(value, _OTHER_TIMEDELTA_TUPLE):
            return self - self.__class__(value)
        else:
            return NotImplemented

    # __rsub__ supports _TOtherDateTime in order to support subtracting a bintime.TimeDelta from a
    # datetime.datetime or hightime.datetime.
    @overload
    def __rsub__(  # noqa: D105 - missing docstring in magic method
        self, value: TimeDelta | _OtherTimeDelta, /
    ) -> TimeDelta: ...

    @overload
    def __rsub__(  # noqa: D105 - missing docstring in magic method
        self, value: ht.datetime, /
    ) -> ht.datetime: ...

    @overload
    def __rsub__(  # noqa: D105 - missing docstring in magic method
        self, value: dt.datetime, /
    ) -> dt.datetime: ...

    def __rsub__(
        self, value: TimeDelta | _OtherTimeDelta | _OtherDateTime, /
    ) -> TimeDelta | _OtherDateTime:
        """Return value-self."""
        if isinstance(value, TimeDelta):
            return self.__class__.from_ticks(value._ticks - self._ticks)
        elif isinstance(value, _OTHER_TIMEDELTA_TUPLE):
            return self.__class__(value) - self
        elif isinstance(value, ht.datetime):
            return value - self._to_hightime_timedelta()
        # Handle dt.datetime separately to round to the nearest microsecond instead of truncating.
        elif isinstance(value, dt.datetime):
            return value - self._to_datetime_timedelta()
        else:
            return NotImplemented

    def __mul__(self, value: int | float | Decimal, /) -> TimeDelta:
        """Return self*value."""
        if isinstance(value, int):
            return self.__class__.from_ticks(self._ticks * value)
        elif isinstance(value, float):
            # Using floating point math on 128-bit numbers loses precision, so use Decimal math.
            return self * Decimal(value)
        elif isinstance(value, Decimal):
            with decimal.localcontext() as ctx:
                ctx.prec = _DECIMAL_DIGITS
                return self.__class__(self.precision_total_seconds() * value)
        else:
            return NotImplemented

    __rmul__ = __mul__

    @overload
    def __floordiv__(  # noqa: D105 - missing docstring in magic method
        self, value: TimeDelta, /
    ) -> int: ...
    @overload
    def __floordiv__(  # noqa: D105 - missing docstring in magic method
        self, value: int, /
    ) -> TimeDelta: ...

    def __floordiv__(self, value: TimeDelta | int, /) -> int | TimeDelta:
        """Return self//value."""
        if isinstance(value, TimeDelta):
            return self._ticks // value._ticks
        elif isinstance(value, int):
            return self.__class__.from_ticks(self._ticks // value)
        else:
            return NotImplemented

    @overload
    def __truediv__(  # noqa: D105 - missing docstring in magic method
        self, value: TimeDelta, /
    ) -> float: ...
    @overload
    def __truediv__(  # noqa: D105 - missing docstring in magic method
        self, value: float, /
    ) -> TimeDelta: ...

    def __truediv__(self, value: TimeDelta | float, /) -> float | TimeDelta:
        """Return self/value."""
        if isinstance(value, TimeDelta):
            return self.total_seconds() / value.total_seconds()
        elif isinstance(value, float):
            return self.__class__(self.total_seconds() / value)
        else:
            return NotImplemented

    def __mod__(self, value: TimeDelta | _OtherTimeDelta, /) -> TimeDelta:
        """Return self%value."""
        if isinstance(value, TimeDelta):
            return self.__class__.from_ticks(self._ticks % value._ticks)
        elif isinstance(value, _OTHER_TIMEDELTA_TUPLE):
            return self % self.__class__(value)
        else:
            return NotImplemented

    def __divmod__(self, value: TimeDelta | _OtherTimeDelta, /) -> tuple[int, TimeDelta]:
        """Return (self//value, self%value)."""
        if isinstance(value, TimeDelta):
            return (self // value, self % value)
        elif isinstance(value, _OTHER_TIMEDELTA_TUPLE):
            return divmod(self, self.__class__(value))
        else:
            return NotImplemented

    # In comparison operators, always promote to the more precise data type (dt -> bt, bt -> ht).
    def __lt__(self, value: TimeDelta | _OtherTimeDelta, /) -> bool:
        """Return self<value."""
        if isinstance(value, self.__class__):
            return self._ticks < value._ticks
        elif isinstance(value, ht.timedelta):
            return self._to_hightime_timedelta() < value
        elif isinstance(value, dt.timedelta):
            return self < self.__class__(value)
        else:
            return NotImplemented

    def __le__(self, value: TimeDelta | _OtherTimeDelta, /) -> bool:
        """Return self<=value."""
        if isinstance(value, self.__class__):
            return self._ticks <= value._ticks
        elif isinstance(value, ht.timedelta):
            return self._to_hightime_timedelta() <= value
        elif isinstance(value, dt.timedelta):
            return self <= self.__class__(value)
        else:
            return NotImplemented

    def __eq__(self, value: object, /) -> bool:
        """Return self==value."""
        if isinstance(value, self.__class__):
            return self._ticks == value._ticks
        elif isinstance(value, ht.timedelta):
            return self._to_hightime_timedelta() == value
        elif isinstance(value, dt.timedelta):
            return self == self.__class__(value)
        else:
            return NotImplemented

    def __gt__(self, value: TimeDelta | _OtherTimeDelta, /) -> bool:
        """Return self<value."""
        if isinstance(value, self.__class__):
            return self._ticks > value._ticks
        elif isinstance(value, ht.timedelta):
            return self._to_hightime_timedelta() > value
        elif isinstance(value, dt.timedelta):
            return self > self.__class__(value)
        else:
            return NotImplemented

    def __ge__(self, value: TimeDelta | _OtherTimeDelta, /) -> bool:
        """Return self>=value."""
        if isinstance(value, self.__class__):
            return self._ticks >= value._ticks
        elif isinstance(value, ht.timedelta):
            return self._to_hightime_timedelta() >= value
        elif isinstance(value, dt.timedelta):
            return self >= self.__class__(value)
        else:
            return NotImplemented

    def __bool__(self) -> bool:
        """Return bool(self)."""
        return self._ticks != 0

    def __hash__(self) -> int:
        """Return hash(self)."""
        return hash(self._ticks)

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        return (self.__class__.from_ticks, (self._ticks,))

    def __str__(self) -> str:
        """Return repr(self)."""
        days = self.days
        seconds = self.seconds
        minutes, seconds = divmod(seconds, 60)
        hours, minutes = divmod(minutes, 60)
        # Display up to 18 digits of fractional seconds, rounded to the nearest digit.
        fractional_seconds = 10**18 * (self._ticks & _FRACTIONAL_SECONDS_MASK)
        fractional_seconds = (fractional_seconds + _TICKS_PER_SECOND // 2) // _TICKS_PER_SECOND
        s = f"{days} day, " if abs(days) == 1 else f"{days} days, " if days else ""
        s += f"{hours}:{minutes:02}:{seconds:02}"
        if fractional_seconds != 0:
            s += f".{fractional_seconds:018}".rstrip("0")  # strip trailing zeroes
        return s

    def __repr__(self) -> str:
        """Return repr(self)."""
        if _REPR_TICKS:
            return (
                f"{self.__class__.__module__}.{self.__class__.__name__}"
                f".from_ticks({self._ticks})"
            )
        # Display up to 24 decimal digits (yoctoseconds), like hightime does. The smallest time
        # increment representable with NI-BTF is 54210 yoctoseconds, so if all 24 decimal digits are
        # displayed, the last few are due to rounding error.
        return (
            f"{self.__class__.__module__}.{self.__class__.__name__}"
            f"(Decimal('{self.precision_total_seconds():.24}'))"
        )


TimeDelta.max = TimeDelta.from_ticks(_INT128_MAX)
TimeDelta.min = TimeDelta.from_ticks(_INT128_MIN)
