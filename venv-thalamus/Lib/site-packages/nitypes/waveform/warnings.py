"""Custom warning classes for waveforms."""

from __future__ import annotations


class ScalingMismatchWarning(RuntimeWarning):
    """Warning used when appending waveforms with mismatched scaling information."""

    pass


class TimingMismatchWarning(RuntimeWarning):
    """Warning used when appending waveforms with mismatched timing information."""

    pass
