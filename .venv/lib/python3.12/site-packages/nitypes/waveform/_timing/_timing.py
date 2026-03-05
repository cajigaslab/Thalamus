from __future__ import annotations

import datetime as dt
import operator
from collections.abc import Iterable, Sequence
from typing import TYPE_CHECKING, Any, ClassVar, Generic, SupportsIndex, cast, final

import hightime as ht
from typing_extensions import Self

import nitypes.bintime as bt
from nitypes._exceptions import add_note
from nitypes.time import convert_datetime, convert_timedelta
from nitypes.waveform._timing._sample_interval import (
    SampleIntervalStrategy,
    create_sample_interval_strategy,
)
from nitypes.waveform.typing import (
    TOtherSampleInterval,
    TOtherTimeOffset,
    TOtherTimestamp,
    TSampleInterval,
    TSampleInterval_co,
    TTimeOffset,
    TTimeOffset_co,
    TTimestamp,
    TTimestamp_co,
)

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import SampleIntervalMode

else:
    from nitypes.waveform._timing._sample_interval import SampleIntervalMode


@final
class Timing(Generic[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]):
    """Waveform timing information.

    Waveform timing objects are immutable.
    """

    empty: ClassVar[Timing[dt.datetime, dt.timedelta, dt.timedelta]]
    """A waveform timing object with no timestamp, time offset, or sample interval."""

    # The typing for these named constructors is tricky:
    # - They can't use covariant type variables as parameter types. Instead, they use the
    #   non-covariant type variables, which are distinct. See
    #   https://github.com/python/mypy/issues/6178
    # - ``cls`` is implicitly ``type[Timing[_TTimestamp_co, _TTimeOffset_co, _TSampleInterval_co]]``
    #   and its type variables may already be solved. For example, we can infer the type of ``cls``
    #   from ``my_timing`` in ``my_timing.__class__.create_with_no_interval()``.
    # - Construct the object with ``Timing(...)`` not ``cls(...)`` to avoid conflicts between the
    #   the class's type variables and the argument type variables.
    # - For type variables that are not inferred from arguments, use the covariant type variables so
    #   that ``my_timing.__class__.create_with_no_interval()`` matches the type of ``my_timing``.

    @classmethod
    def create_with_no_interval(
        cls,
        timestamp: TTimestamp | None = None,
        time_offset: TTimeOffset | None = None,
    ) -> Timing[TTimestamp, TTimeOffset, TSampleInterval_co]:
        """Create a waveform timing object with no sample interval.

        Args:
            timestamp: A timestamp representing the start of an acquisition or a related
                occurrence.
            time_offset: The time difference between the timestamp and the time that the first
                sample was acquired.

        Returns:
            A waveform timing object.
        """
        return Timing(SampleIntervalMode.NONE, timestamp, time_offset)

    @classmethod
    def create_with_regular_interval(
        cls,
        sample_interval: TSampleInterval,
        timestamp: TTimestamp | None = None,
        time_offset: TTimeOffset | None = None,
    ) -> Timing[TTimestamp, TTimeOffset, TSampleInterval]:
        """Create a waveform timing object with a regular sample interval.

        Args:
            sample_interval: The time difference between samples.
            timestamp: A timestamp representing the start of an acquisition or a related
                occurrence.
            time_offset: The time difference between the timestamp and the time that the first
                sample was acquired.

        Returns:
            A waveform timing object.
        """
        return Timing(SampleIntervalMode.REGULAR, timestamp, time_offset, sample_interval)

    @classmethod
    def create_with_irregular_interval(
        cls,
        timestamps: Sequence[TTimestamp],
    ) -> Timing[TTimestamp, TTimeOffset_co, TSampleInterval_co]:
        """Create a waveform timing object with an irregular sample interval.

        Args:
            timestamps: A sequence containing a timestamp for each sample in the waveform,
                specifying the time that the sample was acquired.

        Returns:
            A waveform timing object.
        """
        return Timing(SampleIntervalMode.IRREGULAR, timestamps=timestamps)

    __slots__ = [
        "_sample_interval_strategy",
        "_sample_interval_mode",
        "_timestamp",
        "_time_offset",
        "_sample_interval",
        "_timestamps",
        "__weakref__",
    ]

    _sample_interval_strategy: SampleIntervalStrategy[
        TTimestamp_co, TTimeOffset_co, TSampleInterval_co
    ]
    _sample_interval_mode: SampleIntervalMode
    _timestamp: TTimestamp_co | None
    _time_offset: TTimeOffset_co | None
    _sample_interval: TSampleInterval_co | None
    _timestamps: list[TTimestamp_co] | None

    def __init__(
        self,
        sample_interval_mode: SampleIntervalMode,
        timestamp: TTimestamp_co | None = None,
        time_offset: TTimeOffset_co | None = None,
        sample_interval: TSampleInterval_co | None = None,
        timestamps: Sequence[TTimestamp_co] | None = None,
        *,
        copy_timestamps: bool = True,
    ) -> None:
        """Initialize a new waveform timing object.

        Args:
            sample_interval_mode: The sample interval mode of the waveform timing.
            timestamp: The timestamp of the waveform timing. This argument is optional for
                SampleIntervalMode.NONE and SampleIntervalMode.REGULAR and unsupported for
                SampleIntervalMode.IRREGULAR.
            time_offset: The time difference between the timestamp and the first sample. This
                argument is optional for SampleIntervalMode.NONE and SampleIntervalMode.REGULAR and
                unsupported for SampleIntervalMode.IRREGULAR.
            sample_interval: The time interval between samples. This argument is required for
                SampleIntervalMode.REGULAR and unsupported otherwise.
            timestamps: A sequence containing a timestamp for each sample in the waveform,
                specifying the time that the sample was acquired. This argument is required for
                SampleIntervalMode.IRREGULAR and unsupported otherwise.
            copy_timestamps: Specifies whether to copy the timestamps or take ownership.

        Most applications should use the named constructors instead:
        * :any:`create_with_no_interval`
        * :any:`create_with_regular_interval`
        * :any:`create_with_irregular_interval`
        """
        sample_interval_strategy = create_sample_interval_strategy(sample_interval_mode)
        try:
            sample_interval_strategy.validate_init_args(
                self, sample_interval_mode, timestamp, time_offset, sample_interval, timestamps
            )
        except (TypeError, ValueError) as e:
            add_note(e, f"Sample interval mode: {sample_interval_mode}")
            raise

        if timestamps is not None and (copy_timestamps or not isinstance(timestamps, list)):
            timestamps = list(timestamps)

        self._sample_interval_strategy = sample_interval_strategy
        self._sample_interval_mode = sample_interval_mode
        self._timestamp = timestamp
        self._time_offset = time_offset
        self._sample_interval = sample_interval
        self._timestamps = timestamps

    @property
    def has_timestamp(self) -> bool:
        """Indicates whether the waveform timing has a timestamp."""
        return self._timestamp is not None

    @property
    def timestamp(self) -> TTimestamp_co:
        """A timestamp representing the start of an acquisition or a related occurrence."""
        value = self._timestamp
        if value is None:
            raise RuntimeError("The waveform timing does not have a timestamp.")
        return value

    @property
    def has_start_time(self) -> bool:
        """Indicates whether the waveform timing has a start_time."""
        return self.has_timestamp

    @property
    def start_time(self) -> TTimestamp_co:
        """The time that the first sample in the waveform was acquired.

        This is equivalent to ``t0`` in a LabVIEW waveform.
        This value is derived from :attr:`timestamp` + :attr:`time_offset`.
        """
        value = self.timestamp
        if self.has_time_offset:
            # Work around https://github.com/python/mypy/issues/18203
            value += self.time_offset  # type: ignore[operator]
        return value  # type: ignore[reportReturnType,unused-ignore]

    @property
    def has_time_offset(self) -> bool:
        """Indicates whether the waveform timing has a time offset."""
        return self._time_offset is not None

    @property
    def time_offset(self) -> TTimeOffset_co:
        """The time difference between the timestamp and the first sample."""
        value = self._time_offset
        if value is None:
            raise RuntimeError("The waveform timing does not have a time offset.")
        return value

    @property
    def has_sample_interval(self) -> bool:
        """Indicates whether the waveform timing has a sample interval."""
        return self._sample_interval is not None

    @property
    def sample_interval(self) -> TSampleInterval_co:
        """The time interval between samples.

        This is equivalent to ``dt`` in a LabVIEW waveform.
        """
        value = self._sample_interval
        if value is None:
            raise RuntimeError("The waveform timing does not have a sample interval.")
        return value

    @property
    def sample_interval_mode(self) -> SampleIntervalMode:
        """The sample interval mode that specifies how the waveform is sampled."""
        return self._sample_interval_mode

    def get_timestamps(
        self, start_index: SupportsIndex, count: SupportsIndex
    ) -> Iterable[TTimestamp_co]:
        """Retrieve the timestamps of the waveform samples.

        Args:
            start_index: The sample index of the first timestamp to retrieve.
            count: The number of timestamps to retrieve.

        Returns:
            An iterable containing the requested timestamps.
        """
        start_index = operator.index(start_index)
        count = operator.index(count)

        if start_index < 0:
            raise ValueError("The sample index must be a non-negative integer.")
        if count < 0:
            raise ValueError("The count must be a non-negative integer.")

        return self._sample_interval_strategy.get_timestamps(self, start_index, count)

    def to_bintime(self) -> Timing[bt.DateTime, bt.TimeDelta, bt.TimeDelta]:
        """Convert the timing information to use :any:`nitypes.bintime`."""
        return self._convert(bt.DateTime, bt.TimeDelta, bt.TimeDelta)

    def to_datetime(self) -> Timing[dt.datetime, dt.timedelta, dt.timedelta]:
        """Convert the timing information to use :class:`DateTime`."""
        return self._convert(dt.datetime, dt.timedelta, dt.timedelta)

    def to_hightime(self) -> Timing[ht.datetime, ht.timedelta, ht.timedelta]:
        """Convert the timing information to use :any:`hightime`."""
        return self._convert(ht.datetime, ht.timedelta, ht.timedelta)

    def _convert(
        self,
        timestamp_type: type[TOtherTimestamp],
        time_offset_type: type[TOtherTimeOffset],
        sample_interval_type: type[TOtherSampleInterval],
    ) -> Timing[TOtherTimestamp, TOtherTimeOffset, TOtherSampleInterval]:
        # If the runtime type is correct, cast to the requested static type. Not a workaround.
        if (
            isinstance(self._timestamp, (timestamp_type, type(None)))
            and isinstance(self._time_offset, (time_offset_type, type(None)))
            and isinstance(self._sample_interval, (sample_interval_type, type(None)))
            and (
                self._timestamps is None
                or all(isinstance(ts, timestamp_type) for ts in self._timestamps)
            )
        ):
            return cast(Timing[TOtherTimestamp, TOtherTimeOffset, TOtherSampleInterval], self)

        return Timing(
            self._sample_interval_mode,
            None if self._timestamp is None else convert_datetime(timestamp_type, self._timestamp),
            (
                None
                if self._time_offset is None
                else convert_timedelta(time_offset_type, self._time_offset)
            ),
            (
                None
                if self._sample_interval is None
                else convert_timedelta(sample_interval_type, self._sample_interval)
            ),
            (
                None
                if self._timestamps is None
                else [convert_datetime(timestamp_type, ts) for ts in self._timestamps]
            ),
        )

    def __eq__(self, value: object, /) -> bool:
        """Return self==value."""
        if not isinstance(value, self.__class__):
            return NotImplemented
        return (
            self._timestamp == value._timestamp
            and self._time_offset == value._time_offset
            and self._sample_interval == value._sample_interval
            and self._sample_interval_mode == value._sample_interval_mode
            and self._timestamps == value._timestamps
        )

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        ctor_args = (
            self._sample_interval_mode,
            self._timestamp,
            self._time_offset,
            self._sample_interval,
            self._timestamps,
        )
        ctor_kwargs: dict[str, Any] = {}
        if self._timestamps is not None:
            ctor_kwargs["copy_timestamps"] = False
        return (self.__class__._unpickle, (ctor_args, ctor_kwargs))

    @classmethod
    def _unpickle(cls, args: tuple[Any, ...], kwargs: dict[str, Any]) -> Self:
        return cls(*args, **kwargs)

    def __repr__(self) -> str:
        """Return repr(self)."""
        # For Enum, __str__ is an unqualified ctor expression like E.V and __repr__ is <E.V: 0>.
        args = [f"{self.sample_interval_mode.__class__.__module__}.{self.sample_interval_mode}"]
        if self._timestamp is not None:
            args.append(f"timestamp={self._timestamp!r}")
        if self._time_offset is not None:
            args.append(f"time_offset={self._time_offset!r}")
        if self._sample_interval is not None:
            args.append(f"sample_interval={self._sample_interval!r}")
        if self._timestamps is not None:
            args.append(f"timestamps={self._timestamps!r}")
        return f"{self.__class__.__module__}.{self.__class__.__name__}({', '.join(args)})"

    def _append_timestamps(self, timestamps: Sequence[TTimestamp_co] | None) -> Self:
        new_timing = self._sample_interval_strategy.append_timestamps(self, timestamps)
        assert isinstance(new_timing, self.__class__)
        return new_timing

    def _append_timing(self, other: Self) -> Self:
        if not isinstance(other, self.__class__):
            raise TypeError(
                "The input waveform(s) must have the same waveform timing type as the current waveform."
            )

        new_timing = self._sample_interval_strategy.append_timing(self, other)
        assert isinstance(new_timing, self.__class__)
        return new_timing


Timing.empty = Timing(SampleIntervalMode.NONE)
