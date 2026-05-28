"""Digital waveform data types."""

from nitypes.waveform._digital._signal import DigitalWaveformSignal
from nitypes.waveform._digital._signal_collection import DigitalWaveformSignalCollection
from nitypes.waveform._digital._state import DigitalState
from nitypes.waveform._digital._waveform import (
    DigitalWaveform,
    DigitalWaveformFailure,
    DigitalWaveformTestResult,
)

__all__ = [
    "DigitalState",
    "DigitalWaveform",
    "DigitalWaveformFailure",
    "DigitalWaveformSignal",
    "DigitalWaveformSignalCollection",
    "DigitalWaveformTestResult",
]
