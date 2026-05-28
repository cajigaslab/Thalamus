from __future__ import annotations

from nitypes.waveform.warnings import TimingMismatchWarning, ScalingMismatchWarning


def sample_interval_mismatch() -> TimingMismatchWarning:
    """Create a TimingMismatchWarning about appending waveforms with mismatched sample intervals."""
    return TimingMismatchWarning(
        "The sample interval of one or more waveforms does not match the sample interval of the current waveform."
    )


def scale_mode_mismatch() -> ScalingMismatchWarning:
    """Create a ScalingMismatchwarning about appending waveforms with mismatched scale modes."""
    return ScalingMismatchWarning(
        "The scale mode of one or more waveforms does not match the scale mode of the current waveform."
    )
