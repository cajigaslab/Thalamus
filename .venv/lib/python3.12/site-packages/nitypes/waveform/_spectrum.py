from __future__ import annotations

import sys
from collections.abc import Mapping, Sequence
from typing import (
    TYPE_CHECKING,
    Any,
    Generic,
    SupportsFloat,
    SupportsIndex,
    Union,
    final,
    overload,
)

import numpy as np
import numpy.typing as npt
from typing_extensions import Self, TypeVar

from nitypes._arguments import arg_to_float, arg_to_uint, validate_dtype
from nitypes._exceptions import invalid_arg_type, invalid_array_ndim
from nitypes._numpy import asarray as _np_asarray, long as _np_long, ulong as _np_ulong
from nitypes.waveform._exceptions import (
    create_capacity_mismatch_error,
    create_capacity_too_small_error,
    create_datatype_mismatch_error,
    create_start_index_or_sample_count_too_large_error,
    create_start_index_too_large_error,
)
from nitypes.waveform._extended_properties import CHANNEL_NAME, UNIT_DESCRIPTION
from nitypes.waveform.typing import ExtendedPropertyValue

if sys.version_info < (3, 10):
    import array as std_array

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import ExtendedPropertyDictionary
else:
    from nitypes.waveform._extended_properties import ExtendedPropertyDictionary

_TData = TypeVar("_TData", bound=Union[np.floating, np.integer])
_TOtherData = TypeVar("_TOtherData", bound=Union[np.floating, np.integer])

# Use the C types here because np.isdtype() considers some of them to be distinct types, even when
# they have the same size (e.g. np.intc vs. np.int_).
_DATA_DTYPES = (
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


@final
class Spectrum(Generic[_TData]):
    """A frequency spectrum, which encapsulates analog data and frequency information.

    Constructing
    ^^^^^^^^^^^^

    To construct a frequency spectrum, use the :class:`Spectrum` class:

    >>> Spectrum()
    nitypes.waveform.Spectrum(0)
    >>> Spectrum(5)
    nitypes.waveform.Spectrum(5, data=array([0., 0., 0., 0., 0.]))

    To construct a frequency spectrum from a NumPy array, use the :any:`Spectrum.from_array_1d`
    method.

    >>> import numpy as np
    >>> Spectrum.from_array_1d(np.array([1.0, 2.0, 3.0]))
    nitypes.waveform.Spectrum(3, data=array([1., 2., 3.]))

    You can also use :any:`Spectrum.from_array_1d` to construct a frequency spectrum from a
    sequence, such as a list. In this case, you must specify the NumPy data type.

    >>> Spectrum.from_array_1d([1.0, 2.0, 3.0], np.float64)
    nitypes.waveform.Spectrum(3, data=array([1., 2., 3.]))

    The 2D version, :any:`Spectrum.from_array_2d`, returns multiple waveforms, one for each row of
    data in the array or nested sequence.

    >>> nested_list = [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]
    >>> Spectrum.from_array_2d(nested_list, np.float64)  # doctest: +NORMALIZE_WHITESPACE
    [nitypes.waveform.Spectrum(3, data=array([1., 2., 3.])),
    nitypes.waveform.Spectrum(3, data=array([4., 5., 6.]))]

    Class members
    ^^^^^^^^^^^^^
    """

    @overload
    @classmethod
    def from_array_1d(
        cls,
        array: npt.NDArray[_TOtherData],
        dtype: None = ...,
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
    ) -> Spectrum[_TOtherData]: ...

    @overload
    @classmethod
    def from_array_1d(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        dtype: type[_TOtherData] | np.dtype[_TOtherData],
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
    ) -> Spectrum[_TOtherData]: ...

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
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
    ) -> Spectrum[Any]: ...

    @classmethod
    def from_array_1d(
        cls,
        array: npt.NDArray[Any] | Sequence[Any],
        dtype: npt.DTypeLike = None,
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
        start_frequency: SupportsFloat | None = None,
        frequency_increment: SupportsFloat | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
    ) -> Spectrum[Any]:
        """Construct a spectrum from a one-dimensional array or sequence.

        Args:
            array: The spectrum data as a one-dimensional array or a sequence.
            dtype: The NumPy data type for the spectrum data. This argument is required
                when array is a sequence.
            copy: Specifies whether to copy the array or save a reference to it.
            start_index: The sample index at which the spectrum data begins.
            sample_count: The number of samples in the spectrum.
            start_frequency: The start frequency of the spectrum.
            frequency_increment: The frequency increment of the spectrum.
            extended_properties: The extended properties of the spectrum.

        Returns:
            A spectrum containing the specified data.
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
            data=_np_asarray(array, dtype, copy=copy),
            start_index=start_index,
            sample_count=sample_count,
            start_frequency=start_frequency,
            frequency_increment=frequency_increment,
            extended_properties=extended_properties,
        )

    @overload
    @classmethod
    def from_array_2d(
        cls,
        array: npt.NDArray[_TOtherData],
        dtype: None = ...,
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
    ) -> Sequence[Spectrum[_TOtherData]]: ...

    @overload
    @classmethod
    def from_array_2d(
        cls,
        array: npt.NDArray[Any] | Sequence[Sequence[Any]],
        dtype: type[_TOtherData] | np.dtype[_TOtherData],
        *,
        copy: bool = ...,
        start_index: SupportsIndex | None = ...,
        sample_count: SupportsIndex | None = ...,
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
    ) -> Sequence[Spectrum[_TOtherData]]: ...

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
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
    ) -> Sequence[Spectrum[Any]]: ...

    @classmethod
    def from_array_2d(
        cls,
        array: npt.NDArray[Any] | Sequence[Sequence[Any]],
        dtype: npt.DTypeLike = None,
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
        start_frequency: SupportsFloat | None = None,
        frequency_increment: SupportsFloat | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
    ) -> Sequence[Spectrum[Any]]:
        """Construct a list of spectrums from a two-dimensional array or nested sequence.

        Args:
            array: The spectrum data as a two-dimensional array or a nested sequence.
            dtype: The NumPy data type for the spectrum data. This argument is required
                when array is a sequence.
            copy: Specifies whether to copy the array or save a reference to it.
            start_index: The sample index at which the spectrum data begins.
            sample_count: The number of samples in the spectrum.
            start_frequency: The start frequency of the spectrum.
            frequency_increment: The frequency increment of the spectrum.
            extended_properties: The extended properties of the spectrum.

        Returns:
            A list containing a spectrum for each row of the specified data.

        When constructing multiple spectrums, the same extended properties, timing
        information, and scale mode are applied to all spectrums. Consider assigning
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
                data=_np_asarray(array[i], dtype, copy=copy),
                start_index=start_index,
                sample_count=sample_count,
                start_frequency=start_frequency,
                frequency_increment=frequency_increment,
                extended_properties=extended_properties,
            )
            for i in range(len(array))
        ]

    __slots__ = [
        "_data",
        "_start_index",
        "_sample_count",
        "_start_frequency",
        "_frequency_increment",
        "_extended_properties",
        "__weakref__",
    ]

    _data: npt.NDArray[_TData]
    _start_index: int
    _sample_count: int
    _start_frequency: float
    _frequency_increment: float
    _extended_properties: ExtendedPropertyDictionary

    # If neither dtype nor data is specified, _TData defaults to np.float64.
    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: Spectrum[np.float64],
        sample_count: SupportsIndex | None = ...,
        dtype: None = ...,
        *,
        data: None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: Spectrum[_TOtherData],
        sample_count: SupportsIndex | None = ...,
        dtype: type[_TOtherData] | np.dtype[_TOtherData] = ...,
        *,
        data: None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: Spectrum[_TOtherData],
        sample_count: SupportsIndex | None = ...,
        dtype: None = ...,
        *,
        data: npt.NDArray[_TOtherData] = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
    ) -> None: ...

    @overload
    def __init__(  # noqa: D107 - Missing docstring in __init__ (auto-generated noqa)
        self: Spectrum[Any],
        sample_count: SupportsIndex | None = ...,
        dtype: npt.DTypeLike = ...,
        *,
        data: npt.NDArray[Any] | None = ...,
        start_index: SupportsIndex | None = ...,
        capacity: SupportsIndex | None = ...,
        start_frequency: SupportsFloat | None = ...,
        frequency_increment: SupportsFloat | None = ...,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = ...,
        copy_extended_properties: bool = ...,
    ) -> None: ...

    def __init__(
        self,
        sample_count: SupportsIndex | None = None,
        dtype: npt.DTypeLike = None,
        *,
        data: npt.NDArray[Any] | None = None,
        start_index: SupportsIndex | None = None,
        capacity: SupportsIndex | None = None,
        start_frequency: SupportsFloat | None = None,
        frequency_increment: SupportsFloat | None = None,
        extended_properties: Mapping[str, ExtendedPropertyValue] | None = None,
        copy_extended_properties: bool = True,
    ) -> None:
        """Initialize a new frequency spectrum.

        Args:
            sample_count: The number of samples in the spectrum.
            dtype: The NumPy data type for the spectrum data.
            data: A NumPy ndarray to use for sample storage. The spectrum takes ownership
                of this array. If not specified, an ndarray is created based on the specified dtype,
                start index, sample count, and capacity.
            start_index: The sample index at which the spectrum data begins.
            sample_count: The number of samples in the spectrum.
            capacity: The number of samples to allocate. Pre-allocating a larger buffer optimizes
                appending samples to the spectrum.
            start_frequency: The start frequency of the spectrum.
            frequency_increment: The frequency increment of the spectrum.
            extended_properties: The extended properties of the spectrum.
            copy_extended_properties: Specifies whether to copy the extended properties or take
                ownership.

        Returns:
            A frequency spectrum.
        """
        if data is None:
            self._init_with_new_array(
                sample_count, dtype, start_index=start_index, capacity=capacity
            )
        elif isinstance(data, np.ndarray):
            self._init_with_provided_array(
                data,
                dtype,
                start_index=start_index,
                sample_count=sample_count,
                capacity=capacity,
            )
        else:
            raise invalid_arg_type("raw data", "NumPy ndarray", data)

        self._start_frequency = arg_to_float("start frequency", start_frequency, 0.0)
        self._frequency_increment = arg_to_float("frequency increment", frequency_increment, 0.0)

        if copy_extended_properties or not isinstance(
            extended_properties, ExtendedPropertyDictionary
        ):
            extended_properties = ExtendedPropertyDictionary(extended_properties)
        self._extended_properties = extended_properties

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
            dtype = np.float64
        validate_dtype(dtype, _DATA_DTYPES)

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
        data: npt.NDArray[_TData],
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
        validate_dtype(dtype, _DATA_DTYPES)

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
    def data(self) -> npt.NDArray[_TData]:
        """The spectrum data."""
        return self._data[self._start_index : self._start_index + self._sample_count]

    def get_data(
        self, start_index: SupportsIndex | None = 0, sample_count: SupportsIndex | None = None
    ) -> npt.NDArray[_TData]:
        """Get a subset of the spectrum data.

        Args:
            start_index: The sample index at which the data begins.
            sample_count: The number of samples to return.

        Returns:
            A subset of the spectrum data.
        """
        start_index = arg_to_uint("start index", start_index, 0)
        if start_index > self.sample_count:
            raise create_start_index_too_large_error(
                start_index, "number of samples in the spectrum", self.sample_count
            )

        sample_count = arg_to_uint("sample count", sample_count, self.sample_count - start_index)
        if start_index + sample_count > self.sample_count:
            raise create_start_index_or_sample_count_too_large_error(
                start_index, sample_count, "number of samples in the spectrum", self.sample_count
            )

        return self.data[start_index : start_index + sample_count]

    @property
    def sample_count(self) -> int:
        """The number of samples in the spectrum."""
        return self._sample_count

    @property
    def start_index(self) -> int:
        """The sample index of the underlying array at which the spectrum data begins."""
        return self._start_index

    @property
    def capacity(self) -> int:
        """The total capacity available for spectrum data.

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
            raise create_capacity_too_small_error(value, min_capacity, "spectrum")
        if value != len(self._data):
            self._data.resize(value, refcheck=False)

    @property
    def dtype(self) -> np.dtype[_TData]:
        """The NumPy dtype for the spectrum data."""
        return self._data.dtype

    @property
    def start_frequency(self) -> float:
        """The start frequency of the spectrum."""
        return self._start_frequency

    @start_frequency.setter
    def start_frequency(self, value: float) -> None:
        if not isinstance(value, (float, int)):
            raise invalid_arg_type("start frequency", "float", value)
        self._start_frequency = value

    @property
    def frequency_increment(self) -> float:
        """The frequency increment of the spectrum."""
        return self._frequency_increment

    @frequency_increment.setter
    def frequency_increment(self, value: float) -> None:
        if not isinstance(value, (float, int)):
            raise invalid_arg_type("frequency increment", "float", value)
        self._frequency_increment = value

    @property
    def extended_properties(self) -> ExtendedPropertyDictionary:
        """The extended properties for the spectrum."""
        return self._extended_properties

    @property
    def channel_name(self) -> str:
        """The name of the device channel from which the spectrum was acquired."""
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
        """The unit of measurement, such as volts, of the spectrum."""
        value = self._extended_properties.get(UNIT_DESCRIPTION, "")
        assert isinstance(value, str)
        return value

    @units.setter
    def units(self, value: str) -> None:
        if not isinstance(value, str):
            raise invalid_arg_type("units", "str", value)
        self._extended_properties[UNIT_DESCRIPTION] = value

    def append(
        self,
        other: npt.NDArray[_TData] | Spectrum[_TData] | Sequence[Spectrum[_TData]],
        /,
    ) -> None:
        """Append data to the spectrum.

        Args:
            other: The array or spectrum(s) to append.

        Raises:
            ValueError: The other array has the wrong number of dimensions.
            TypeError: The data types of the current spectrum and other array or spectrum(s) do not
                match, or an argument has the wrong data type.

        When appending spectrums:

        * Extended properties of the other spectrum(s) are merged into the current spectrum if they
          are not already set in the current spectrum.
        """
        if isinstance(other, np.ndarray):
            self._append_array(other)
        elif isinstance(other, Spectrum):
            self._append_spectrum(other)
        elif isinstance(other, Sequence) and all(isinstance(x, Spectrum) for x in other):
            self._append_spectrums(other)
        else:
            raise invalid_arg_type("input", "array or spectrum(s)", other)

    def _append_array(
        self,
        array: npt.NDArray[_TData],
    ) -> None:
        if array.dtype != self.dtype:
            raise create_datatype_mismatch_error("input array", array.dtype, "spectrum", self.dtype)
        if array.ndim != 1:
            raise invalid_array_ndim("input array", "one-dimensional array", array.ndim)

        self._increase_capacity(len(array))

        offset = self._start_index + self._sample_count
        self._data[offset : offset + len(array)] = array
        self._sample_count += len(array)

    def _append_spectrum(self, spectrum: Spectrum[_TData]) -> None:
        self._append_spectrums([spectrum])

    def _append_spectrums(self, spectrums: Sequence[Spectrum[_TData]]) -> None:
        for spectrum in spectrums:
            if spectrum.dtype != self.dtype:
                raise create_datatype_mismatch_error(
                    "input spectrum", spectrum.dtype, "spectrum", self.dtype
                )

        self._increase_capacity(sum(spectrum.sample_count for spectrum in spectrums))

        offset = self._start_index + self._sample_count
        for spectrum in spectrums:
            self._data[offset : offset + spectrum.sample_count] = spectrum.data
            offset += spectrum.sample_count
            self._sample_count += spectrum.sample_count
            self._extended_properties._merge(spectrum._extended_properties)

    def _increase_capacity(self, amount: int) -> None:
        new_capacity = self._start_index + self._sample_count + amount
        if new_capacity > self.capacity:
            self.capacity = new_capacity

    def load_data(
        self,
        array: npt.NDArray[_TData],
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
    ) -> None:
        """Load new data into an existing spectrum.

        Args:
            array: A NumPy array containing the data to load.
            copy: Specifies whether to copy the array or save a reference to it.
            start_index: The sample index at which the spectrum data begins.
            sample_count: The number of samples in the spectrum.
        """
        if isinstance(array, np.ndarray):
            self._load_array(array, copy=copy, start_index=start_index, sample_count=sample_count)
        else:
            raise invalid_arg_type("input array", "array", array)

    def _load_array(
        self,
        array: npt.NDArray[_TData],
        *,
        copy: bool = True,
        start_index: SupportsIndex | None = 0,
        sample_count: SupportsIndex | None = None,
    ) -> None:
        if array.dtype != self.dtype:
            raise create_datatype_mismatch_error("input array", array.dtype, "spectrum", self.dtype)
        if array.ndim != 1:
            raise invalid_array_ndim("input array", "one-dimensional array", array.ndim)

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
            and np.array_equal(self.data, value.data)
            and self.start_frequency == value.start_frequency
            and self.frequency_increment == value.frequency_increment
            and self._extended_properties == value._extended_properties
        )

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        ctor_args = (self._sample_count, self.dtype)
        ctor_kwargs: dict[str, Any] = {
            "data": self.data,
            "start_frequency": self._start_frequency,
            "frequency_increment": self._frequency_increment,
            "extended_properties": self._extended_properties,
            "copy_extended_properties": False,
        }
        return (self.__class__._unpickle, (ctor_args, ctor_kwargs))

    @classmethod
    def _unpickle(cls, args: tuple[Any, ...], kwargs: dict[str, Any]) -> Self:
        return cls(*args, **kwargs)

    def __repr__(self) -> str:
        """Return repr(self)."""
        args = [f"{self._sample_count}"]
        if self.dtype != np.float64:
            args.append(f"{self.dtype.name}")
        # start_index and capacity are not shown because they are allocation details. data hides
        # the unused data before start_index and after start_index+sample_count.
        if self._sample_count > 0:
            args.append(f"data={self.data!r}")
        if self._start_frequency != 0.0:
            args.append(f"start_frequency={self.start_frequency!r}")
        if self._frequency_increment != 0.0:
            args.append(f"frequency_increment={self._frequency_increment!r}")
        if self._extended_properties:
            args.append(f"extended_properties={self._extended_properties._properties!r}")
        return f"{self.__class__.__module__}.{self.__class__.__name__}({', '.join(args)})"
