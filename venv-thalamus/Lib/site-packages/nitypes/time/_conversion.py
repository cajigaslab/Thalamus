from __future__ import annotations

import datetime as dt
from collections.abc import Callable
from functools import singledispatch
from typing import Any, cast

import hightime as ht

import nitypes.bintime as bt
from nitypes._exceptions import invalid_arg_type, invalid_requested_type
from nitypes.time.typing import AnyDateTime, AnyTimeDelta, TDateTime, TTimeDelta


def convert_datetime(requested_type: type[TDateTime], value: AnyDateTime, /) -> TDateTime:
    """Convert a datetime object to the specified type."""
    convert_func = _CONVERT_DATETIME_FOR_TYPE.get(requested_type)
    if convert_func is None:
        raise invalid_requested_type("datetime", requested_type)
    return cast(TDateTime, convert_func(value))


@singledispatch
def _convert_to_bt_absolute_time(value: object, /) -> bt.DateTime:
    raise invalid_arg_type("value", "datetime", value)


@_convert_to_bt_absolute_time.register
def _(value: bt.DateTime, /) -> bt.DateTime:
    return value


@_convert_to_bt_absolute_time.register
def _(value: dt.datetime, /) -> bt.DateTime:
    return bt.DateTime(value)


@_convert_to_bt_absolute_time.register
def _(value: ht.datetime, /) -> bt.DateTime:
    return bt.DateTime(value)


@singledispatch
def _convert_to_dt_datetime(value: object, /) -> dt.datetime:
    raise invalid_arg_type("value", "datetime", value)


@_convert_to_dt_datetime.register
def _(value: bt.DateTime, /) -> dt.datetime:
    return value._to_datetime_datetime()


@_convert_to_dt_datetime.register
def _(value: dt.datetime, /) -> dt.datetime:
    return value


@_convert_to_dt_datetime.register
def _(value: ht.datetime, /) -> dt.datetime:
    return dt.datetime(
        value.year,
        value.month,
        value.day,
        value.hour,
        value.minute,
        value.second,
        value.microsecond,
        value.tzinfo,
        fold=value.fold,
    )


@singledispatch
def _convert_to_ht_datetime(value: object, /) -> ht.datetime:
    raise invalid_arg_type("value", "datetime", value)


@_convert_to_ht_datetime.register
def _(value: bt.DateTime, /) -> ht.datetime:
    return value._to_hightime_datetime()


@_convert_to_ht_datetime.register
def _(value: dt.datetime, /) -> ht.datetime:
    return ht.datetime(
        value.year,
        value.month,
        value.day,
        value.hour,
        value.minute,
        value.second,
        value.microsecond,
        value.tzinfo,
        fold=value.fold,
    )


@_convert_to_ht_datetime.register
def _(value: ht.datetime, /) -> ht.datetime:
    return value


_CONVERT_DATETIME_FOR_TYPE: dict[type[Any], Callable[[object], object]] = {
    bt.DateTime: _convert_to_bt_absolute_time,
    dt.datetime: _convert_to_dt_datetime,
    ht.datetime: _convert_to_ht_datetime,
}


def convert_timedelta(requested_type: type[TTimeDelta], value: AnyTimeDelta, /) -> TTimeDelta:
    """Convert a timedelta object to the specified type."""
    convert_func = _CONVERT_TIMEDELTA_FOR_TYPE.get(requested_type)
    if convert_func is None:
        raise invalid_requested_type("timedelta", requested_type)
    return cast(TTimeDelta, convert_func(value))


@singledispatch
def _convert_to_bt_timedelta(value: object, /) -> bt.TimeDelta:
    raise invalid_arg_type("value", "timedelta", value)


@_convert_to_bt_timedelta.register
def _(value: bt.TimeDelta, /) -> bt.TimeDelta:
    return value


@_convert_to_bt_timedelta.register
def _(value: dt.timedelta, /) -> bt.TimeDelta:
    return bt.TimeDelta(value)


@_convert_to_bt_timedelta.register
def _(value: ht.timedelta, /) -> bt.TimeDelta:
    return bt.TimeDelta(value)


@singledispatch
def _convert_to_dt_timedelta(value: object, /) -> dt.timedelta:
    raise invalid_arg_type("value", "timedelta", value)


@_convert_to_dt_timedelta.register
def _(value: bt.TimeDelta, /) -> dt.timedelta:
    return value._to_datetime_timedelta()


@_convert_to_dt_timedelta.register
def _(value: dt.timedelta, /) -> dt.timedelta:
    return value


@_convert_to_dt_timedelta.register
def _(value: ht.timedelta, /) -> dt.timedelta:
    return dt.timedelta(value.days, value.seconds, value.microseconds)


@singledispatch
def _convert_to_ht_timedelta(value: object, /) -> ht.timedelta:
    raise invalid_arg_type("value", "timedelta", value)


@_convert_to_ht_timedelta.register
def _(value: bt.TimeDelta, /) -> ht.timedelta:
    return value._to_hightime_timedelta()


@_convert_to_ht_timedelta.register
def _(value: dt.timedelta, /) -> ht.timedelta:
    return ht.timedelta(
        value.days,
        value.seconds,
        value.microseconds,
    )


@_convert_to_ht_timedelta.register
def _(value: ht.timedelta, /) -> ht.timedelta:
    return value


_CONVERT_TIMEDELTA_FOR_TYPE: dict[type[Any], Callable[[object], object]] = {
    bt.TimeDelta: _convert_to_bt_timedelta,
    dt.timedelta: _convert_to_dt_timedelta,
    ht.timedelta: _convert_to_ht_timedelta,
}
