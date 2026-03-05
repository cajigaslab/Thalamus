from __future__ import annotations

from typing_extensions import Literal

from nitypes.waveform.errors import (
    CapacityMismatchError,
    CapacityTooSmallError,
    DatatypeMismatchError,
    IrregularTimestampCountMismatchError,
    StartIndexTooLargeError,
    StartIndexOrSampleCountTooLargeError,
    NoTimestampInformationError,
    SampleIntervalModeMismatchError,
    SignalCountMismatchError,
)


def create_capacity_mismatch_error(capacity: int, array_length: int) -> CapacityMismatchError:
    """Create an error for a capacity-length mismatch."""
    message = (
        f"The capacity must match the input array length.\n\n"
        f"Capacity: {capacity}\n"
        f"Array length: {array_length}"
    )
    return CapacityMismatchError(message)


def create_capacity_too_small_error(
    capacity: int, min_capacity: int, object_description: str
) -> CapacityTooSmallError:
    """Create an error for when capacity is too small."""
    message = (
        f"The capacity must be equal to or greater than the number of samples in the {object_description}.\n\n"
        f"Capacity: {capacity}\n"
        f"Number of samples: {min_capacity}"
    )
    return CapacityTooSmallError(message)


def create_datatype_mismatch_error(
    arg_description: Literal["input array", "input spectrum", "input waveform"],
    arg_dtype: object,
    other_description: Literal["requested", "spectrum", "waveform"],
    other_dtype: object,
) -> DatatypeMismatchError:
    """Create an error for a data type mismatch."""
    arg_key = {
        "input array": "Input array data type",
        "input spectrum": "Input spectrum data type",
        "input waveform": "Input waveform data type",
    }
    other_key = {
        "requested": "Requested data type",
        "spectrum": "Spectrum data type",
        "waveform": "Waveform data type",
    }
    message = (
        f"The data type of the {arg_description} must match the {other_description} data type.\n\n"
        f"{arg_key[arg_description]}: {arg_dtype}\n"
        f"{other_key[other_description]}: {other_dtype}"
    )
    return DatatypeMismatchError(message)


def create_irregular_timestamp_count_mismatch_error(
    irregular_timestamp_count: int,
    other_description: Literal["input array length", "number of samples in the waveform"],
    other: int,
    *,
    reversed: bool = False,
) -> IrregularTimestampCountMismatchError:
    """Create an error for an irregular timestamp count mismatch."""
    other_key = {
        "input array length": "Array length",
        "number of samples in the waveform": "Number of samples",
    }
    if reversed:
        message = (
            "The input array length must be equal to the number of irregular timestamps.\n\n"
            f"{other_key[other_description]}: {other}\n"
            f"Number of timestamps: {irregular_timestamp_count}"
        )
    else:
        message = (
            f"The number of irregular timestamps must be equal to the {other_description}.\n\n"
            f"Number of timestamps: {irregular_timestamp_count}\n"
            f"{other_key[other_description]}: {other}"
        )
    return IrregularTimestampCountMismatchError(message)


def create_start_index_too_large_error(
    start_index: int,
    capacity_description: Literal[
        "capacity",
        "input array length",
        "number of samples in the spectrum",
        "number of samples in the waveform",
    ],
    capacity: int,
) -> StartIndexTooLargeError:
    """Create an error for an invalid start index argument."""
    capacity_key = {
        "capacity": "Capacity",
        "input array length": "Array length",
        "number of samples in the spectrum": "Number of samples",
        "number of samples in the waveform": "Number of samples",
    }
    message = (
        f"The start index must be less than or equal to the {capacity_description}.\n\n"
        f"Start index: {start_index}\n"
        f"{capacity_key[capacity_description]}: {capacity}"
    )
    return StartIndexTooLargeError(message)


def create_start_index_or_sample_count_too_large_error(
    start_index: int,
    sample_count: int,
    capacity_description: Literal[
        "capacity",
        "input array length",
        "number of samples in the expected waveform",
        "number of samples in the spectrum",
        "number of samples in the waveform",
    ],
    capacity: int,
) -> StartIndexOrSampleCountTooLargeError:
    """Create an error for an invalid start index or sample count argument."""
    capacity_key = {
        "capacity": "Capacity",
        "input array length": "Array length",
        "number of samples in the expected waveform": "Number of samples",
        "number of samples in the spectrum": "Number of samples",
        "number of samples in the waveform": "Number of samples",
    }
    message = (
        f"The sum of the start index and sample count must be less than or equal to the {capacity_description}.\n\n"
        f"Start index: {start_index}\n"
        f"Sample count: {sample_count}\n"
        f"{capacity_key[capacity_description]}: {capacity}"
    )
    return StartIndexOrSampleCountTooLargeError(message)


def create_no_timestamp_information_error() -> NoTimestampInformationError:
    """Create an error for waveform timing with no timestamp information."""
    message = (
        "The waveform timing does not have valid timestamp information. "
        "To obtain timestamps, the waveform must be irregular or must be initialized "
        "with a valid time stamp and sample interval."
    )
    return NoTimestampInformationError(message)


def create_sample_interval_mode_mismatch_error() -> SampleIntervalModeMismatchError:
    """Create an error for mixing none/regular with irregular timing."""
    message = (
        "The timing of one or more waveforms does not match the timing of the current waveform."
    )
    return SampleIntervalModeMismatchError(message)


def create_signal_count_mismatch_error(
    arg_description: Literal["expected waveform", "input array", "input waveform", "provided"],
    arg_signal_count: int,
    other_description: Literal["array", "port", "waveform"],
    other_signal_count: int,
) -> SignalCountMismatchError:
    """Create an error for a mismatched signal count."""
    arg_key = {
        "expected waveform": "Expected waveform signal count",
        "input array": "Input array signal count",
        "input waveform": "Input waveform signal count",
        "provided": "Signal count",
    }
    other_key = {
        "array": "Array signal count",
        "port": "Port signal count",
        "waveform": "Waveform signal count",
    }
    message = (
        f"The {arg_description} signal count must match the {other_description} signal count.\n\n"
        f"{arg_key[arg_description]}: {arg_signal_count}\n"
        f"{other_key[other_description]}: {other_signal_count}"
    )
    return SignalCountMismatchError(message)
