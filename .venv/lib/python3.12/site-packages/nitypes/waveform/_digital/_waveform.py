from __future__ import annotations

import datetime as dt
import sys
import weakref
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Generic, Literal, SupportsIndex, overload

import hightime as ht
import numpy as np
import numpy.typing as npt
from typing_extensions import Self

from nitypes._arguments import arg_to_uint, validate_dtype, validate_unsupported_arg
from nitypes._exceptions import invalid_arg_type, invalid_array_ndim
from nitypes._numpy import asarray as _np_asarray
from nitypes.time.typing import AnyDateTime, AnyTimeDelta
from nitypes.waveform._digital._port import bit_mask, get_port_dtype, port_to_line_data
from nitypes.waveform._exceptions import (
    create_capacity_mismatch_error,
    create_capacity_too_small_error,
    create_datatype_mismatch_error,
    create_irregular_timestamp_count_mismatch_error,
    create_signal_count_mismatch_error,
    create_start_index_or_sample_count_too_large_error,
    create_start_index_too_large_error,
)
from nitypes.waveform._extended_properties import CHANNEL_NAME, LINE_NAMES
from nitypes.waveform._types import DIGITAL_PORT_DTYPES, DIGITAL_STATE_DTYPES
from nitypes.waveform.typing import (
    ExtendedPropertyValue,
    TDigitalState,
    TOtherDigitalState,
)

if sys.version_info < (3, 10):
    import array as std_array

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import (
        DigitalState,
        DigitalWaveformSignalCollection,
        ExtendedPropertyDictionary,
        Timing,
    )
else:
    from nitypes.waveform._digital._signal_collection import (
        DigitalWaveformSignalCollection,
    )
    from nitypes.waveform._digital._state import DigitalState
    from nitypes.waveform._extended_properties import ExtendedPropertyDictionary
    from nitypes.waveform._timing import Timing


@dataclass(frozen=True)
class DigitalWaveformFailure:
    """A test failure, indicating where the actual waveform did not match the expected waveform."""

    sample_index: int
    """The sample index into the compared waveform where the test failure occurred."""

    expected_sample_index: int
    """The sample index into the expected waveform where the test failure occurred."""

    signal_index: int
    """The signal index where the test failure occurred."""

    actual_state: DigitalState
    """The state from the compared waveform where the test failure occurred."""

    expected_state: DigitalState
    """The state from the expected waveform where the test failure occurred."""


@dataclass(frozen=True)
class DigitalWaveformTestResult:
    """A test result from comparing a digital waveform against an expected digital waveform."""

    @property
    def success(self) -> bool:
        """True if the test is successful, False if the test failed."""
        return len(self.failures) == 0

    failures: Sequence[DigitalWaveformFailure]
    """A collection of test failure information."""


class DigitalWaveform(Generic[TDigitalState]):
    """A digital waveform, which encapsulates digital data and timing information.

    Constructing
    ^^^^^^^^^^^^

    To construct a digital waveform, use the :class:`DigitalWaveform` class:

    >>> DigitalWaveform()
    nitypes.waveform.DigitalWaveform(0, 1)
    >>> DigitalWaveform(sample_count=5, signal_count=3)  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.DigitalWaveform(5, 3, data=array([[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0],
    [0, 0, 0]], dtype=uint8))

    When displaying a digital waveform as a string, the first number is the sample count and the second
    number is the signal count.

    To construct a digital waveform from a NumPy array of line data, use the
    :any:`DigitalWaveform.from_lines` method. Each array element represents a digital state, such as 1
    for "on" or 0 for "off". The line data should be in a 1D array indexed by sample or a 2D array
    indexed by (sample, signal). *(Note, signal indices are reversed! See "Signal index vs. column index"
    below for details.)* The digital waveform displays the line data as a 2D array.

    >>> import numpy as np
    >>> DigitalWaveform.from_lines(np.array([0, 1, 0], np.uint8))
    nitypes.waveform.DigitalWaveform(3, 1, data=array([[0], [1], [0]], dtype=uint8))
    >>> DigitalWaveform.from_lines(np.array([[0, 0], [1, 0], [0, 1], [1, 1]], np.uint8))
    nitypes.waveform.DigitalWaveform(4, 2, data=array([[0, 0], [1, 0], [0, 1], [1, 1]], dtype=uint8))

    You can also use :any:`DigitalWaveform.from_lines` to construct a digital waveform from a sequence,
    such as a list.

    >>> DigitalWaveform.from_lines([[0, 0], [1, 0], [0, 1], [1, 1]])
    nitypes.waveform.DigitalWaveform(4, 2, data=array([[0, 0], [1, 0], [0, 1], [1, 1]], dtype=uint8))

    To construct a digital waveform from a NumPy array of port data, use the
    :any:`DigitalWaveform.from_port` method. Each element of the port data array represents a digital
    sample taken over a port of signals. Each bit in the sample is a signal value, either 1 for "on" or
    0 for "off". *(Note, signal indices are reversed! See "Signal index vs. column index" below for
    details.)*

    >>> DigitalWaveform.from_port(np.array([0, 1, 2, 3], np.uint8))  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.DigitalWaveform(4, 8, data=array([[0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 1], [0, 0, 0, 0, 0, 0, 1, 0], [0, 0, 0, 0, 0, 0, 1, 1]], dtype=uint8))

    You can use a mask to specify which lines in the port to include in the waveform.

    >>> DigitalWaveform.from_port(np.array([0, 1, 2, 3], np.uint8), 0x3)
    nitypes.waveform.DigitalWaveform(4, 2, data=array([[0, 0], [0, 1], [1, 0], [1, 1]], dtype=uint8))

    You can also use a non-NumPy sequence such as a list, but you must specify a mask so the waveform
    knows how many bits are in each list element.

    >>> DigitalWaveform.from_port([0, 1, 2, 3], 0x3)
    nitypes.waveform.DigitalWaveform(4, 2, data=array([[0, 0], [0, 1], [1, 0], [1, 1]], dtype=uint8))

    The 2D version, :any:`DigitalWaveform.from_ports`, returns multiple waveforms, one for each row of
    data in the array or nested sequence.

    >>> nested_list = [[0, 1, 2, 3], [3, 0, 3, 0]]
    >>> DigitalWaveform.from_ports(nested_list, [0x3, 0x3])  # doctest: +NORMALIZE_WHITESPACE
    [nitypes.waveform.DigitalWaveform(4, 2, data=array([[0, 0], [0, 1], [1, 0], [1, 1]], dtype=uint8)),
    nitypes.waveform.DigitalWaveform(4, 2, data=array([[1, 1], [0, 0], [1, 1], [0, 0]], dtype=uint8))]

    Digital signals
    ^^^^^^^^^^^^^^^

    You can access individual signals using the :any:`DigitalWaveform.signals` property.

    >>> wfm = DigitalWaveform.from_port([0, 1, 2, 3], 0x3)
    >>> wfm.signals[0]
    nitypes.waveform.DigitalWaveformSignal(data=array([0, 1, 0, 1], dtype=uint8))
    >>> wfm.signals[1]
    nitypes.waveform.DigitalWaveformSignal(data=array([0, 0, 1, 1], dtype=uint8))

    The :any:`DigitalWaveformSignal.data` property returns a view of the data for that signal.

    >>> wfm.signals[0].data
    array([0, 1, 0, 1], dtype=uint8)

    Signal index vs. column index
    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    Each :class:`DigitalWaveformSignal` has two index properties:

    * :attr:`DigitalWaveformSignal.signal_index` - The position in the :attr:`DigitalWaveform.signals`
      collection (0-based from the first signal). signal_index 0 is the rightmost column in the data.
    * :attr:`DigitalWaveformSignal.column_index` - The column in the :attr:`DigitalWaveform.data`
      array, e.g. `waveform.data[:, column_index]`. column_index 0 is the leftmost column in the data.

    These indices are reversed with respect to each other. signal_index 0 (line 0) corresponds to
    the highest column_index, and the highest signal_index (the highest line) corresponds to
    column_index 0. This ordering follows industry conventions where line 0 is the least
    significant bit and appears last (in the rightmost column) of the data array.

    >>> wfm = DigitalWaveform.from_port([0, 1, 2, 3], 0x7)  # 3 signals
    >>> wfm.data
    array([[0, 0, 0],
           [0, 0, 1],
           [0, 1, 0],
           [0, 1, 1]], dtype=uint8)
    >>> wfm.signals[0].signal_index
    0
    >>> wfm.signals[0].column_index
    2
    >>> wfm.signals[0].data
    array([0, 1, 0, 1], dtype=uint8)
    >>> wfm.signals[2].signal_index
    2
    >>> wfm.signals[2].column_index
    0
    >>> wfm.signals[2].data
    array([0, 0, 0, 0], dtype=uint8)

    Digital signal names
    ^^^^^^^^^^^^^^^^^^^^

    The :any:`DigitalWaveformSignal.name` property allows you to get and set the signal names.

    >>> wfm.signals[0].name = "port0/line0"
    >>> wfm.signals[1].name = "port0/line1"
    >>> wfm.signals[2].name = "port0/line2"
    >>> wfm.signals[0].name
    'port0/line0'
    >>> wfm.signals[0]
    nitypes.waveform.DigitalWaveformSignal(name='port0/line0', data=array([0, 1, 0, 1], dtype=uint8))

    The signal names are stored in the ``NI_LineNames`` extended property on the digital waveform.
    Note that the order of the names in the string follows column_index order (highest line number
    first), which is reversed compared to signal_index order (lowest line first). This means line 0
    (signal_index 0) appears last in the NI_LineNames string. This matches industry conventions
    where line 0 appears in the rightmost column of the data array.

    >>> wfm.extended_properties["NI_LineNames"]
    'port0/line2, port0/line1, port0/line0'

    When creating a digital waveform, you can directly set the ``NI_LineNames`` extended property.

    >>> wfm = DigitalWaveform.from_port([2, 4], 0x7,
    ... extended_properties={"NI_LineNames": "Dev1/port1/line6, Dev1/port1/line5, Dev1/port1/line4"})
    >>> wfm.signals[0]
    nitypes.waveform.DigitalWaveformSignal(name='Dev1/port1/line4', data=array([0, 0], dtype=uint8))
    >>> wfm.signals[1]
    nitypes.waveform.DigitalWaveformSignal(name='Dev1/port1/line5', data=array([1, 0], dtype=uint8))
    >>> wfm.signals[2]
    nitypes.waveform.DigitalWaveformSignal(name='Dev1/port1/line6', data=array([0, 1], dtype=uint8))

    Digital state types
    ^^^^^^^^^^^^^^^^^^^

    By default, digital waveforms use a NumPy ``dtype`` of :any:`numpy.uint8`, which uses a byte of
    memory for each digital state.

    Using ``np.uint8`` allows the waveform to contain digital states other than "on" or off", such as
    such as :any:`DigitalState.FORCE_OFF` (``X``) or :any:`DigitalState.COMPARE_HIGH` (``H``). This
    capability is used for digital pattern applications.

    You can also construct a digital waveform using a NumPy ``dtype`` of :any:`numpy.bool`. This also
    uses a byte of memory for each digital state, but it restricts the states to "on" and "off".

    Testing digital waveforms
    ^^^^^^^^^^^^^^^^^^^^^^^^^

    You can use :meth:`DigitalWaveform.test` to compare an acquired waveform against an expected
    waveform. This returns a :class:`DigitalWaveformTestResult` object, which has a Boolean ``success``
    property and a ``failures`` property containing a collection of :class:`DigitalWaveformFailure`
    objects, which indicate the location of each test failure.

    Here is an example. The expected waveform counts in binary using ``COMPARE_LOW`` (``L``) and
    ``COMPARE_HIGH`` (``H``), but signal 0 of the actual waveform is stuck high.

    >>> actual = DigitalWaveform.from_lines([[0, 1], [1, 1], [0, 1], [1, 1]])
    >>> expected = DigitalWaveform.from_lines([[DigitalState.COMPARE_LOW, DigitalState.COMPARE_LOW],
    ... [DigitalState.COMPARE_HIGH, DigitalState.COMPARE_LOW],
    ... [DigitalState.COMPARE_LOW, DigitalState.COMPARE_HIGH],
    ... [DigitalState.COMPARE_HIGH, DigitalState.COMPARE_HIGH]])
    >>> result = actual.test(expected)
    >>> result.success
    False
    >>> len(result.failures)
    2

    The failures indicate the sample indices into the actual and expected waveforms, the signal index,
    and the digital state from the actual and expected waveforms:

    >>> result.failures[0]  # doctest: +NORMALIZE_WHITESPACE
    DigitalWaveformFailure(sample_index=0, expected_sample_index=0, signal_index=0, column_index=1,
    actual_state=<DigitalState.FORCE_UP: 1>, expected_state=<DigitalState.COMPARE_LOW: 3>)
    >>> result.failures[1]  # doctest: +NORMALIZE_WHITESPACE
    DigitalWaveformFailure(sample_index=1, expected_sample_index=1, signal_index=0, column_index=1,
    actual_state=<DigitalState.FORCE_UP: 1>, expected_state=<DigitalState.COMPARE_LOW: 3>)

    Timing information
    ^^^^^^^^^^^^^^^^^^

    Digital waveforms have the same timing information as analog waveforms.

    Class members
    ^^^^^^^^^^^^^
    """  # noqa: W505 - doc line too long

    @overload
    @classmethod
    def from_lines(
        cls,
        array: npt.NDArray[TOtherDigitalState],
        dtype: None = ...,
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        signal_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> DigitalWaveform[TOtherDigitalState]: ...

    @overload
    @classmethod
    def from_lines(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        dtype: type[TOtherDigitalState] | np.dtype[TOtherDigitalState],
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        signal_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> DigitalWaveform[TOtherDigitalState]: ...

    @overload
    @classmethod
    def from_lines(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        dtype: npt.DTypeLike = ...,
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        signal_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> DigitalWaveform[Any]: ...

    @classmethod
    def from_lines(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        dtype: npt.DTypeLike = None,
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
        signal_count: SupportsIndex | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = None,
    ) -> DigitalWaveform[Any]:
        """Construct a waveform from a one or two-dimensional array or sequence of line data.

        Each element of the line data array represents a digital state, such as 1 for "on" or 0
        for "off". The line data should be in a 1D array indexed by sample or a 2D array indexed
        by (sample, signal). The line data may also use digital state values from the
        :class:`DigitalState` enum.

        Note that signal indices are reversed with respect to this array's column indices.
        The first column in each sample corresponds to the highest line number and highest signal
        index. The last column in each sample corresponds to line 0 and signal index 0.

        Args:
            array: The line data as a one or two-dimensional array or a sequence.
            dtype: The NumPy data type for the waveform data.
            copy: Specifies whether to copy the array or save a reference to it.
            start_index: The sample index at which the waveform data begins.
            sample_count: The number of samples in the waveform.
            signal_count: The number of signals in the waveform.
            extended_properties: The extended properties of the waveform.
            timing: The timing information of the waveform.

        Returns:
            A waveform containing the specified data.
        """
        if isinstance(array, np.ndarray):
            if array.ndim not in (1, 2):
                raise invalid_array_ndim(
                    "input array", "one or two-dimensional array or sequence", array.ndim
                )
            if dtype is not None and array.dtype != dtype:
                raise create_datatype_mismatch_error("input array", array.dtype, "requested", dtype)
        elif isinstance(array, Sequence) or (
            sys.version_info < (3, 10) and isinstance(array, std_array.array)
        ):
            if dtype is None:
                dtype = np.uint8
        else:
            raise invalid_arg_type("input array", "one or two-dimensional array or sequence", array)

        return cls(
            data=_np_asarray(array, dtype, copy=copy),
            start_index=start_index,
            sample_count=sample_count,
            signal_count=signal_count,
            extended_properties=extended_properties,
            timing=timing,
        )

    @overload
    @classmethod
    def from_port(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        mask: SupportsIndex | None = ...,
        dtype: None = ...,
        *,
        bitorder: Literal["big", "little"] = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> DigitalWaveform[np.uint8]: ...

    @overload
    @classmethod
    def from_port(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        mask: SupportsIndex | None = ...,
        dtype: (
            type[TOtherDigitalState]  # pyright: ignore[reportInvalidTypeVarUse]
            | np.dtype[TOtherDigitalState]
        ) = ...,
        *,
        bitorder: Literal["big", "little"] = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> DigitalWaveform[TOtherDigitalState]: ...

    @overload
    @classmethod
    def from_port(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        mask: SupportsIndex | None = ...,
        dtype: npt.DTypeLike = ...,
        *,
        bitorder: Literal["big", "little"] = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> DigitalWaveform[Any]: ...

    @classmethod
    def from_port(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        mask: SupportsIndex | None = None,
        dtype: npt.DTypeLike = None,
        *,
        bitorder: Literal["big", "little"] = "big",
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = None,
    ) -> DigitalWaveform[Any]:
        """Construct a waveform from a one-dimensional array or sequence of port data.

        This method allocates a new array in order to convert the port data (integers) to line data
        (bits).

        Each element of the port data array represents a digital sample taken over a port of
        signals. Each bit in the sample represents a digital state, either 1 for "on" or 0 for
        "off".

        When bitorder='big' (default), the integers in the samples are big-endian. The most
        significant bit of each integer will be placed in the first column of the data array
        (corresponding to the highest line number and highest signal index). The least significant
        bit will be placed in the last column of the data array (corresponding to line 0 and signal
        index 0).

        When bitorder='little', the integers in the samples are little-endian. The least
        significant bit of each integer will be placed in the first column of the data array
        (corresponding to the highest line number and highest signal index). The most significant
        bit will be placed in the last column of the data array (corresponding to line 0 and signal
        index 0).

        If the input array is not a NumPy array, you must specify the mask.

        Args:
            array: The port data as a one-dimensional array or a sequence.
            mask: A bitmask specifying which lines to include in the waveform.
            dtype: The NumPy data type for the waveform (line) data.
            bitorder: The bit ordering to use when unpacking port data ('big' or 'little').
                Defaults to 'big'.
            start_index: The sample index at which the waveform data begins.
            sample_count: The number of samples in the waveform.
            extended_properties: The extended properties of the waveform.
            timing: The timing information of the waveform.

        Returns:
            A waveform containing the specified data.
        """
        if isinstance(array, np.ndarray):
            if array.ndim != 1:
                raise invalid_array_ndim(
                    "input array", "one-dimensional array or sequence", array.ndim
                )
            validate_dtype(array.dtype, DIGITAL_PORT_DTYPES)
            default_mask = bit_mask(array.dtype.itemsize * 8)
        elif isinstance(array, Sequence) or (
            sys.version_info < (3, 10) and isinstance(array, std_array.array)
        ):
            # np.array([0,1]).dtype is np.int64 by default, so the user must specify the port size.
            if mask is None:
                raise ValueError(
                    "You must specify a mask when the input array is not a NumPy array."
                )
            default_mask = 0
        else:
            raise invalid_arg_type("input array", "one or two-dimensional array or sequence", array)

        if dtype is None:
            dtype = np.uint8
        validate_dtype(dtype, DIGITAL_STATE_DTYPES)

        mask = arg_to_uint("mask", mask, default_mask)

        if isinstance(array, np.ndarray):
            port_dtype = array.dtype
        else:
            port_dtype = get_port_dtype(mask)

        port_data = _np_asarray(array, port_dtype)
        line_data = port_to_line_data(port_data, mask, bitorder)
        if line_data.dtype != dtype:
            line_data = line_data.view(dtype)

        return cls(
            data=line_data,
            dtype=dtype,
            start_index=start_index,
            sample_count=sample_count,
            extended_properties=extended_properties,
            timing=timing,
        )

    @overload
    @classmethod
    def from_ports(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        masks: Sequence[SupportsIndex] | None = ...,
        dtype: None = ...,
        *,
        bitorder: Literal["big", "little"] = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> Sequence[DigitalWaveform[np.uint8]]: ...

    @overload
    @classmethod
    def from_ports(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        masks: Sequence[SupportsIndex] | None = ...,
        dtype: (
            type[TOtherDigitalState]  # pyright: ignore[reportInvalidTypeVarUse]
            | np.dtype[TOtherDigitalState]
        ) = ...,
        *,
        bitorder: Literal["big", "little"] = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> Sequence[DigitalWaveform[TOtherDigitalState]]: ...

    @overload
    @classmethod
    def from_ports(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        masks: Sequence[SupportsIndex] | None = ...,
        dtype: npt.DTypeLike = ...,
        *,
        bitorder: Literal["big", "little"] = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> Sequence[DigitalWaveform[Any]]: ...

    @classmethod
    def from_ports(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        masks: Sequence[SupportsIndex] | None = None,
        dtype: npt.DTypeLike = None,
        *,
        bitorder: Literal["big", "little"] = "big",
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = None,
    ) -> Sequence[DigitalWaveform[Any]]:
        """Construct a waveform from a two-dimensional array or sequence of port data.

        This method allocates a new array in order to convert the port data to line data.

        Each row of the port data array corresponds to a resulting DigitalWaveform. Each element of
        the port data array represents a digital sample taken over a port of signals. Each bit in
        the sample represents a digital state, either 1 for "on" or 0 for "off".

        When bitorder='big' (default), the integers in the samples are big-endian. The most
        significant bit of each integer will be placed in the first column of the data array
        (corresponding to the highest line number and highest signal index). The least significant
        bit will be placed in the last column of the data array (corresponding to line 0 and signal
        index 0).

        When bitorder='little', the integers in the samples are little-endian. The least
        significant bit of each integer will be placed in the first column of the data array
        (corresponding to the highest line number and highest signal index). The most significant
        bit will be placed in the last column of the data array (corresponding to line 0 and signal
        index 0).

        If the input array is not a NumPy array, you must specify the masks.

        Args:
            array: The port data as a two-dimensional array or a sequence.
            masks: A sequence of bitmasks specifying which lines from each port to include in the
                corresponding waveform.
            dtype: The NumPy data type for the waveform (line) data.
            bitorder: The bit ordering to use when unpacking port data ('big' or 'little').
                Defaults to 'big'.
            start_index: The sample index at which the waveform data begins.
            sample_count: The number of samples in the waveform.
            extended_properties: The extended properties of the waveform.
            timing: The timing information of the waveform.

        Returns:
            A waveform containing the specified data.
        """
        if isinstance(array, np.ndarray):
            if array.ndim != 2:
                raise invalid_array_ndim(
                    "input array", "two-dimensional array or sequence", array.ndim
                )
            validate_dtype(array.dtype, DIGITAL_PORT_DTYPES)
            default_masks = [bit_mask(array.dtype.itemsize * 8)] * array.shape[0]
        elif isinstance(array, Sequence) or (
            sys.version_info < (3, 10) and isinstance(array, std_array.array)
        ):
            # np.array([0,1]).dtype is np.int64 by default, so the user must specify the port size.
            if masks is None:
                raise ValueError(
                    "You must specify the masks when the input array is not a NumPy array."
                )
            default_masks = []
        else:
            raise invalid_arg_type("input array", "one or two-dimensional array or sequence", array)

        if dtype is None:
            dtype = np.uint8
        validate_dtype(dtype, DIGITAL_STATE_DTYPES)

        if not isinstance(masks, (type(None), Sequence)):
            raise invalid_arg_type("masks", "sequence of ints")
        if masks is not None:
            validated_masks = [arg_to_uint("mask", mask) for mask in masks]
        else:
            validated_masks = default_masks

        if isinstance(array, np.ndarray):
            port_dtype = array.dtype
        else:
            port_dtype = get_port_dtype(validated_masks)

        port_data = _np_asarray(array, port_dtype)
        waveforms = []
        for port_index in range(port_data.shape[0]):
            line_data = port_to_line_data(
                port_data[port_index, :], validated_masks[port_index], bitorder
            )
            if line_data.dtype != dtype:
                line_data = line_data.view(dtype)

            waveforms.append(
                cls(
                    data=line_data,
                    dtype=dtype,
                    start_index=start_index,
                    sample_count=sample_count,
                    extended_properties=extended_properties,
                    timing=timing,
                )
            )
        return waveforms

    __slots__ = [
        "_data",
        "_data_1d",
        "_start_index",
        "_sample_count",
        "_extended_properties",
        "_timing",
        "_signals",
        "_line_names",
        "__weakref__",
    ]

    _data: npt.NDArray[TDigitalState]
    _data_1d: npt.NDArray[TDigitalState] | None
    _start_index: int
    _sample_count: int
    _extended_properties: ExtendedPropertyDictionary
    _timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta]
    _signals: DigitalWaveformSignalCollection[TDigitalState] | None
    _line_names: list[str] | None

    # If neither dtype nor data is specified, _TData defaults to np.uint8.
    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: DigitalWaveform[np.uint8],
        sample_count: SupportsIndex | None = ...,
        signal_count: SupportsIndex | None = ...,
        dtype: None = ...,
        default_value: bool | int | DigitalState | None = ...,
        *,
        data: None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: DigitalWaveform[TOtherDigitalState],
        sample_count: SupportsIndex | None = ...,
        signal_count: SupportsIndex | None = ...,
        dtype: type[TOtherDigitalState] | np.dtype[TOtherDigitalState] = ...,
        default_value: bool | int | DigitalState | None = ...,
        *,
        data: None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: DigitalWaveform[TOtherDigitalState],
        sample_count: SupportsIndex | None = ...,
        signal_count: SupportsIndex | None = ...,
        dtype: None = ...,
        default_value: bool | int | DigitalState | None = ...,
        *,
        data: npt.NDArray[TOtherDigitalState] = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: DigitalWaveform[Any],
        sample_count: SupportsIndex | None = ...,
        signal_count: SupportsIndex | None = ...,
        dtype: npt.DTypeLike = ...,
        default_value: bool | int | DigitalState | None = ...,
        *,
        data: npt.NDArray[Any] | None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
    ) -> None: ...

    def __init__(
        self,
        sample_count: SupportsIndex | None = None,
        signal_count: SupportsIndex | None = None,
        dtype: npt.DTypeLike = None,
        default_value: bool | int | DigitalState | None = None,
        *,
        data: npt.NDArray[Any] | None = None,
        start_index: SupportsIndex | None = None,
        capacity: SupportsIndex | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        copy_extended_properties: bool = True,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = None,
    ) -> None:
        """Initialize a new digital waveform.

        Args:
            sample_count: The number of samples in the waveform.
            signal_count: The number of signals in the waveform.
            dtype: The NumPy data type for the waveform data.
            default_value: The :class:`DigitalState` to initialize the waveform with.
            data: A NumPy ndarray to use for sample storage. The waveform takes ownership
                of this array. If not specified, an ndarray is created based on the specified dtype,
                start index, sample count, and capacity.
            start_index: The sample index at which the waveform data begins.
            sample_count: The number of samples in the waveform.
            capacity: The number of samples to allocate. Pre-allocating a larger buffer optimizes
                appending samples to the waveform.
            extended_properties: The extended properties of the waveform.
            copy_extended_properties: Specifies whether to copy the extended properties or take
                ownership.
            timing: The timing information of the waveform.

        Returns:
            A digital waveform.
        """
        if data is None:
            self._init_with_new_array(
                sample_count,
                signal_count,
                dtype,
                default_value,
                start_index=start_index,
                capacity=capacity,
            )
        elif isinstance(data, np.ndarray):
            self._init_with_provided_array(
                data,
                dtype,
                start_index=start_index,
                sample_count=sample_count,
                signal_count=signal_count,
                capacity=capacity,
            )
        else:
            raise invalid_arg_type("raw data", "NumPy ndarray", data)

        if copy_extended_properties or not isinstance(
            extended_properties, ExtendedPropertyDictionary
        ):
            extended_properties = ExtendedPropertyDictionary(extended_properties)
        self._extended_properties = extended_properties
        if not hasattr(self._extended_properties, "_on_key_changed"):
            # when unpickling an old version, _on_key_changed may not exist
            self._extended_properties._on_key_changed = []
        self._extended_properties._on_key_changed.append(
            weakref.WeakMethod(self._on_extended_property_changed)
        )

        if timing is None:
            timing = Timing.empty
        self._timing = timing

        self._signals = None
        self._line_names = None

    def _on_extended_property_changed(self, key: str) -> None:
        if key == LINE_NAMES:
            self._line_names = None

    def _init_with_new_array(
        self,
        sample_count: SupportsIndex | None = None,
        signal_count: SupportsIndex | None = None,
        dtype: npt.DTypeLike = None,
        default_value: bool | int | DigitalState | None = None,
        *,
        start_index: SupportsIndex | None = None,
        capacity: SupportsIndex | None = None,
    ) -> None:
        start_index = arg_to_uint("start index", start_index, 0)
        sample_count = arg_to_uint("sample count", sample_count, 0)
        signal_count = arg_to_uint("signal count", signal_count, 1)
        capacity = arg_to_uint("capacity", capacity, sample_count)

        if dtype is None:
            dtype = np.uint8
        validate_dtype(dtype, DIGITAL_STATE_DTYPES)

        if start_index > capacity:
            raise create_start_index_too_large_error(start_index, "capacity", capacity)
        if start_index + sample_count > capacity:
            raise create_start_index_or_sample_count_too_large_error(
                start_index, sample_count, "capacity", capacity
            )

        if default_value is None:
            default_value = 0
        elif not isinstance(default_value, (bool, int, DigitalState)):
            raise invalid_arg_type("default value", "bool, int, or DigitalState", default_value)

        self._data = np.full((capacity, signal_count), default_value, dtype)
        self._data_1d = None
        self._start_index = start_index
        self._sample_count = sample_count

    def _init_with_provided_array(
        self,
        data: npt.NDArray[TDigitalState],
        dtype: npt.DTypeLike = None,
        *,
        start_index: SupportsIndex | None = None,
        sample_count: SupportsIndex | None = None,
        signal_count: SupportsIndex | None = None,
        capacity: SupportsIndex | None = None,
    ) -> None:
        if not isinstance(data, np.ndarray):
            raise invalid_arg_type("input array", "one or two-dimensional array", data)

        if dtype is None:
            dtype = data.dtype
        if dtype != data.dtype:
            raise create_datatype_mismatch_error(
                "input array", data.dtype, "requested", np.dtype(dtype)
            )
        validate_dtype(dtype, DIGITAL_STATE_DTYPES)

        if data.ndim == 1:
            data_signal_count = 1
            data_1d = data
            data = data.reshape(len(data), 1)
        elif data.ndim == 2:
            data_signal_count = data.shape[1]
            data_1d = None
        else:
            raise invalid_array_ndim("input array", "one or two-dimensional array", data.ndim)

        capacity = arg_to_uint("capacity", capacity, len(data))
        if capacity != len(data):
            raise create_capacity_mismatch_error(capacity, len(data))

        start_index = arg_to_uint("start index", start_index, 0)
        if start_index > capacity:
            raise create_start_index_too_large_error(start_index, "input array length", capacity)

        sample_count = arg_to_uint("sample count", sample_count, len(data) - start_index)
        if start_index + sample_count > len(data):
            raise create_start_index_or_sample_count_too_large_error(
                start_index, sample_count, "input array length", len(data)
            )

        signal_count = arg_to_uint("signal count", signal_count, data_signal_count)
        if signal_count != data_signal_count:
            raise create_signal_count_mismatch_error(
                "provided", signal_count, "array", data_signal_count
            )

        self._data = data
        self._data_1d = data_1d
        self._start_index = start_index
        self._sample_count = sample_count

    @property
    def signals(self) -> DigitalWaveformSignalCollection[TDigitalState]:
        """A collection of objects representing waveform signals."""
        # Lazily allocate self._signals if the application needs it.
        #
        # https://github.com/ni/nitypes-python/issues/131 - DigitalWaveform.signals introduces a
        # reference cycle, which affects GC overhead.
        value = self._signals
        if value is None:
            value = self._signals = DigitalWaveformSignalCollection(self)
        return value

    @property
    def data(self) -> npt.NDArray[TDigitalState]:
        """The waveform data, indexed by (sample, signal)."""
        return self._data[self._start_index : self._start_index + self._sample_count]

    def get_data(
        self, start_index: SupportsIndex | None = 0, sample_count: SupportsIndex | None = None
    ) -> npt.NDArray[TDigitalState]:
        """Get a subset of the waveform data.

        Args:
            start_index: The sample index at which the data begins.
            sample_count: The number of samples to return.

        Returns:
            A subset of the raw waveform data.
        """
        start_index = arg_to_uint("start index", start_index, 0)
        if start_index > self.sample_count:
            raise create_start_index_too_large_error(
                start_index, "number of samples in the waveform", self.sample_count
            )

        sample_count = arg_to_uint("sample count", sample_count, self.sample_count - start_index)
        if start_index + sample_count > self.sample_count:
            raise create_start_index_or_sample_count_too_large_error(
                start_index, sample_count, "number of samples in the waveform", self.sample_count
            )

        return self.data[start_index : start_index + sample_count]

    @property
    def sample_count(self) -> int:
        """The number of samples in the waveform."""
        return self._sample_count

    @sample_count.setter
    def sample_count(self, value: int) -> None:
        """Set the number of samples in the waveform."""
        value = arg_to_uint("sample count", value)
        if self._start_index + value > self.capacity:
            raise create_start_index_or_sample_count_too_large_error(
                self._start_index, value, "capacity", self.capacity
            )
        self._sample_count = value

    @property
    def signal_count(self) -> int:
        """The number of signals in the waveform."""
        # npt.NDArray[_ScalarT] currently has a shape type of _AnyShape, which is tuple[Any, ...]
        shape: tuple[int, ...] = self._data.shape
        return shape[1]

    @property
    def start_index(self) -> int:
        """The sample index of the underlying array at which the waveform data begins."""
        return self._start_index

    @property
    def capacity(self) -> int:
        """The total capacity available for waveform data.

        Setting the capacity resizes the underlying NumPy array in-place.

        * Other Python objects with references to the array will see the array size change.
        * If the array has a reference to an external buffer (such as an array.array), attempting
          to resize it raises ValueError.
        """
        return len(self._data)

    @capacity.setter
    def capacity(self, value: int) -> None:
        value = arg_to_uint("capacity", value)
        min_capacity = self._start_index + self._sample_count
        if value < min_capacity:
            raise create_capacity_too_small_error(value, min_capacity, "waveform")
        if value != len(self._data):
            if self._data_1d is not None:
                # If _data is a 2D view of a 1D array, resize the base array and recreate the view.
                self._data_1d.resize(value, refcheck=False)
                self._data = self._data_1d.reshape(len(self._data_1d), 1)
            else:
                self._data.resize((value, self.signal_count), refcheck=False)

    @property
    def dtype(self) -> np.dtype[TDigitalState]:
        """The NumPy dtype for the waveform data."""
        return self._data.dtype

    @property
    def extended_properties(self) -> ExtendedPropertyDictionary:
        """The extended properties for the waveform."""
        return self._extended_properties

    @property
    def channel_name(self) -> str:
        """The name of the device channel from which the waveform was acquired."""
        value = self._extended_properties.get(CHANNEL_NAME, "")
        assert isinstance(value, str)
        return value

    @channel_name.setter
    def channel_name(self, value: str) -> None:
        if not isinstance(value, str):
            raise invalid_arg_type("channel name", "str", value)
        self._extended_properties[CHANNEL_NAME] = value

    def _get_line_names(self) -> list[str]:
        # Lazily allocate self._line_names if the application needs it.
        line_names = self._line_names
        if line_names is None:
            line_names_str = self._extended_properties.get(LINE_NAMES, "")
            assert isinstance(line_names_str, str)
            line_names = self._line_names = [name.strip() for name in line_names_str.split(",")]
            if len(line_names) < self.signal_count:
                line_names.extend([""] * (self.signal_count - len(line_names)))
        return line_names

    def _get_line_name(self, column_index: int) -> str:
        return self._get_line_names()[column_index]

    def _set_line_name(self, column_index: int, value: str) -> None:
        line_names = self._get_line_names()
        line_names[column_index] = value
        self._extended_properties[LINE_NAMES] = ", ".join(line_names)

    def _set_timing(self, value: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta]) -> None:
        if self._timing is not value:
            self._timing = value

    def _validate_timing(self, value: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta]) -> None:
        if value._timestamps is not None and len(value._timestamps) != self._sample_count:
            raise create_irregular_timestamp_count_mismatch_error(
                len(value._timestamps), "number of samples in the waveform", self._sample_count
            )

    @property
    def timing(self) -> Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta]:
        """The timing information of the waveform.

        The default value is Timing.empty.
        """
        return self._timing

    @timing.setter
    def timing(self, value: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta]) -> None:
        if not isinstance(value, Timing):
            raise invalid_arg_type("timing information", "Timing object", value)
        self._validate_timing(value)
        self._set_timing(value)

    def append(
        self,
        other: (
            npt.NDArray[TDigitalState]
            | DigitalWaveform[TDigitalState]
            | Sequence[DigitalWaveform[TDigitalState]]
        ),
        /,
        timestamps: Sequence[dt.datetime] | Sequence[ht.datetime] | None = None,
    ) -> None:
        """Append data to the waveform.

        Args:
            other: The array or waveform(s) to append.
            timestamps: A sequence of timestamps. When the current waveform has
                SampleIntervalMode.IRREGULAR, you must provide a sequence of timestamps with the
                same length as the array.

        Raises:
            TimingMismatchError: The current and other waveforms have incompatible timing.
            TimingMismatchWarning: The sample intervals of the waveform(s) do not match.
            ValueError: The other array has the wrong number of dimensions or the length of the
                timestamps argument does not match the length of the other array.
            TypeError: The data types of the current waveform and other array or waveform(s) do not
                match, or an argument has the wrong data type.

        When appending waveforms:

        * Timing information is merged based on the sample interval mode of the current
          waveform:

          * SampleIntervalMode.NONE or SampleIntervalMode.REGULAR: The other waveform(s) must also
            have SampleIntervalMode.NONE or SampleIntervalMode.REGULAR. If the sample interval does
            not match, a TimingMismatchWarning is generated. Otherwise, the timing information of
            the other waveform(s) is discarded.

          * SampleIntervalMode.IRREGULAR: The other waveforms(s) must also have
            SampleIntervalMode.IRREGULAR. The timestamps of the other waveforms(s) are appended to
            the current waveform's timing information.

        * Extended properties of the other waveform(s) are merged into the current waveform if they
          are not already set in the current waveform.
        """
        if isinstance(other, np.ndarray):
            self._append_array(other, timestamps)
        elif isinstance(other, DigitalWaveform):
            validate_unsupported_arg("timestamps", timestamps)
            self._append_waveform(other)
        elif isinstance(other, Sequence) and all(isinstance(x, DigitalWaveform) for x in other):
            validate_unsupported_arg("timestamps", timestamps)
            self._append_waveforms(other)
        else:
            raise invalid_arg_type("input", "array or waveform(s)", other)

    def _append_array(
        self,
        array: npt.NDArray[TDigitalState],
        timestamps: Sequence[dt.datetime] | Sequence[ht.datetime] | None = None,
    ) -> None:
        if array.dtype != self.dtype:
            raise create_datatype_mismatch_error("input array", array.dtype, "waveform", self.dtype)

        if array.ndim == 1:
            array_signal_count = 1
            array = array.reshape(len(array), 1)
        elif array.ndim == 2:
            array_signal_count = array.shape[1]
        else:
            raise invalid_array_ndim("input array", "one or two-dimensional array", array.ndim)

        if array_signal_count != self.signal_count:
            raise create_signal_count_mismatch_error(
                "input array", array_signal_count, "waveform", self.signal_count
            )

        if timestamps is not None and len(array) != len(timestamps):
            raise create_irregular_timestamp_count_mismatch_error(
                len(timestamps), "input array length", len(array)
            )

        new_timing = self._timing._append_timestamps(timestamps)

        self._increase_capacity(len(array))
        self._set_timing(new_timing)

        offset = self._start_index + self._sample_count
        self._data[offset : offset + len(array)] = array
        self._sample_count += len(array)

    def _append_waveform(self, waveform: DigitalWaveform[TDigitalState]) -> None:
        self._append_waveforms([waveform])

    def _append_waveforms(self, waveforms: Sequence[DigitalWaveform[TDigitalState]]) -> None:
        for waveform in waveforms:
            if waveform.dtype != self.dtype:
                raise create_datatype_mismatch_error(
                    "input waveform", waveform.dtype, "waveform", self.dtype
                )

        new_timing = self._timing
        for waveform in waveforms:
            new_timing = new_timing._append_timing(waveform._timing)

        self._increase_capacity(sum(waveform.sample_count for waveform in waveforms))
        self._set_timing(new_timing)

        offset = self._start_index + self._sample_count
        for waveform in waveforms:
            self._data[offset : offset + waveform.sample_count] = waveform.data
            offset += waveform.sample_count
            self._sample_count += waveform.sample_count
            self._extended_properties._merge(waveform._extended_properties)

    def _increase_capacity(self, amount: int) -> None:
        new_capacity = self._start_index + self._sample_count + amount
        if new_capacity > self.capacity:
            self.capacity = new_capacity

    def load_data(
        self,
        array: npt.NDArray[TDigitalState],
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
    ) -> None:
        """Load new data into an existing waveform.

        Args:
            array: A NumPy array containing the data to load.
            copy: Specifies whether to copy the array or save a reference to it.
            start_index: The sample index at which the waveform data begins.
            sample_count: The number of samples in the waveform.
        """
        if isinstance(array, np.ndarray):
            self._load_array(array, copy=copy, start_index=start_index, sample_count=sample_count)
        else:
            raise invalid_arg_type("input array", "array", array)

    def _load_array(
        self,
        array: npt.NDArray[TDigitalState],
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
        signal_count: SupportsIndex | None = None,
    ) -> None:
        if array.dtype != self.dtype:
            raise create_datatype_mismatch_error("input array", array.dtype, "waveform", self.dtype)

        if array.ndim == 1:
            array_signal_count = 1
            array = array.reshape(len(array), 1)
        elif array.ndim == 2:
            array_signal_count = array.shape[1]
        else:
            raise invalid_array_ndim("input array", "one or two-dimensional array", array.ndim)

        if self._timing._timestamps is not None and len(array) != len(self._timing._timestamps):
            raise create_irregular_timestamp_count_mismatch_error(
                len(self._timing._timestamps), "input array length", len(array), reversed=True
            )

        start_index = arg_to_uint("start index", start_index, 0)
        sample_count = arg_to_uint("sample count", sample_count, len(array) - start_index)
        signal_count = arg_to_uint("signal count", signal_count, array_signal_count)

        if signal_count != array_signal_count:
            raise create_signal_count_mismatch_error(
                "input array", signal_count, "waveform", array_signal_count
            )

        if copy:
            if sample_count > len(self._data):
                self.capacity = sample_count
            self._data[0:sample_count] = array[start_index : start_index + sample_count]
            self._start_index = 0
            self._sample_count = sample_count
        else:
            self._data = array
            self._start_index = start_index
            self._sample_count = sample_count

    def test(
        self,
        expected_waveform: DigitalWaveform[TDigitalState],
        *,
        start_sample: SupportsIndex | None = 0,
        expected_start_sample: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
    ) -> DigitalWaveformTestResult:
        """Test the digital waveform against an expected digital waveform.

        Args:
            expected_waveform: The expected digital waveform to compare against.
            start_sample: The beginning sample of ``self`` to compare.
            expected_start_sample: The beginning sample of ``expected_waveform`` to compare.
            sample_count: The number of samples to compare.

        Returns:
            The test result.
        """
        start_sample = arg_to_uint("start sample", start_sample, 0)
        expected_start_sample = arg_to_uint("expected start sample", expected_start_sample, 0)
        sample_count = arg_to_uint("sample count", sample_count, self.sample_count - start_sample)

        if self.signal_count != expected_waveform.signal_count:
            raise create_signal_count_mismatch_error(
                "expected waveform", expected_waveform.signal_count, "waveform", self.signal_count
            )
        if start_sample + sample_count > self.sample_count:
            raise create_start_index_or_sample_count_too_large_error(
                start_sample, sample_count, "number of samples in the waveform", self.sample_count
            )
        if expected_start_sample + sample_count > expected_waveform.sample_count:
            raise create_start_index_or_sample_count_too_large_error(
                expected_start_sample,
                sample_count,
                "number of samples in the expected waveform",
                expected_waveform.sample_count,
            )

        failures = []
        for _ in range(sample_count):
            for column_index in range(self.signal_count):
                signal_index = self._reverse_index(column_index)
                actual_state = DigitalState(self.data[start_sample, column_index])
                expected_state = DigitalState(
                    expected_waveform.data[expected_start_sample, column_index]
                )
                if DigitalState.test(actual_state, expected_state):
                    failures.append(
                        DigitalWaveformFailure(
                            start_sample,
                            expected_start_sample,
                            signal_index,
                            actual_state,
                            expected_state,
                        )
                    )
            start_sample += 1
            expected_start_sample += 1

        return DigitalWaveformTestResult(failures)

    def _reverse_index(self, index: int) -> int:
        """Convert a signal_index to a column_index, or vice versa."""
        assert 0 <= index < self.signal_count
        return self.signal_count - 1 - index

    def __eq__(self, value: object, /) -> bool:
        """Return self==value."""
        if not isinstance(value, self.__class__):
            return NotImplemented
        return (
            self.dtype == value.dtype
            and np.array_equal(self.data, value.data)
            and self._extended_properties == value._extended_properties
            and self._timing == value._timing
        )

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        ctor_args = (self._sample_count, self.signal_count, self.dtype)
        ctor_kwargs: dict[str, Any] = {
            "data": self.data,
            "extended_properties": self._extended_properties,
            "copy_extended_properties": False,
            "timing": self._timing,
        }
        return (self.__class__._unpickle, (ctor_args, ctor_kwargs))

    @classmethod
    def _unpickle(cls, args: tuple[Any, ...], kwargs: dict[str, Any]) -> Self:
        return cls(*args, **kwargs)

    def __repr__(self) -> str:
        """Return repr(self)."""
        args = [f"{self._sample_count}, {self.signal_count}"]
        if self.dtype != np.uint8:
            args.append(f"{self.dtype.name}")
        # start_index and capacity are not shown because they are allocation details. data hides
        # the unused data before start_index and after start_index+sample_count.
        if self._sample_count > 0:
            # Hack: undo NumPy's line wrapping
            args.append(f"data={self.data!r}".replace("\n      ", ""))
        if self._extended_properties:
            args.append(f"extended_properties={self._extended_properties._properties!r}")
        if self._timing is not Timing.empty:
            args.append(f"timing={self._timing!r}")
        return f"{self.__class__.__module__}.{self.__class__.__name__}({', '.join(args)})"
