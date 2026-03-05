"""Sample interval strategies for waveform timing."""

from typing import Any

from nitypes._exceptions import invalid_arg_value
from nitypes.waveform._timing._sample_interval._base import SampleIntervalStrategy
from nitypes.waveform._timing._sample_interval._irregular import (
    IrregularSampleIntervalStrategy,
)
from nitypes.waveform._timing._sample_interval._mode import SampleIntervalMode
from nitypes.waveform._timing._sample_interval._none import NoneSampleIntervalStrategy
from nitypes.waveform._timing._sample_interval._regular import (
    RegularSampleIntervalStrategy,
)

__all__ = [
    "create_sample_interval_strategy",
    "IrregularSampleIntervalStrategy",
    "NoneSampleIntervalStrategy",
    "RegularSampleIntervalStrategy",
    "SampleIntervalMode",
    "SampleIntervalStrategy",
]


def create_sample_interval_strategy(
    sample_interval_mode: SampleIntervalMode,
) -> SampleIntervalStrategy[Any, Any, Any]:
    """Create a sample interval strategy for the specified mode."""
    strategy_type = _SAMPLE_INTERVAL_STRATEGY_TYPE_FOR_MODE.get(sample_interval_mode)
    if strategy_type is None:
        raise invalid_arg_value(
            "sample interval mode", "SampleIntervalMode object", sample_interval_mode
        )
    return strategy_type()


_SAMPLE_INTERVAL_STRATEGY_TYPE_FOR_MODE: dict[
    SampleIntervalMode, type[SampleIntervalStrategy[Any, Any, Any]]
] = {
    SampleIntervalMode.NONE: NoneSampleIntervalStrategy,
    SampleIntervalMode.REGULAR: RegularSampleIntervalStrategy,
    SampleIntervalMode.IRREGULAR: IrregularSampleIntervalStrategy,
}
