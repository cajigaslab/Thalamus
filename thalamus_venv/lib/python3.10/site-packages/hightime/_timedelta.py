import datetime as std_datetime
import decimal
from decimal import Decimal
from fractions import Fraction

_YS_PER_S = 10**24
_YS_PER_US = 10**18
_YS_PER_FS = 10**9
_YS_PER_DAY = 60 * 60 * 24 * _YS_PER_S

_US_PER_DAY = 24 * 60 * 60 * 1000 * 1000
_US_PER_WEEK = 7 * _US_PER_DAY
_NS_PER_HOUR = 60 * 60 * (10**9)
_PS_PER_MINUTE = 60 * (10**12)

_FIELD_NAMES = [
    "days",
    "seconds",
    "microseconds",
    "femtoseconds",
    "yoctoseconds",
]


# Ripped from standard library's datetime.py
def _divide_and_round(a, b):
    q, r = divmod(a, b)
    r *= 2
    greater_than_half = r > b if b > 0 else r < b
    if greater_than_half or r == b and q % 2 == 1:
        q += 1

    return q


def _cmp(x, y):
    return 0 if x == y else 1 if x > y else -1


class timedelta(std_datetime.timedelta):
    __slots__ = ("_femtoseconds", "_yoctoseconds")

    def __new__(
        cls,
        days=0,
        seconds=0,
        microseconds=0,
        milliseconds=0,
        minutes=0,
        hours=0,
        weeks=0,
        # These are at the end to try and keep the signature compatible
        nanoseconds=0,
        picoseconds=0,
        femtoseconds=0,
        attoseconds=0,
        zeptoseconds=0,
        yoctoseconds=0,
    ):
        # Ideally we'd just take care of the sub-microsecond bits, but since the user
        # could specify larger units as a float with a sub-microsecond value,
        # datetime.datetime would round it. Therefore we're responsible for everything.

        # To handle imprecision, we (somewhat) arbitrarily limit the granularity of the
        # higher units.
        #   Weeks -> Up to 1 microsecond
        #   Days -> Up to 1 microsecond
        #   Hours -> Up to 1 nanosecond
        #   Minutes -> Up to 1 picosecond
        #   Seconds -> Up to 1 femtosecond
        #   Milliseconds -> Up to 1 attosecond
        #   Microsecond -> Up to 1 zeptosecond
        #   Nanosecond -> Unspecified beyond yoctosecond
        weeks = Fraction(weeks).limit_denominator(_US_PER_WEEK)
        days = Fraction(days).limit_denominator(_US_PER_DAY)
        hours = Fraction(hours).limit_denominator(_NS_PER_HOUR)
        minutes = Fraction(minutes).limit_denominator(_PS_PER_MINUTE)
        seconds = round(Fraction(seconds), 15)

        # Let's get ready for some really big numbers...
        yoctoseconds = Fraction(yoctoseconds)
        for index, unit_value in enumerate(
            [
                zeptoseconds,
                attoseconds,
                femtoseconds,
                picoseconds,
                nanoseconds,
                microseconds,
                milliseconds,
            ]
        ):
            truncated = round(Fraction(unit_value), 15)
            yoctoseconds += Fraction(truncated * (1000 ** (index + 1)))
        yoctoseconds += Fraction(seconds * _YS_PER_S)
        yoctoseconds += Fraction(minutes * 60 * _YS_PER_S)
        yoctoseconds += Fraction(hours * 60 * 60 * _YS_PER_S)
        yoctoseconds += Fraction(days * _YS_PER_DAY)
        yoctoseconds += Fraction(weeks * 7 * _YS_PER_DAY)

        days, yoctoseconds = divmod(yoctoseconds, _YS_PER_DAY)
        seconds, yoctoseconds = divmod(yoctoseconds, _YS_PER_S)
        microseconds, yoctoseconds = divmod(yoctoseconds, _YS_PER_US)
        femtoseconds, yoctoseconds = divmod(yoctoseconds, _YS_PER_FS)

        self = super().__new__(
            cls,
            days=days,
            seconds=seconds,
            microseconds=microseconds,
        )

        self._femtoseconds = femtoseconds
        self._yoctoseconds = round(yoctoseconds)
        return self

    # Public properties

    days = std_datetime.timedelta.days
    seconds = std_datetime.timedelta.seconds
    microseconds = std_datetime.timedelta.microseconds

    @property
    def femtoseconds(self):
        """femtoseconds"""
        return self._femtoseconds

    @property
    def yoctoseconds(self):
        """yoctoseconds"""
        return self._yoctoseconds

    # Public methods

    def total_seconds(self):
        """Total seconds in the duration."""
        return (
            (self.days * 86400)
            + self.seconds
            + (self.microseconds / 10**6)
            + (self.femtoseconds / 10**15)
            + (self.yoctoseconds / 10**24)
        )

    def precision_total_seconds(self):
        """Precise total seconds in the duration.

        Note: up to 64 significant digits are used in computation.
        """
        with decimal.localcontext() as ctx:
            ctx.prec = 64
            return Decimal(
                (self.days * 86400)
                + self.seconds
                + Decimal(self.microseconds) / Decimal(10**6)
                + Decimal(self.femtoseconds) / Decimal(10**15)
                + Decimal(self.yoctoseconds) / Decimal(10**24)
            )

    # String operators

    def __repr__(self):
        # Follow newer repr: https://github.com/python/cpython/pull/3687
        r = "{}.{}".format(self.__class__.__module__, self.__class__.__qualname__)
        r += "("
        r += ", ".join(
            "{}={}".format(name, getattr(self, name))
            for name in _FIELD_NAMES
            if getattr(self, name) != 0
        )
        r += ")"
        return r

    def __str__(self):
        s = super().__str__()
        if self.femtoseconds or self.yoctoseconds:
            if not self.microseconds:
                s += "." + "0" * 6

            s += "{:09d}".format(self.femtoseconds)

            if self.yoctoseconds:
                s += "{:09d}".format(self.yoctoseconds)

        return s

    # Comparison operators

    def __eq__(self, other):
        if isinstance(other, std_datetime.timedelta):
            return _cmp(timedelta._as_tuple(self), timedelta._as_tuple(other)) == 0
        else:
            return False

    def __ne__(self, other):
        return not (self == other)

    def __lt__(self, other):
        return self._cmp(other) < 0

    def __le__(self, other):
        return self._cmp(other) <= 0

    def __gt__(self, other):
        return self._cmp(other) > 0

    def __ge__(self, other):
        return self._cmp(other) >= 0

    def __bool__(self):
        return any(getattr(self, field) for field in _FIELD_NAMES)

    # Arithmetic operators

    def __pos__(self):
        return self

    def __abs__(self):
        return -self if self.days < 0 else self

    def __add__(self, other):
        if isinstance(other, std_datetime.timedelta):
            return timedelta(
                **{
                    field: getattr(self, field) + getattr(other, field, 0)
                    for field in _FIELD_NAMES
                }
            )
        return NotImplemented

    __radd__ = __add__

    def __sub__(self, other):
        if isinstance(other, std_datetime.timedelta):
            return timedelta(
                **{
                    field: getattr(self, field) - getattr(other, field, 0)
                    for field in _FIELD_NAMES
                }
            )
        return NotImplemented

    def __neg__(self):
        return timedelta(**{field: -(getattr(self, field)) for field in _FIELD_NAMES})

    def __mul__(self, other):
        if isinstance(other, (int, float)):
            return timedelta(
                **{field: getattr(self, field) * other for field in _FIELD_NAMES}
            )
        return NotImplemented

    __rmul__ = __mul__

    def __floordiv__(self, other):
        if not isinstance(other, (int, std_datetime.timedelta)):
            return NotImplemented

        ys = timedelta._as_ys(self)
        if isinstance(other, std_datetime.timedelta):
            return ys // timedelta._as_ys(other)
        return timedelta(yoctoseconds=ys // other)

    def __truediv__(self, other):
        if not isinstance(other, (int, float, std_datetime.timedelta)):
            return NotImplemented

        if isinstance(other, std_datetime.timedelta):
            return float(Fraction(timedelta._as_ys(self), timedelta._as_ys(other)))
        return timedelta(
            **{field: getattr(self, field) / other for field in _FIELD_NAMES}
        )

    def __mod__(self, other):
        if isinstance(other, std_datetime.timedelta):
            return timedelta(
                yoctoseconds=timedelta._as_ys(self) % timedelta._as_ys(other)
            )
        return NotImplemented

    def __divmod__(self, other):
        if isinstance(other, std_datetime.timedelta):
            q, r = divmod(timedelta._as_ys(self), timedelta._as_ys(other))
            return q, timedelta(yoctoseconds=r)
        return NotImplemented

    # Hash support

    def __hash__(self):
        return hash(timedelta._as_tuple(self))

    # Helper methods

    @classmethod
    def _as_ys(cls, td):
        days = td.days
        seconds = (days * 24 * 3600) + td.seconds
        microseconds = (seconds * 1000000) + td.microseconds
        femtoseconds = (microseconds * 1000000000) + getattr(td, "femtoseconds", 0)
        return (femtoseconds * 1000000000) + getattr(td, "yoctoseconds", 0)

    @classmethod
    def _as_tuple(cls, td):
        return tuple(getattr(td, field, 0) for field in _FIELD_NAMES)

    def _cmp(self, other):
        if isinstance(other, std_datetime.timedelta):
            return _cmp(timedelta._as_tuple(self), timedelta._as_tuple(other))
        else:
            raise TypeError(
                "can't compare '{}' to '{}'".format(
                    type(self).__name__, type(other).__name__
                )
            )
