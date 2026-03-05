from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any, SupportsIndex, Union, overload

import numpy as np
import numpy.typing as npt
from typing_extensions import TYPE_CHECKING, TypeVar, final, override

from nitypes.complex import ComplexInt32Base, ComplexInt32DType, convert_complex
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

# _TRaw specifies the type of the raw_data array. ComplexWaveform accepts a narrower set of types
# than NumericWaveform. Note that ComplexInt32Base is an alias for np.void, but other structured
# data types are rejected at run time.
_TRaw = TypeVar("_TRaw", bound=Union[np.complexfloating, ComplexInt32Base])
_TOtherRaw = TypeVar("_TOtherRaw", bound=Union[np.complexfloating, ComplexInt32Base])

_RAW_DTYPES = (
    # Complex floating point
    np.csingle,
    np.cdouble,
    # Complex integers
    ComplexInt32DType,
)

_SCALED_DTYPES = (
    # Complex floating point
    np.csingle,
    np.cdouble,
)


@final
class ComplexWaveform(NumericWaveform[_TRaw, np.complex128]):
    """A complex waveform, which encapsulates complex data and timing information.

    Constructing
    ^^^^^^^^^^^^

    To construct a complex waveform, use the :class:`ComplexWaveform` class:

    >>> ComplexWaveform()
    nitypes.waveform.ComplexWaveform(0)
    >>> ComplexWaveform(5)
    nitypes.waveform.ComplexWaveform(5, raw_data=array([0.+0.j, 0.+0.j, 0.+0.j, 0.+0.j, 0.+0.j]))

    To construct a complex waveform from a NumPy array, use the :any:`ComplexWaveform.from_array_1d`
    method.

    >>> import numpy as np
    >>> ComplexWaveform.from_array_1d(np.array([1+2j, 3+4j, 5+6j]))
    nitypes.waveform.ComplexWaveform(3, raw_data=array([1.+2.j, 3.+4.j, 5.+6.j]))

    You can also use :any:`ComplexWaveform.from_array_1d` to construct a complex waveform from a
    sequence, such as a list. In this case, you must specify the NumPy data type.

    >>> ComplexWaveform.from_array_1d([1+2j, 3+4j, 5+6j], np.complex128)
    nitypes.waveform.ComplexWaveform(3, raw_data=array([1.+2.j, 3.+4.j, 5.+6.j]))

    The 2D version, :any:`ComplexWaveform.from_array_2d`, returns multiple waveforms, one for each row of
    data in the array or nested sequence.

    >>> nested_list = [[1+2j, 3+4j, 5+6j], [7+8j, 9+10j, 11+12j]]
    >>> ComplexWaveform.from_array_2d(nested_list, np.complex128)  # doctest: +NORMALIZE_WHITESPACE
    [nitypes.waveform.ComplexWaveform(3, raw_data=array([1.+2.j, 3.+4.j, 5.+6.j])),
    nitypes.waveform.ComplexWaveform(3, raw_data=array([ 7. +8.j,  9.+10.j, 11.+12.j]))]

    Scaling complex-number data
    ^^^^^^^^^^^^^^^^^^^^^^^^^^^

    Complex waveforms support scaling raw integer data to floating-point. Python and NumPy do not have
    native support for complex integers, so this uses the :any:`ComplexInt32DType` structured data type.

    >>> from nitypes.complex import ComplexInt32DType
    >>> scale_mode = LinearScaleMode(gain=2.0, offset=0.5)
    >>> wfm = ComplexWaveform.from_array_1d([(1, 2), (3, 4)], ComplexInt32DType, scale_mode=scale_mode)
    >>> wfm  # doctest: +NORMALIZE_WHITESPACE
    nitypes.waveform.ComplexWaveform(2, void32, raw_data=array([(1, 2), (3, 4)],
        dtype=[('real', '<i2'), ('imag', '<i2')]),
        scale_mode=nitypes.waveform.LinearScaleMode(2.0, 0.5))
    >>> wfm.raw_data
    array([(1, 2), (3, 4)], dtype=[('real', '<i2'), ('imag', '<i2')])
    >>> wfm.scaled_data
    array([2.5+4.j, 6.5+8.j])

    Timing information
    ^^^^^^^^^^^^^^^^^^

    Complex waveforms have the same timing information as analog waveforms. For more details, see
    :class:`AnalogWaveform`.

    Class members
    ^^^^^^^^^^^^^
    """  # noqa: W505 - doc line too long

    @override
    @staticmethod
    def _get_default_raw_dtype() -> type[np.generic] | np.dtype[np.generic]:
        return np.complex128

    @override
    @staticmethod
    def _get_default_scaled_dtype() -> type[np.generic] | np.dtype[np.generic]:
        return np.complex128

    @override
    @staticmethod
    def _get_supported_raw_dtypes() -> tuple[npt.DTypeLike, ...]:
        return _RAW_DTYPES

    @override
    @staticmethod
    def _get_supported_scaled_dtypes() -> tuple[npt.DTypeLike, ...]:
        return _SCALED_DTYPES

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
    ) -> ComplexWaveform[_TOtherRaw]: ...

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
    ) -> ComplexWaveform[_TOtherRaw]: ...

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
    ) -> ComplexWaveform[Any]: ...

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
    ) -> ComplexWaveform[Any]:
        """Construct a complex waveform from a one-dimensional array or sequence.

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
            A complex waveform containing the specified data.
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
    ) -> Sequence[ComplexWaveform[_TOtherRaw]]: ...

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
    ) -> Sequence[ComplexWaveform[_TOtherRaw]]: ...

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
    ) -> Sequence[ComplexWaveform[Any]]: ...

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
    ) -> Sequence[ComplexWaveform[Any]]:
        """Construct multiple complex waveforms from a two-dimensional array or nested sequence.

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
            A sequence containing a complex waveform for each row of the specified data.

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

    # If neither dtype nor raw_data is specified, _TRaw defaults to np.complex128.
    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: ComplexWaveform[np.complex128],
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
        self: ComplexWaveform[_TOtherRaw],
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
        self: ComplexWaveform[_TOtherRaw],
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
        self: ComplexWaveform[Any],
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
        """Initialize a new complex waveform.

        Args:
            sample_count: The number of samples in the waveform.
            dtype: The NumPy data type for the waveform data. If not specified, the data
                type defaults to np.complex128.
            raw_data: A NumPy ndarray to use for sample storage. The waveform takes ownership
                of this array. If not specified, an ndarray is created based on the specified dtype,
                start index, sample count, and capacity.
            start_index: The sample index at which the waveform data begins.
            capacity: The number of samples to allocate. Pre-allocating a larger buffer optimizes
                appending samples to the waveform.
            extended_properties: The extended properties of the waveform.
            copy_extended_properties: Specifies whether to copy the extended properties or take
                ownership.
            timing: The timing information of the waveform.
            scale_mode: The scale mode of the waveform.

        Returns:
            A complex waveform.
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
        return convert_complex(dtype, raw_data)
