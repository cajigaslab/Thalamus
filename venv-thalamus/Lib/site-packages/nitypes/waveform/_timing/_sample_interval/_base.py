from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Iterable, Sequence
from typing import TYPE_CHECKING, Generic

from nitypes.waveform._timing._sample_interval._mode import SampleIntervalMode
from nitypes.waveform.typing import TSampleInterval_co, TTimeOffset_co, TTimestamp_co

if TYPE_CHECKING:
    from nitypes.waveform._timing._timing import Timing  # circular import


class SampleIntervalStrategy(ABC, Generic[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]):
    """Implements SampleIntervalMode specific behavior."""

    # Note that timing is always passed as a parameter. The timing object has a reference to the
    # strategy, so saving a reference to the timing object would introduce a reference cycle.
    __slots__ = ()

    @abstractmethod
    def validate_init_args(
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        sample_interval_mode: SampleIntervalMode,
        timestamp: TTimestamp_co | None,
        time_offset: TTimeOffset_co | None,
        sample_interval: TSampleInterval_co | None,
        timestamps: Sequence[TTimestamp_co] | None,
    ) -> None:
        """Validate the BaseTiming.__init__ arguments for this mode."""
        raise NotImplementedError

    @abstractmethod
    def get_timestamps(
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        start_index: int,
        count: int,
    ) -> Iterable[TTimestamp_co]:
        """Get or generate timestamps for the specified samples."""
        raise NotImplementedError

    @abstractmethod
    def append_timestamps(
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        timestamps: Sequence[TTimestamp_co] | None,
    ) -> Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]:
        """Append timestamps and return a new waveform timing if needed."""
        raise NotImplementedError

    @abstractmethod
    def append_timing(
        self,
        timing: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
        other: Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co],
    ) -> Timing[TTimestamp_co, TTimeOffset_co, TSampleInterval_co]:
        """Append timing and return a new waveform timing if needed."""
        raise NotImplementedError
