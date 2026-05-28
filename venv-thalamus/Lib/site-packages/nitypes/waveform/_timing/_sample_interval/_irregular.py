from __future__ import annotations

from collections.abc import Iterable, Sequence
from enum import Enum
from typing import TYPE_CHECKING, final

from nitypes._arguments import validate_unsupported_arg
from nitypes._exceptions import invalid_arg_type
from nitypes.time._types import ANY_DATETIME_TUPLE
from nitypes.waveform._exceptions import create_sample_interval_mode_mismatch_error
from nitypes.waveform._timing._sample_interval._base import SampleIntervalStrategy
from nitypes.waveform._timing._sample_interval._mode import SampleIntervalMode
from nitypes.waveform.errors import TimingMismatchError
from nitypes.waveform.typing import (
    TSampleInterval_co,
    TTimeOffset_co,
    TTimestamp,
    TTimestamp_co,
)

if TYPE_CHECKING:
    from nitypes.waveform._timing._timing import Timing  # circular import


class _Direction(Enum):
    INCREASING = -1
    UNKNOWN = 0
    DECREASING = 1


def _are_timestamps_monotonic(timestamps: Sequence[TTimestamp_co]) -> bool:
    direction = _Direction.UNKNOWN
    for i in range(1, len(timestamps)):
        comparison = _get_direction(timestamps[i - 1], timestamps[i])
        if comparison == _Direction.UNKNOWN:
            continue

        if direction == _Direction.UNKNOWN:
            direction = comparison
        elif comparison != direction:
            return False
    return True


def _get_direction(left: TTimestamp, right: TTimestamp) -> _Direction:
    # Work around https://github.com/python/mypy/issues/18203
    if left < right:  # type: ignore[operator]
        return _Direction.INCREASING
    if right < left:  # type: ignore[operator]
        return _Direction.DECREASING
    return _Direction.UNKNOWN


@final
class IrregularSampleIntervalStrategy(
    SampleIntervalStrategy[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]
):
    """Implements SampleIntervalMode.IRREGULAR specific behavior."""

    def validate_init_args(  # noqa: D102 - Missing docstring in public method - override
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        sample_interval_mode: SampleIntervalMode,
        timestamp: TTimestamp_co | None,
        time_offset: TTimeOffset_co | None,
        sample_interval: TSampleInterval_co | None,
        timestamps: Sequence[TTimestamp_co] | None,
    ) -> None:
        validate_unsupported_arg("timestamp", timestamp)
        validate_unsupported_arg("time offset", time_offset)
        validate_unsupported_arg("sample interval", sample_interval)
        if not isinstance(timestamps, Sequence) or not all(
            isinstance(ts, ANY_DATETIME_TUPLE) for ts in timestamps
        ):
            raise invalid_arg_type("timestamps", "sequence of datetime objects", timestamps)
        if not _are_timestamps_monotonic(timestamps):
            raise ValueError("The timestamps must be in ascending or descending order.")

    def get_timestamps(  # noqa: D102 - Missing docstring in public method - override
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        start_index: int,
        count: int,
    ) -> Iterable[TTimestamp_co]:
        assert timing._timestamps is not None
        if count > len(timing._timestamps):
            raise ValueError("The count must be less than or equal to the number of timestamps.")
        return timing._timestamps[start_index : start_index + count]

    def append_timestamps(  # noqa: D102 - Missing docstring in public method - override
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        timestamps: Sequence[TTimestamp_co] | None,
    ) -> Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]:
        assert timing._timestamps is not None

        if timestamps is None:
            raise TimingMismatchError(
                "The timestamps argument is required when appending to a waveform with irregular timing."
            )

        datetime_type = type(timing._timestamps[0]) if timing._timestamps else ANY_DATETIME_TUPLE
        if not all(isinstance(ts, datetime_type) for ts in timestamps):
            raise TypeError(
                "The timestamp data type must match the timing information of the current waveform."
            )

        if len(timestamps) == 0:
            return timing
        else:
            if not isinstance(timestamps, list):
                timestamps = list(timestamps)

            return timing.__class__.create_with_irregular_interval(timing._timestamps + timestamps)

    def append_timing(  # noqa: D102 - Missing docstring in public method - override
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        other: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
    ) -> Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]:
        if other._sample_interval_mode != SampleIntervalMode.IRREGULAR:
            raise create_sample_interval_mode_mismatch_error()

        assert timing._timestamps is not None and other._timestamps is not None

        if len(timing._timestamps) == 0:
            return other
        elif len(other._timestamps) == 0:
            return timing
        else:
            # The constructor will verify that the combined list of timestamps is monotonic. This is
            # not optimal for a large number of appends.
            return timing.__class__.create_with_irregular_interval(
                timing._timestamps + other._timestamps
            )
