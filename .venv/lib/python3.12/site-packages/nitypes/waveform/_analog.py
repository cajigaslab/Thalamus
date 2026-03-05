from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any, SupportsIndex, Union, overload

import numpy as np
import numpy.typing as npt
from typing_extensions import TYPE_CHECKING, TypeVar, final, override

from nitypes._numpy import long as _np_long, ulong as _np_ulong
from nitypes.time.typing import AnyDateTime, AnyTimeDelta
from nitypes.waveform._numeric import _TOtherScaled
from nitypes.waveform.typing import ExtendedPropertyValue

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import NumericWaveform, ScaleMode, Timing
else:
    from nitypes.waveform._numeric import NumericWaveform
    from nitypes.waveform._scaling import ScaleMode
    from nitypes.waveform._timing import Timing


# _TRaw specifies the type of the raw_data array. AnalogWaveform accepts a narrower set of types
# than NumericWaveform.
_TRaw = TypeVar("_TRaw", bound=Union[np.floating, np.integer])
_TOtherRaw = TypeVar("_TOtherRaw", bound=Union[np.floating, np.integer])

# Use the C types here because np.isdtype() considers some of them to be distinct types, even when
# they have the same size (e.g. np.intc vs. np.int_).
_RAW_DTYPES = (
    # Floating point
    np.single,
    np.double,
    # Signed integers
    np.byte,
    np.short,
    np.intc,
    np.int_,
    _np_long,
    np.longlong,
    # Unsigned integers
    np.ubyte,
    np.ushort,
    np.uintc,
    np.uint,
    _np_ulong,
    np.ulonglong,
)

_SCALED_DTYPES = (
    # Floating point
    np.single,
    np.double,
)


@final
class AnalogWaveform(NumericWaveform[_TRaw, np.float64]):
    """An analog waveform, which encapsulates analog data and timing information.

    Constructing
    ^^^^^^^^^^^^

    To construct an analog waveform, use the :class:`AnalogWaveform` class:

    >>> AnalogWaveform()
    nitypes.waveform.AnalogWaveform(0)
    >>> AnalogWaveform(5)
    nitypes.waveform.AnalogWaveform(5, raw_data=array([0., 0., 0., 0., 0.]))

    To construct an analog waveform from a NumPy array, use the :any:`AnalogWaveform.from_array_1d`
    method.

    >>> import numpy as np
    >>> AnalogWaveform.from_array_1d(np.array([1.0, 2.0, 3.0]))
    nitypes.waveform.AnalogWaveform(3, raw_data=array([1., 2., 3.]))

    You can also use :any:`AnalogWaveform.from_array_1d` to construct an analog waveform from a
    sequence, such as a list. In this case, you must specify the NumPy data type.

    >>> AnalogWaveform.from_array_1d([1.0, 2.0, 3.0], np.float64)
    nitypes.waveform.AnalogWaveform(3, raw_data=array([1., 2., 3.]))

    The 2D version, :any:`AnalogWaveform.from_array_2d`, returns multiple waveforms, one for each row of
    data in the array or nested sequence.

    >>> nested_list = [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]
    >>> AnalogWaveform.from_array_2d(nested_list, np.float64)  # doctest: +NORMALIZE_WHITESPACE
    [nitypes.waveform.AnalogWaveform(3, raw_data=array([1., 2., 3.])),
    nitypes.waveform.AnalogWaveform(3, raw_data=array([4., 5., 6.]))]

    Timing information
    ^^^^^^^^^^^^^^^^^^

    Analog waveforms include timing information, such as the start time and sample interval, to support
    analyzing and visualizing the data.

    You can specify timing information by constructing a :class:`Timing` object and passing it to the
    waveform constructor or factory method:

    >>> import datetime as dt
    >>> wfm = AnalogWaveform(timing=Timing.create_with_regular_interval(
    ...     dt.timedelta(seconds=1e-3), dt.datetime(2024, 12, 31, 23, 59, 59, tzinfo=dt.timezone.utc)
    ... ))
    >>> wfm.timing  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.Timing(nitypes.waveform.SampleIntervalMode.REGULAR,
        timestamp=datetime.datetime(2024, 12, 31, 23, 59, 59, tzinfo=datetime.timezone.utc),
        sample_interval=datetime.timedelta(microseconds=1000))

    You can query the waveform's timing information using the :class:`Timing` object's properties:

    >>> wfm.timing.start_time
    datetime.datetime(2024, 12, 31, 23, 59, 59, tzinfo=datetime.timezone.utc)
    >>> wfm.timing.sample_interval
    datetime.timedelta(microseconds=1000)

    Timing objects are immutable, so you cannot directly set their properties:

    >>> wfm.timing.sample_interval = dt.timedelta(seconds=10e-3)  # doctest: +ELLIPSIS
    Traceback (most recent call last):
    ...
    AttributeError: ...

    Instead, if you want to modify the timing information for an existing waveform, you can create a new
    timing object and set the :any:`NumericWaveform.timing` property:

    >>> wfm.timing = Timing.create_with_regular_interval(
    ...     dt.timedelta(seconds=1e-3), dt.datetime(2025, 1, 1, tzinfo=dt.timezone.utc)
    ... )
    >>> wfm.timing  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.Timing(nitypes.waveform.SampleIntervalMode.REGULAR,
        timestamp=datetime.datetime(2025, 1, 1, 0, 0, tzinfo=datetime.timezone.utc),
        sample_interval=datetime.timedelta(microseconds=1000))

    Timing objects support time types from the :class:`DateTime`, :any:`hightime`, and
    :any:`nitypes.bintime` modules. If you need the timing information in a specific representation, use
    the conversion methods:

    >>> wfm.timing.to_datetime()  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.Timing(nitypes.waveform.SampleIntervalMode.REGULAR,
        timestamp=datetime.datetime(2025, 1, 1, 0, 0, tzinfo=datetime.timezone.utc),
        sample_interval=datetime.timedelta(microseconds=1000))
    >>> wfm.timing.to_hightime()  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.Timing(nitypes.waveform.SampleIntervalMode.REGULAR,
        timestamp=hightime.datetime(2025, 1, 1, 0, 0, tzinfo=datetime.timezone.utc),
        sample_interval=hightime.timedelta(microseconds=1000))
    >>> wfm.timing.to_bintime()  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.Timing(nitypes.waveform.SampleIntervalMode.REGULAR,
        timestamp=nitypes.bintime.DateTime(2025, 1, 1, 0, 0, tzinfo=datetime.timezone.utc),
        sample_interval=nitypes.bintime.TimeDelta(Decimal('0.000999999999999999966606573')))

    If :any:`NumericWaveform.timing` is not specified for a given waveform, it defaults to the
    :any:`Timing.empty` singleton object.

    >>> AnalogWaveform().timing
    nitypes.waveform.Timing(nitypes.waveform.SampleIntervalMode.NONE)
    >>> AnalogWaveform().timing is Timing.empty
    True

    Accessing unspecified properties of the timing object raises an exception:

    >>> Timing.empty.sample_interval
    Traceback (most recent call last):
    ...
    RuntimeError: The waveform timing does not have a sample interval.

    You can use :any:`Timing.sample_interval_mode` and ``has_*`` properties such as
    :any:`Timing.has_timestamp` to query which properties of the timing object were specified:

    >>> wfm.timing.sample_interval_mode
    <SampleIntervalMode.REGULAR: 1>
    >>> (wfm.timing.has_timestamp, wfm.timing.has_sample_interval)
    (True, True)
    >>> Timing.empty.sample_interval_mode
    <SampleIntervalMode.NONE: 0>
    >>> (Timing.empty.has_timestamp, Timing.empty.has_sample_interval)
    (False, False)

    Scaling analog data
    ^^^^^^^^^^^^^^^^^^^

    By default, analog waveforms contain floating point data in :any:`numpy.float64` format, but they
    can also be used to scale raw integer data to floating-point:

    >>> import numpy as np
    >>> scale_mode = LinearScaleMode(gain=2.0, offset=0.5)
    >>> wfm = AnalogWaveform.from_array_1d([1, 2, 3], np.int32, scale_mode=scale_mode)
    >>> wfm  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.AnalogWaveform(3, int32, raw_data=array([1, 2, 3], dtype=int32),
        scale_mode=nitypes.waveform.LinearScaleMode(2.0, 0.5))
    >>> wfm.raw_data
    array([1, 2, 3], dtype=int32)
    >>> wfm.scaled_data
    array([2.5, 4.5, 6.5])

    Class members
    ^^^^^^^^^^^^^
    """  # noqa: W505 - doc line too long

    @override
    @staticmethod
    def _get_default_raw_dtype() -> type[np.generic] | np.dtype[np.generic]:
        return np.float64

    @override
    @staticmethod
    def _get_default_scaled_dtype() -> type[np.generic] | np.dtype[np.generic]:
        return np.float64

    @override
    @staticmethod
    def _get_supported_raw_dtypes() -> tuple[npt.DTypeLike, ...]:
        return _RAW_DTYPES

    @override
    @staticmethod
    def _get_supported_scaled_dtypes() -> tuple[npt.DTypeLike, ...]:
        return _SCALED_DTYPES

    # Override from_array_1d, from_array_2d, and __init__ in order to use overloads to control type
    # inference.
    @overload
    @classmethod
    def from_array_1d(
        cls,
        array: npt.NDArray[_TOtherRaw],
        dtype: None = ...,
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> AnalogWaveform[_TOtherRaw]: ...

    @overload
    @classmethod
    def from_array_1d(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        dtype: type[_TOtherRaw] | np.dtype[_TOtherRaw],
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> AnalogWaveform[_TOtherRaw]: ...

    @overload
    @classmethod
    def from_array_1d(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        dtype: npt.DTypeLike = ...,
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> AnalogWaveform[Any]: ...

    @override
    @classmethod
    def from_array_1d(  # pyright: ignore[reportIncompatibleMethodOverride]
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        dtype: npt.DTypeLike = None,
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = None,
        scale_mode: ScaleMode | None = None,
    ) -> AnalogWaveform[Any]:
        """Construct an analog waveform from a one-dimensional array or sequence.

        Args:
            array: The waveform data as a one-dimensional array or a sequence.
            dtype: The NumPy data type for the waveform data. This argument is required
                when array is a sequence.
            copy: Specifies whether to copy the array or save a reference to it.
            start_index: The sample index at which the waveform data begins.
            sample_count: The number of samples in the waveform.
            extended_properties: The extended properties of the waveform.
            timing: The timing information of the waveform.
            scale_mode: The scale mode of the waveform.

        Returns:
            An analog waveform containing the specified data.
        """
        return super().from_array_1d(
            array,
            dtype,
            copy=copy,
            start_index=start_index,
            sample_count=sample_count,
            extended_properties=extended_properties,
            timing=timing,
            scale_mode=scale_mode,
        )

    @overload
    @classmethod
    def from_array_2d(
        cls,
        array: npt.NDArray[_TOtherRaw],
        dtype: None = ...,
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> Sequence[AnalogWaveform[_TOtherRaw]]: ...

    @overload
    @classmethod
    def from_array_2d(
        cls,
        array: npt.NDArray[Any] | Sequence[Sequence[Any]],
        dtype: type[_TOtherRaw] | np.dtype[_TOtherRaw],
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> Sequence[AnalogWaveform[_TOtherRaw]]: ...

    @overload
    @classmethod
    def from_array_2d(
        cls,
        array: npt.NDArray[Any] | Sequence[Sequence[Any]],
        dtype: npt.DTypeLike = ...,
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> Sequence[AnalogWaveform[Any]]: ...

    @override
    @classmethod
    def from_array_2d(  # pyright: ignore[reportIncompatibleMethodOverride]
        cls,
        array: npt.NDArray[Any] | Sequence[Sequence[Any]],
        dtype: npt.DTypeLike = None,
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = None,
        scale_mode: ScaleMode | None = None,
    ) -> Sequence[AnalogWaveform[Any]]:
        """Construct multiple analog waveforms from a two-dimensional array or nested sequence.

        Args:
            array: The waveform data as a two-dimensional array or a nested sequence.
            dtype: The NumPy data type for the waveform data. This argument is required
                when array is a sequence.
            copy: Specifies whether to copy the array or save a reference to it.
            start_index: The sample index at which the waveform data begins.
            sample_count: The number of samples in the waveform.
            extended_properties: The extended properties of the waveform.
            timing: The timing information of the waveform.
            scale_mode: The scale mode of the waveform.

        Returns:
            A sequence containing an analog waveform for each row of the specified data.

        When constructing multiple waveforms, the same extended properties, timing
        information, and scale mode are applied to all waveforms. Consider assigning
        these properties after construction.
        """
        return super().from_array_2d(
            array,
            dtype,
            copy=copy,
            start_index=start_index,
            sample_count=sample_count,
            extended_properties=extended_properties,
            timing=timing,
            scale_mode=scale_mode,
        )

    __slots__ = ()

    # If neither dtype nor raw_data is specified, _TRaw defaults to np.float64.
    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: AnalogWaveform[np.float64],
        sample_count: SupportsIndex | None = ...,
        dtype: None = ...,
        *,
        raw_data: None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: AnalogWaveform[_TOtherRaw],
        sample_count: SupportsIndex | None = ...,
        dtype: type[_TOtherRaw] | np.dtype[_TOtherRaw] = ...,
        *,
        raw_data: None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: AnalogWaveform[_TOtherRaw],
        sample_count: SupportsIndex | None = ...,
        dtype: None = ...,
        *,
        raw_data: npt.NDArray[_TOtherRaw] = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: AnalogWaveform[Any],
        sample_count: SupportsIndex | None = ...,
        dtype: npt.DTypeLike = ...,
        *,
        raw_data: npt.NDArray[Any] | None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = ...,
        scale_mode: ScaleMode | None = ...,
    ) -> None: ...

    def __init__(
        self,
        sample_count: SupportsIndex | None = None,
        dtype: npt.DTypeLike = None,
        *,
        raw_data: npt.NDArray[Any] | None = None,
        start_index: SupportsIndex | None = None,
        capacity: SupportsIndex | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        copy_extended_properties: bool = True,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = None,
        scale_mode: ScaleMode | None = None,
    ) -> None:
        """Initialize a new analog waveform.

        Args:
            sample_count: The number of samples in the analog waveform.
            dtype: The NumPy data type for the analog waveform data. If not specified, the data
                type defaults to np.float64.
            raw_data: A NumPy ndarray to use for sample storage. The analog waveform takes ownership
                of this array. If not specified, an ndarray is created based on the specified dtype,
                start index, sample count, and capacity.
            start_index: The sample index at which the analog waveform data begins.
            capacity: The number of samples to allocate. Pre-allocating a larger buffer optimizes
                appending samples to the waveform.
            extended_properties: The extended properties of the analog waveform.
            copy_extended_properties: Specifies whether to copy the extended properties or take
                ownership.
            timing: The timing information of the analog waveform.
            scale_mode: The scale mode of the analog waveform.

        Returns:
            An analog waveform.
        """
        return super().__init__(
            sample_count,
            dtype,
            raw_data=raw_data,
            start_index=start_index,
            capacity=capacity,
            extended_properties=extended_properties,
            copy_extended_properties=copy_extended_properties,
            timing=timing,
            scale_mode=scale_mode,
        )

    @override
    def _convert_data(
        self,
        dtype: npt.DTypeLike | type[_TOtherScaled] | np.dtype[_TOtherScaled],
        raw_data: npt.NDArray[_TRaw],
    ) -> npt.NDArray[_TOtherScaled]:
        return raw_data.astype(dtype)
