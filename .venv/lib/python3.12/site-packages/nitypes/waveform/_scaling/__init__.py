"""Waveform scaling data types for NI Python APIs."""

from nitypes.waveform._scaling._base import ScaleMode
from nitypes.waveform._scaling._linear import LinearScaleMode
from nitypes.waveform._scaling._none import NoneScaleMode

__all__ = ["LinearScaleMode", "NO_SCALING", "NoneScaleMode", "ScaleMode"]

NO_SCALING = NoneScaleMode()
"""A scale mode that does not scale data."""
