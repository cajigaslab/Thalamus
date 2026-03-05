from __future__ import annotations

from collections.abc import Iterable, MutableSequence
from typing import (
    TYPE_CHECKING,
    Any,
    final,
    overload,
)

import numpy as np
import numpy.typing as npt

from nitypes._exceptions import invalid_arg_value, invalid_arg_type

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.bintime import CVIAbsoluteTimeDType, DateTime, TimeValueTuple
else:
    from nitypes.bintime._dtypes import CVIAbsoluteTimeDType
    from nitypes.bintime._datetime import DateTime
    from nitypes.bintime._time_value_tuple import TimeValueTuple


@final
class DateTimeArray(MutableSequence[DateTime]):
    """A mutable array of :class:`DateTime` values in NI Binary Time Format (NI-BTF).

    Raises:
        TypeError: If any item in value is not a DateTime instance.
    """

    __slots__ = ["_array"]

    _array: npt.NDArray[np.void]

    def __init__(
        self,
        value: Iterable[DateTime] | None = None,
    ) -> None:
        """Initialize a new DateTimeArray."""
        value = [] if value is None else list(value)
        if not all(isinstance(item, DateTime) for item in value):
            raise invalid_arg_type("value", "iterable of DateTime", value)
        self._array = np.fromiter(
            (entry.to_tuple().to_cvi() for entry in value),
            dtype=CVIAbsoluteTimeDType,
            count=len(value),
        )

    @overload
    def __getitem__(  # noqa: D105 - missing docstring in magic method
        self, index: int
    ) -> DateTime: ...

    @overload
    def __getitem__(  # noqa: D105 - missing docstring in magic method
        self, index: slice
    ) -> DateTimeArray: ...

    def __getitem__(self, index: int | slice) -> DateTime | DateTimeArray:
        """Return self[index].

        Raises:
            TypeError: If index is an invalid type.
            IndexError: If index is out of range.
        """
        if isinstance(index, int):
            entry = self._array[index].item()
            as_tuple = TimeValueTuple.from_cvi(*entry)
            return DateTime.from_tuple(as_tuple)
        elif isinstance(index, slice):
            sliced_entries = self._array[index]
            new_array = DateTimeArray()
            new_array._array = sliced_entries
            return new_array
        else:
            raise invalid_arg_type("index", "int or slice", index)

    def __len__(self) -> int:
        """Return len(self)."""
        return len(self._array)

    @overload
    def __setitem__(  # noqa: D105 - missing docstring in magic method
        self, index: int, value: DateTime
    ) -> None: ...

    @overload
    def __setitem__(  # noqa: D105 - missing docstring in magic method
        self, index: slice, value: Iterable[DateTime]
    ) -> None: ...

    def __setitem__(self, index: int | slice, value: DateTime | Iterable[DateTime]) -> None:
        """Set a new value for DateTime at the specified location or slice.

        Raises:
            TypeError: If index is an invalid type, or slice value is not iterable.
            ValueError: If slice assignment length doesn't match the selected range.
            IndexError: If index is out of range.
        """
        if isinstance(index, int):
            if not isinstance(value, DateTime):
                raise invalid_arg_type("value", "DateTime", value)
            self._array[index] = value.to_tuple().to_cvi()
        elif isinstance(index, slice):
            if not isinstance(value, Iterable):
                raise invalid_arg_type("value", "iterable of DateTime", value)
            values = list(value)
            if not all(isinstance(item, DateTime) for item in values):
                raise invalid_arg_type("value", "iterable of DateTime", value)

            start, stop, step = index.indices(len(self))
            selected_count = len(range(start, stop, step))
            new_entry_count = len(values)
            if step > 1 and new_entry_count != selected_count:
                raise invalid_arg_value(
                    "value", "iterable with the same length as the slice", value
                )

            if new_entry_count < selected_count:
                # Shrink
                replaced = slice(start, start + new_entry_count)
                removed = slice(start + new_entry_count, stop)
                self._array[replaced] = [item.to_tuple().to_cvi() for item in values]
                del self[removed]
            elif new_entry_count > selected_count:
                # Grow
                replaced = slice(start, stop)
                self._array[replaced] = [
                    item.to_tuple().to_cvi() for item in values[:selected_count]
                ]
                self._array = np.insert(
                    self._array,
                    stop,
                    [item.to_tuple().to_cvi() for item in values[selected_count:]],
                )
            else:
                # Replace, accounting for strides
                self._array[index] = [item.to_tuple().to_cvi() for item in values]
        else:
            raise invalid_arg_type("index", "int or slice", index)

    @overload
    def __delitem__(self, index: int) -> None: ...  # noqa: D105 - missing docstring in magic method

    @overload
    def __delitem__(  # noqa: D105 - missing docstring in magic method
        self, index: slice
    ) -> None: ...

    def __delitem__(self, index: int | slice) -> None:
        """Delete the value at the specified location or slice.

        Raises:
            TypeError: If index is an invalid type.
            IndexError: If index is out of range.
        """
        if isinstance(index, (int, slice)):
            self._array = np.delete(self._array, index)
        else:
            raise invalid_arg_type("index", "int or slice", index)

    def insert(self, index: int, value: DateTime) -> None:
        """Insert the DateTime value before the specified index.

        Raises:
            TypeError: If index is not int or value is not DateTime.
        """
        if not isinstance(index, int):
            raise invalid_arg_type("index", "int", index)
        if not isinstance(value, DateTime):
            raise invalid_arg_type("value", "DateTime", value)
        lower = -len(self._array)
        upper = len(self._array)
        index = min(max(index, lower), upper)
        as_cvi = value.to_tuple().to_cvi()
        self._array = np.insert(self._array, index, as_cvi)

    def extend(self, values: Iterable[DateTime]) -> None:
        """Extend the array by appending the elements from values."""
        if values is None:
            raise invalid_arg_type("values", "iterable of DateTime", values)
        new_array = DateTimeArray(values)
        self._array = np.append(self._array, new_array._array)

    def __eq__(self, other: object) -> bool:
        """Return self == other."""
        if not isinstance(other, DateTimeArray):
            return NotImplemented
        return np.array_equal(self._array, other._array)

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        return (self.__class__, (list(iter(self)),))

    def __repr__(self) -> str:
        """Return repr(self)."""
        ctor_args = list(iter(self))
        return f"{self.__class__.__module__}.{self.__class__.__name__}({ctor_args})"

    def __str__(self) -> str:
        """Return str(self)."""
        values = list(iter(self))
        return f"[{'; '.join(str(v) for v in values)}]"
