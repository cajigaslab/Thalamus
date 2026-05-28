from __future__ import annotations

import warnings
from collections.abc import Generator, Iterable, Sequence
from typing import TYPE_CHECKING, cast, final

from nitypes._arguments import validate_unsupported_arg
from nitypes._exceptions import add_note, invalid_arg_type
from nitypes.time._types import ANY_DATETIME_TUPLE, ANY_TIMEDELTA_TUPLE
from nitypes.waveform._exceptions import (
    create_no_timestamp_information_error,
    create_sample_interval_mode_mismatch_error,
)
from nitypes.waveform._timing._sample_interval._base import SampleIntervalStrategy
from nitypes.waveform._timing._sample_interval._mode import SampleIntervalMode
from nitypes.waveform._warnings import sample_interval_mismatch
from nitypes.waveform.typing import TSampleInterval_co, TTimeOffset_co, TTimestamp_co

if TYPE_CHECKING:
    from nitypes.waveform._timing._timing import Timing  # circular import


@final
class RegularSampleIntervalStrategy(
    SampleIntervalStrategy[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]
):
    """Implements SampleIntervalMode.REGULAR specific behavior."""

    def validate_init_args(  # noqa: D102 - Missing docstring in public method - override
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        sample_interval_mode: SampleIntervalMode,
        timestamp: TTimestamp_co | None,
        time_offset: TTimeOffset_co | None,
        sample_interval: TSampleInterval_co | None,
        timestamps: Sequence[TTimestamp_co] | None,
    ) -> None:
        if not isinstance(timestamp, (ANY_DATETIME_TUPLE, type(None))):
            raise invalid_arg_type("timestamp", "datetime or None", timestamp)
        if not isinstance(time_offset, (ANY_TIMEDELTA_TUPLE, type(None))):
            raise invalid_arg_type("time offset", "timedelta or None", time_offset)
        if not isinstance(sample_interval, ANY_TIMEDELTA_TUPLE):
            raise invalid_arg_type("sample interval", "timedelta", sample_interval)
        validate_unsupported_arg("timestamps", timestamps)

    def get_timestamps(  # noqa: D102 - Missing docstring in public method - override
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        start_index: int,
        count: int,
    ) -> Iterable[TTimestamp_co]:
        if timing.has_timestamp:
            return self._generate_regular_timestamps(timing, start_index, count)
        raise create_no_timestamp_information_error()

    def _generate_regular_timestamps(
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        start_index: int,
        count: int,
    ) -> Generator[TTimestamp_co]:
        sample_interval = timing.sample_interval
        # Work around https://github.com/python/mypy/issues/18203
        timestamp = timing.start_time + start_index * sample_interval  # type: ignore[operator]
        for i in range(count):
            if i != 0:
                timestamp += sample_interval  # type: ignore[operator]
            yield cast(TTimestamp_co, timestamp)

    def append_timestamps(  # noqa: D102 - Missing docstring in public method - override
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        timestamps: Sequence[TTimestamp_co] | None,
    ) -> Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]:
        try:
            validate_unsupported_arg("timestamps", timestamps)
        except (TypeError, ValueError) as e:
            add_note(e, f"Sample interval mode: {timing.sample_interval_mode}")
            raise
        return timing

    def append_timing(  # noqa: D102 - Missing docstring in public method - override
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        other: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
    ) -> Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]:
        if other._sample_interval_mode not in (SampleIntervalMode.NONE, SampleIntervalMode.REGULAR):
            raise create_sample_interval_mode_mismatch_error()
        if timing._sample_interval != other._sample_interval:
            warnings.warn(sample_interval_mismatch())
        return timing
