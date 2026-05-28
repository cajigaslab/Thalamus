from __future__ import annotations

import datetime as dt
import sys
import warnings
from abc import ABC, abstractmethod
from collections.abc import Mapping, Sequence
from typing import TYPE_CHECKING, Any, Generic, SupportsIndex, overload

import hightime as ht
import numpy as np
import numpy.typing as npt
from typing_extensions import Self, TypeVar

from nitypes._arguments import arg_to_uint, validate_dtype, validate_unsupported_arg
from nitypes._exceptions import invalid_arg_type, invalid_array_ndim
from nitypes._numpy import asarray as _np_asarray
from nitypes.time.typing import AnyDateTime, AnyTimeDelta
from nitypes.waveform._exceptions import (
    create_capacity_mismatch_error,
    create_capacity_too_small_error,
    create_datatype_mismatch_error,
    create_irregular_timestamp_count_mismatch_error,
    create_start_index_or_sample_count_too_large_error,
    create_start_index_too_large_error,
)
from nitypes.waveform._extended_properties import CHANNEL_NAME, UNIT_DESCRIPTION
from nitypes.waveform._warnings import scale_mode_mismatch
from nitypes.waveform.typing import ExtendedPropertyValue

if sys.version_info < (3, 10):
    import array as std_array

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import (
        NO_SCALING,
        ExtendedPropertyDictionary,
        ScaleMode,
        Timing,
    )
else:
    from nitypes.waveform._extended_properties import ExtendedPropertyDictionary
    from nitypes.waveform._scaling import NO_SCALING, ScaleMode
    from nitypes.waveform._timing import Timing

# _TRaw specifies the type of the raw_data array. It is not limited to supported types. Requesting
# an unsupported type raises TypeError at run time. It is invariant because waveforms are mutable.
_TRaw = TypeVar("_TRaw", bound=np.generic)

# _TScaled specifies the type of the scaled_data property.
_TScaled = TypeVar("_TScaled", bound=np.generic)
_TOtherScaled = TypeVar("_TOtherScaled", bound=np.generic)


# Note about NumPy type hints:
# - At time of writing (April 2025), shape typing is still under development, so we do not
#   distinguish between 1D and 2D arrays in type hints.
# - npt.ArrayLike accepts some types that np.asarray() does not, such as buffers, so we are
#   explicitly using npt.NDArray | Sequence instead of npt.ArrayLike.
# - _TRaw is bound to np.generic, so Sequence[_TRaw] will not match list[int].
# - We are not using PEP 696 – Type Defaults for Type Parameters for type variables on functions
#   because it makes the type parameter default to np.float64 in some cases where it should be
#   inferred as Any, such as when dtype is specified as a str. PEP 696 seems more appropriate for
#   type variables on classes.


class NumericWaveform(ABC, Generic[_TRaw, _TScaled]):
    """A numeric waveform, which encapsulates numeric data and timing information.

    This is an abstract base class. To create a numeric waveform, use :class:`AnalogWaveform` or
    :class:`ComplexWaveform`.
    """

    @staticmethod
    @abstractmethod
    def _get_default_raw_dtype() -> type[np.generic] | np.dtype[np.generic]:
        raise NotImplementedError

    @staticmethod
    @abstractmethod
    def _get_default_scaled_dtype() -> type[np.generic] | np.dtype[np.generic]:
        raise NotImplementedError

    @staticmethod
    @abstractmethod
    def _get_supported_raw_dtypes() -> tuple[npt.DTypeLike, ...]:
        raise NotImplementedError

    @staticmethod
    @abstractmethod
    def _get_supported_scaled_dtypes() -> tuple[npt.DTypeLike, ...]:
        raise NotImplementedError

    @classmethod
    def from_array_1d(
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
    ) -> Self:
        """Construct a waveform from a one-dimensional array or sequence.

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
            A waveform containing the specified data.
        """
        if isinstance(array, np.ndarray):
            if array.ndim != 1:
                raise invalid_array_ndim(
                    "input array", "one-dimensional array or sequence", array.ndim
                )
        elif isinstance(array, Sequence) or (
            sys.version_info < (3, 10) and isinstance(array, std_array.array)
        ):
            if dtype is None:
                raise ValueError("You must specify a dtype when the input array is a sequence.")
        else:
            raise invalid_arg_type("input array", "one-dimensional array or sequence", array)

        return cls(
            raw_data=_np_asarray(array, dtype, copy=copy),
            start_index=start_index,
            sample_count=sample_count,
            extended_properties=extended_properties,
            timing=timing,
            scale_mode=scale_mode,
        )

    @classmethod
    def from_array_2d(
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
    ) -> Sequence[Self]:
        """Construct multiple waveforms from a two-dimensional array or nested sequence.

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
            A sequence containing a waveform for each row of the specified data.

        When constructing multiple waveforms, the same extended properties, timing
        information, and scale mode are applied to all waveforms. Consider assigning
        these properties after construction.
        """
        if isinstance(array, np.ndarray):
            if array.ndim != 2:
                raise invalid_array_ndim(
                    "input array", "two-dimensional array or nested sequence", array.ndim
                )
        elif isinstance(array, Sequence) or (
            sys.version_info < (3, 10) and isinstance(array, std_array.array)
        ):
            if dtype is None:
                raise ValueError("You must specify a dtype when the input array is a sequence.")
        else:
            raise invalid_arg_type("input array", "two-dimensional array or nested sequence", array)

        return [
            cls(
                raw_data=_np_asarray(array[i], dtype, copy=copy),
                start_index=start_index,
                sample_count=sample_count,
                extended_properties=extended_properties,
                timing=timing,
                scale_mode=scale_mode,
            )
            for i in range(len(array))
        ]

    __slots__ = [
        "_data",
        "_start_index",
        "_sample_count",
        "_extended_properties",
        "_timing",
        "_scale_mode",
        "__weakref__",
    ]

    _data: npt.NDArray[_TRaw]
    _start_index: int
    _sample_count: int
    _extended_properties: ExtendedPropertyDictionary
    _timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta]
    _scale_mode: ScaleMode

    def __init__(
        self,
        sample_count: SupportsIndex | None = None,
        dtype: npt.DTypeLike = None,
        *,
        raw_data: npt.NDArray[_TRaw] | None = None,
        start_index: SupportsIndex | None = None,
        capacity: SupportsIndex | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        copy_extended_properties: bool = True,
        timing: Timing[AnyDateTime, AnyTimeDelta, AnyTimeDelta] | None = None,
        scale_mode: ScaleMode | None = None,
    ) -> None:
        """Initialize a new numeric waveform.

        Args:
            sample_count: The number of samples in the waveform.
            dtype: The NumPy data type for the waveform data.
            raw_data: A NumPy ndarray to use for sample storage. The waveform takes ownership
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
            scale_mode: The scale mode of the waveform.

        Returns:
            A numeric waveform.
        """
        if raw_data is None:
            self._init_with_new_array(
                sample_count, dtype, start_index=start_index, capacity=capacity
            )
        elif isinstance(raw_data, np.ndarray):
            self._init_with_provided_array(
                raw_data,
                dtype,
                start_index=start_index,
                sample_count=sample_count,
                capacity=capacity,
            )
        else:
            raise invalid_arg_type("raw data", "NumPy ndarray", raw_data)

        if copy_extended_properties or not isinstance(
            extended_properties, ExtendedPropertyDictionary
        ):
            extended_properties = ExtendedPropertyDictionary(extended_properties)
        self._extended_properties = extended_properties

        if timing is None:
            timing = Timing.empty
        self._timing = timing

        if scale_mode is None:
            scale_mode = NO_SCALING
        self._scale_mode = scale_mode

    def _init_with_new_array(
        self,
        sample_count: SupportsIndex | None = None,
        dtype: npt.DTypeLike = None,
        *,
        start_index: SupportsIndex | None = None,
        capacity: SupportsIndex | None = None,
    ) -> None:
        start_index = arg_to_uint("start index", start_index, 0)
        sample_count = arg_to_uint("sample count", sample_count, 0)
        capacity = arg_to_uint("capacity", capacity, sample_count)

        if dtype is None:
            dtype = self.__class__._get_default_raw_dtype()
        validate_dtype(dtype, self.__class__._get_supported_raw_dtypes())

        if start_index > capacity:
            raise create_start_index_too_large_error(start_index, "capacity", capacity)
        if start_index + sample_count > capacity:
            raise create_start_index_or_sample_count_too_large_error(
                start_index, sample_count, "capacity", capacity
            )

        self._data = np.zeros(capacity, dtype)
        self._start_index = start_index
        self._sample_count = sample_count

    def _init_with_provided_array(
        self,
        data: npt.NDArray[_TRaw],
        dtype: npt.DTypeLike = None,
        *,
        start_index: SupportsIndex | None = None,
        sample_count: SupportsIndex | None = None,
        capacity: SupportsIndex | None = None,
    ) -> None:
        if not isinstance(data, np.ndarray):
            raise invalid_arg_type("input array", "one-dimensional array", data)
        if data.ndim != 1:
            raise invalid_array_ndim("input array", "one-dimensional array", data.ndim)

        if dtype is None:
            dtype = data.dtype
        if dtype != data.dtype:
            raise create_datatype_mismatch_error(
                "input array", data.dtype, "requested", np.dtype(dtype)
            )
        validate_dtype(dtype, self.__class__._get_supported_raw_dtypes())

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

        self._data = data
        self._start_index = start_index
        self._sample_count = sample_count

    @property
    def raw_data(self) -> npt.NDArray[_TRaw]:
        """The raw waveform data."""
        return self._data[self._start_index : self._start_index + self._sample_count]

    def get_raw_data(
        self, start_index: SupportsIndex | None = 0, sample_count: SupportsIndex | None = None
    ) -> npt.NDArray[_TRaw]:
        """Get a subset of the raw waveform data.

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

        return self.raw_data[start_index : start_index + sample_count]

    @property
    def scaled_data(self) -> npt.NDArray[_TScaled]:
        """The scaled waveform data.

        This property converts all of the waveform samples from the raw data type to the scaled
        data type and scales them using :attr:`scale_mode`. To scale a subset of the waveform or
        scale to single-precision floating point, use the :meth:`get_scaled_data` method
        instead.
        """
        return self.get_scaled_data()

    # If dtype is not specified, the dtype defaults to _TScaled.
    @overload
    def get_scaled_data(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self,
        dtype: None = ...,
        *,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
    ) -> npt.NDArray[_TScaled]: ...

    @overload
    def get_scaled_data(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self,
        dtype: type[_TOtherScaled] | np.dtype[_TOtherScaled],
        *,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
    ) -> npt.NDArray[_TOtherScaled]: ...

    @overload
    def get_scaled_data(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self,
        dtype: npt.DTypeLike = ...,
        *,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
    ) -> npt.NDArray[Any]: ...

    def get_scaled_data(
        self,
        dtype: npt.DTypeLike = None,
        *,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
    ) -> npt.NDArray[Any]:
        """Get a subset of the scaled waveform data with the specified dtype.

        Args:
            dtype: The NumPy data type to use for scaled data.
            start_index: The sample index at which to start scaling.
            sample_count: The number of samples to scale.

        Returns:
            A subset of the scaled waveform data.
        """
        if dtype is None:
            dtype = self.__class__._get_default_scaled_dtype()
        validate_dtype(dtype, self.__class__._get_supported_scaled_dtypes())

        raw_data = self.get_raw_data(start_index, sample_count)
        converted_data: npt.NDArray[Any] = self._convert_data(dtype, raw_data)
        return self._scale_mode._transform_data(converted_data)

    @abstractmethod
    def _convert_data(
        self,
        dtype: npt.DTypeLike | type[_TOtherScaled] | np.dtype[_TOtherScaled],
        raw_data: npt.NDArray[_TRaw],
    ) -> npt.NDArray[_TOtherScaled]:
        raise NotImplementedError

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
            self._data.resize(value, refcheck=False)

    @property
    def dtype(self) -> np.dtype[_TRaw]:
        """The NumPy dtype for the waveform data."""
        return self._data.dtype

    @property
    def extended_properties(self) -> ExtendedPropertyDictionary:
        """The extended properties for the waveform.

        .. note::
            Data stored in the extended properties dictionary may not be encrypted when you send it
            over the network or write it to a TDMS file.
        """
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

    @property
    def units(self) -> str:
        """The unit of measurement, such as volts, of the waveform."""
        value = self._extended_properties.get(UNIT_DESCRIPTION, "")
        assert isinstance(value, str)
        return value

    @units.setter
    def units(self, value: str) -> None:
        if not isinstance(value, str):
            raise invalid_arg_type("units", "str", value)
        self._extended_properties[UNIT_DESCRIPTION] = value

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

    @property
    def scale_mode(self) -> ScaleMode:
        """The scale mode of the waveform."""
        return self._scale_mode

    @scale_mode.setter
    def scale_mode(self, value: ScaleMode) -> None:
        if not isinstance(value, ScaleMode):
            raise invalid_arg_type("scale mode", "ScaleMode object", value)
        self._scale_mode = value

    def append(
        self,
        other: (
            npt.NDArray[_TRaw]
            | NumericWaveform[_TRaw, _TScaled]
            | Sequence[NumericWaveform[_TRaw, _TScaled]]
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
            ScalingMismatchWarning: The scale modes of the waveform(s) do not match.
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

        * If the scale mode of other waveform(s) does not match the scale mode of the current
          waveform, a ScalingMismatchWarning is generated. Otherwise, the scaling information of the
          other waveform(s) is discarded.
        """
        if isinstance(other, np.ndarray):
            self._append_array(other, timestamps)
        elif isinstance(other, NumericWaveform):
            validate_unsupported_arg("timestamps", timestamps)
            self._append_waveform(other)
        elif isinstance(other, Sequence) and all(isinstance(x, NumericWaveform) for x in other):
            validate_unsupported_arg("timestamps", timestamps)
            self._append_waveforms(other)
        else:
            raise invalid_arg_type("input", "array or waveform(s)", other)

    def _append_array(
        self,
        array: npt.NDArray[_TRaw],
        timestamps: Sequence[dt.datetime] | Sequence[ht.datetime] | None = None,
    ) -> None:
        if array.dtype != self.dtype:
            raise create_datatype_mismatch_error("input array", array.dtype, "waveform", self.dtype)
        if array.ndim != 1:
            raise invalid_array_ndim("input array", "one-dimensional array", array.ndim)
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

    def _append_waveform(self, waveform: NumericWaveform[_TRaw, _TScaled]) -> None:
        self._append_waveforms([waveform])

    def _append_waveforms(self, waveforms: Sequence[NumericWaveform[_TRaw, _TScaled]]) -> None:
        for waveform in waveforms:
            if waveform.dtype != self.dtype:
                raise create_datatype_mismatch_error(
                    "input waveform", waveform.dtype, "waveform", self.dtype
                )
            if waveform._scale_mode != self._scale_mode:
                warnings.warn(scale_mode_mismatch())

        new_timing = self._timing
        for waveform in waveforms:
            new_timing = new_timing._append_timing(waveform._timing)

        self._increase_capacity(sum(waveform.sample_count for waveform in waveforms))
        self._set_timing(new_timing)

        offset = self._start_index + self._sample_count
        for waveform in waveforms:
            self._data[offset : offset + waveform.sample_count] = waveform.raw_data
            offset += waveform.sample_count
            self._sample_count += waveform.sample_count
            self._extended_properties._merge(waveform._extended_properties)

    def _increase_capacity(self, amount: int) -> None:
        new_capacity = self._start_index + self._sample_count + amount
        if new_capacity > self.capacity:
            self.capacity = new_capacity

    def load_data(
        self,
        array: npt.NDArray[_TRaw],
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
        array: npt.NDArray[_TRaw],
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
    ) -> None:
        if array.dtype != self.dtype:
            raise create_datatype_mismatch_error("input array", array.dtype, "waveform", self.dtype)
        if array.ndim != 1:
            raise invalid_array_ndim("input array", "one-dimensional array", array.ndim)
        if self._timing._timestamps is not None and len(array) != len(self._timing._timestamps):
            raise create_irregular_timestamp_count_mismatch_error(
                len(self._timing._timestamps), "input array length", len(array), reversed=True
            )

        start_index = arg_to_uint("start index", start_index, 0)
        sample_count = arg_to_uint("sample count", sample_count, len(array) - start_index)

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

    def __eq__(self, value: object, /) -> bool:
        """Return self==value."""
        if not isinstance(value, self.__class__):
            return NotImplemented
        return (
            self.dtype == value.dtype
            and np.array_equal(self.raw_data, value.raw_data)
            and self._extended_properties == value._extended_properties
            and self._timing == value._timing
            and self._scale_mode == value._scale_mode
        )

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        ctor_args = (self._sample_count, self.dtype)
        ctor_kwargs: dict[str, Any] = {
            "raw_data": self.raw_data,
            "extended_properties": self._extended_properties,
            "copy_extended_properties": False,
            "timing": self._timing,
            "scale_mode": self._scale_mode,
        }
        return (self.__class__._unpickle, (ctor_args, ctor_kwargs))

    @classmethod
    def _unpickle(cls, args: tuple[Any, ...], kwargs: dict[str, Any]) -> Self:
        return cls(*args, **kwargs)

    def __repr__(self) -> str:
        """Return repr(self)."""
        args = [f"{self._sample_count}"]
        if self.dtype != self.__class__._get_default_raw_dtype():
            args.append(f"{self.dtype.name}")
        # start_index and capacity are not shown because they are allocation details. raw_data hides
        # the unused data before start_index and after start_index+sample_count.
        if self._sample_count > 0:
            args.append(f"raw_data={self.raw_data!r}")
        if self._extended_properties:
            args.append(f"extended_properties={self._extended_properties._properties!r}")
        if self._timing is not Timing.empty:
            args.append(f"timing={self._timing!r}")
        if self._scale_mode is not NO_SCALING:
            args.append(f"scale_mode={self._scale_mode}")
        return f"{self.__class__.__module__}.{self.__class__.__name__}({', '.join(args)})"
