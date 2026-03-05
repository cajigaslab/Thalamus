from __future__ import annotations

import datetime as dt

import hightime as ht

import nitypes.bintime as bt

ANY_DATETIME_TUPLE = (bt.DateTime, dt.datetime, ht.datetime)
"""Tuple of types corresponding to :any:`AnyDateTime`."""

ANY_TIMEDELTA_TUPLE = (bt.TimeDelta, dt.timedelta, ht.timedelta)
"""Tuple of types corresponding to :any:`AnyTimeDelta`."""
